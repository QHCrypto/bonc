#include <frontend_result_parser.h>
#include <gurobi_c++.h>
#include <sbox_and_input.h>

#include <boost/algorithm/string.hpp>
#include <boost/program_options.hpp>
#include <fstream>
#include <print>

#include "sbox_modelling.h"

struct EnabledStreamableTypes : boost::program_options::options_description {};

template <typename T>
concept Streamable = std::derived_from<EnabledStreamableTypes, T>
                  && requires(T a, std::ostream& os) {
                       { os << a } -> std::same_as<std::ostream&>;
                     };

template <Streamable T>
struct std::formatter<T> {
  constexpr auto parse(auto&& pc) {
    return pc.begin();
  }

  constexpr auto format(const T& p, auto&& fc) const {
    std::ostringstream ss;
    ss << p;
    return std::ranges::copy(ss.str(), fc.out()).out;
  }
};

#include <unordered_map>

template <typename... F>
struct Overload : F... {
  using F::operator()...;
};

#include "traverse-result.hpp"

class DivisionPropertyModeller {
  std::unordered_map<std::string, std::unordered_set<int>> active_bits;
  std::unordered_map<const bonc::BitExpr*, bonc::dp::TraverseResult> traversed;
  std::unordered_set<bonc::DeferredModelledValue> outputs;
  std::unordered_map<bonc::SBoxInputBlock,
                     std::vector<bonc::dp::TraverseResult>>
      traversed_sbox_inputs;
  bonc::dp::MILPModel model;

  bonc::dp::TraverseResult traverseImpl(bonc::Ref<bonc::BitExpr> expr) {
    using Um = bonc::UnmodelledValue;
    using Mo = bonc::DeferredModelledValue;
    using R = bonc::dp::TraverseResult;
    auto kind = expr->getKind();
    switch (kind) {
      case bonc::BitExpr::Constant: {
        auto value =
            boost::static_pointer_cast<bonc::ConstantBitExpr>(expr)->getValue();
        return value ? R::makeUnmodelled(Um::True)
                     : R::makeUnmodelled(Um::False);
      }
      case bonc::BitExpr::Read: {
        auto [target, offset] =
            boost::static_pointer_cast<bonc::ReadBitExpr>(expr)
                ->getTargetAndOffset();
        if (target->getKind() == bonc::ReadTarget::Input) {
          if (auto it = active_bits.find(target->getName());
              it != active_bits.end()) {
            if (it->second.find(offset) != it->second.end()) {
              return R::makeModelled(model.createDeferredConstant(true), model);
            } else {
              return R::makeModelled(model.createDeferredConstant(false),
                                     model);
            }
          } else {
            return R::makeUnmodelled(Um::Unspecified);
          }
        }
        return traverse(target->update_expressions.at(offset));
      }
      case bonc::BitExpr::Lookup: {
        auto lookup_expr =
            boost::static_pointer_cast<bonc::LookupBitExpr>(expr);
        auto inputs = lookup_expr->getInputs();
        auto sbox = lookup_expr->getTable();
        auto key = bonc::SBoxInputBlock{inputs, sbox};
        std::vector<R> outputs;
        if (auto it = this->traversed_sbox_inputs.find(key);
            it != this->traversed_sbox_inputs.end()) {
          outputs = it->second;
        } else {
          std::vector<R> inputs;
          for (auto& input : lookup_expr->getInputs()) {
            inputs.push_back(traverse(input));
          }
          if (std::ranges::any_of(
                  inputs, [](const auto& v) { return !v.modelled(); })) {
            outputs = std::vector<R>(sbox->getOutputWidth(),
                                     R::makeUnmodelled(Um::Unspecified));
          } else {
            auto vars = inputs | std::views::transform([](const R& r) {
                          return std::get<Mo>(r.variant());
                        })
                      | std::ranges::to<std::vector>();
            std::ranges::generate_n(
                std::back_inserter(vars), sbox->getOutputWidth(),
                [&]() { return model.createDeferredVariable(); });

            auto vertices = divisionPropertyTrail(std::move(sbox));
            auto inequalities = vToH(vertices);
            auto reduced = reduceInequalities(inequalities, vertices);
            for (const auto& [coeff, constant] : reduced) {
              std::vector<bonc::LinearExprItem<Mo>> items;
              std::ranges::transform(vars, coeff, std::back_inserter(items),
                                     [](const Mo& var, int c) {
                                       return bonc::LinearExprItem<Mo>(var, c);
                                     });
              auto expr = bonc::LinearExpr<Mo>(std::move(items), constant);
              model.addConstraint(std::move(expr) >= 0);
            }
            outputs = vars | std::views::drop(inputs.size())
                    | std::views::transform([&](const Mo& var) {
                        return R::makeModelled(var, model);
                      })
                    | std::ranges::to<std::vector>();
          }
          traversed_sbox_inputs.insert({key, outputs});
        }
        auto output_offset = lookup_expr->getOutputOffset();
        if (output_offset >= outputs.size()) {
          return R::makeUnmodelled(Um::False);
        } else {
          return outputs.at(lookup_expr->getOutputOffset());
        }
      }
      case bonc::BitExpr::Not: {
        auto not_operand =
            boost::static_pointer_cast<bonc::NotBitExpr>(expr)->getExpr();
        return traverse(not_operand);
      }
      case bonc::BitExpr::And:
      case bonc::BitExpr::Or: {
        auto binary_expr =
            boost::static_pointer_cast<bonc::BinaryBitExpr>(expr);
        auto lhs = traverse(binary_expr->getLeft());
        auto rhs = traverse(binary_expr->getRight());
        auto single_side_modelled_visitor = [&](Um lhs, Mo rhs) -> R {
          if (kind == bonc::BinaryBitExpr::And) {
            if (lhs.type == Um::False) {
              return R::makeUnmodelled(Um::False);
            } else if (lhs.type == Um::True) {
              return R::makeModelled(rhs, model);
            }
          }
          if (kind == bonc::BinaryBitExpr::Or) {
            if (lhs.type == Um::False) {
              return R::makeModelled(rhs, model);
            } else if (lhs.type == Um::True) {
              return R::makeUnmodelled(Um::True);
            }
          }
          return R::makeUnmodelled(Um::Unspecified);
        };
        return std::visit(
            Overload{
                [kind](Um lhs, Um rhs) -> R {
                  if (lhs.type == Um::Unspecified
                      || rhs.type == Um::Unspecified) {
                    return R::makeUnmodelled(Um::Unspecified);
                  }
                  if (kind == bonc::BitExpr::And) {
                    return (lhs.type == Um::True && rhs.type == Um::True)
                             ? R::makeUnmodelled(Um::True)
                             : R::makeUnmodelled(Um::False);
                  } else {  // kind == bonc::BitExpr::Or
                    return (lhs.type == Um::False && rhs.type == Um::False)
                             ? R::makeUnmodelled(Um::False)
                             : R::makeUnmodelled(Um::True);
                  }
                },
                single_side_modelled_visitor,
                [kind, single_side_modelled_visitor](Mo lhs, Um rhs) -> R {
                  return single_side_modelled_visitor(rhs, lhs);
                },
                [this](Mo lhs, Mo rhs) -> R {
                  return R::makeModelled(model.and_(lhs, rhs), model);
                },
            },
            lhs.variant(), rhs.variant());
      }
      case bonc::BitExpr::Xor: {
        auto binary_expr =
            boost::static_pointer_cast<bonc::BinaryBitExpr>(expr);
        auto lhs = traverse(binary_expr->getLeft());
        auto rhs = traverse(binary_expr->getRight());
        return std::visit(
            Overload{
                [](Um lhs, Um rhs) -> R {
                  if (lhs.type == Um::Unspecified
                      || rhs.type == Um::Unspecified) {
                    return R::makeUnmodelled(Um::Unspecified);
                  }
                  return (lhs.type == rhs.type) ? R::makeUnmodelled(Um::False)
                                                : R::makeUnmodelled(Um::True);
                },
                [&](Um lhs, Mo rhs) -> R {
                  return R::makeModelled(rhs, model);
                },
                [&](Mo lhs, Um rhs) -> R {
                  return R::makeModelled(lhs, model);
                },
                [&](Mo lhs, Mo rhs) -> R {
                  return R::makeModelled(model.xor_(lhs, rhs), model);
                },
            },
            lhs.variant(), rhs.variant());
      }
      default: {
        throw std::runtime_error("Unknown BitExpr kind");
      }
    }
  }

public:
  DivisionPropertyModeller() = default;

  void addActiveBits(const std::string& name,
                     std::unordered_set<int> active_bits) {
    this->active_bits[name] = std::move(active_bits);
  }

  bonc::dp::TraverseResult traverse(bonc::Ref<bonc::BitExpr> expr) {
    if (auto it = traversed.find(expr.get()); it != traversed.end()) {
      return it->second.reuse(model);
    }
    auto result = traverseImpl(expr);
    auto [it, suc] = traversed.insert({expr.get(), result});
    assert(suc);
    return result;
  }

  void markOutput(const bonc::dp::TraverseResult& result) {
    if (result.modelled()) {
      auto var = std::get<bonc::DeferredModelledValue>(result.variant());
      this->outputs.insert(var);
    }
  }

  auto finalize() {
    this->model.setObjective(std::ranges::fold_left(
        outputs, bonc::LinearExpr<bonc::DeferredModelledValue>{}, std::plus{}));
    return this->model.gurobiLpFormat();
  }

  std::unordered_set<const bonc::ModelVar*> getOutputs() const {
    std::unordered_set<const bonc::ModelVar*> result;
    for (auto var : outputs) {
      result.insert(var->getVar());
    }
    return result;
  }
};

int main(int argc, char** argv) try {
  namespace po = boost::program_options;
  po::options_description desc("Allowed options");
  // clang-format off
  desc.add_options()
    ("help", "Print help message")
    ("input", po::value<std::string>()->required(), "Input file containing the frontend result in JSON format")
    ("active-bits,I", po::value<std::string>()->default_value(""), "Specify active bits as initial DP, format \"name1=range;name2=range;...\". Range is comma-separated numbers or a-b for contiguous ranges, e.g., \"0,2,4-7\"")
    ("output-bits,O", po::value<std::string>(), "Specify output bits as target final DP, format \"name1=range;name2=range;...\". Defaults to all output bits. Range is comma-separated numbers or a-b for contiguous ranges, e.g., \"0,2,4-7\"")
    ("output,o", po::value<std::string>()->default_value("output.lp"), "Output LP file")
  ;
  // clang-format on

  po::positional_options_description p;
  p.add("input", -1);

  po::variables_map vm;
  po::store(
      po::command_line_parser(argc, argv).options(desc).positional(p).run(),
      vm);
  po::notify(vm);

  if (vm.count("help")) {
    std::println("{}", desc);
    return 0;
  }

  std::string input_file = vm["input"].as<std::string>();

  std::ifstream ifs(input_file);
  bonc::FrontendResultParser parser(ifs);

  auto [_, _, outputs] = parser.parseAll();

  auto modeller = DivisionPropertyModeller{};
  std::vector<std::string> input_blocks;
  boost::split(input_blocks, vm["active-bits"].as<std::string>(),
               boost::is_any_of(";"));
  for (auto& block : input_blocks) {
    auto eq_pos = block.find('=');
    if (eq_pos == std::string::npos) {
      throw std::runtime_error(
          "Invalid format for --active-bits, expected name=range");
    }
    auto name = block.substr(0, eq_pos);
    auto range_str = block.substr(eq_pos + 1);
    auto active_bits = parseCommaSeparatedNumbers(range_str);
    modeller.addActiveBits(name, std::move(active_bits));
  }

  bool all_output_bits = false;
  std::unordered_map<std::string, std::unordered_set<int>> output_bits;
  if (vm.count("output-bits")) {
    auto output_bits_string = vm["output-bits"].as<std::string>();
    std::vector<std::string> output_blocks;
    all_output_bits = output_bits_string.empty();
    boost::split(output_blocks, output_bits_string, boost::is_any_of(";"));
    for (auto& block : output_blocks) {
      auto eq_pos = block.find('=');
      if (eq_pos == std::string::npos) {
        throw std::runtime_error(
            "Invalid format for --output-bits, expected name=range");
      }
      auto name = block.substr(0, eq_pos);
      auto range_str = block.substr(eq_pos + 1);
      auto bit_indices = parseCommaSeparatedNumbers(range_str);
      output_bits[name] = std::move(bit_indices);
    }
  } else {
    all_output_bits = true;
  }

  for (auto& [name, size, expressions] : outputs) {
    std::println("Output: {}, Size: {}", name, size);
    for (auto i = 0uz; i < expressions.size(); i++) {
      if (all_output_bits
          || (output_bits.count(name) && output_bits.at(name).contains(i))) {
        auto& expr = expressions.at(i);
        std::println("  Bit {}", i);
        auto result = modeller.traverse(expr);
        modeller.markOutput(result);
      }
    }
  }

  auto [var_names, lp_content] = modeller.finalize();
  std::string output_file = vm["output"].as<std::string>();
  {
    std::ofstream ofs(output_file);
    ofs << lp_content;
  }

  GRBEnv env;
  GRBModel model(env, output_file);

  std::unordered_set<std::string> balanced;
  auto output_vars = modeller.getOutputs();
  bool success = false;
  while (balanced.size() < output_vars.size()) {
    model.optimize();
    if (model.get(GRB_IntAttr_Status) == GRB_OPTIMAL) {
      auto obj = model.getObjective();
      if (model.get(GRB_DoubleAttr_ObjVal) > 1) {
        success = true;
        break;
      } else {
        std::cout << "COUNTER = " << balanced.size() << "\n";
        for (auto& v : output_vars) {
          auto name = var_names.at(v);
          auto u = model.getVarByName(name);
          auto temp = u.get(GRB_DoubleAttr_X);
          if (std::abs(temp - 1) < 1e-6) {
            balanced.insert(name);
            u.set(GRB_DoubleAttr_UB, 0);
            model.update();
            break;
          }
        }
      }
    } else if (model.get(GRB_IntAttr_Status) == GRB_INFEASIBLE) {
      success = true;
      break;
    } else {
      throw std::runtime_error("Unknown error!");
    }
  }
  if (success) {
    std::println("Distinguisher found!");
    for (auto& v : balanced) {
      std::print("{} ", v);
    }
    std::println("");
  } else {
    std::println("No distinguisher found.");
  }
} catch (const GRBException& e) {
  std::cerr << "Gurobi Error code = " << e.getErrorCode() << std::endl;
  std::cerr << e.getMessage() << std::endl;
  return 1;
} catch (const std::exception& e) {
  std::cerr << "Error: " << e.what() << std::endl;
  return 1;
}