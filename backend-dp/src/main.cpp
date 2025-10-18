#include <frontend_result_parser.h>
#include <sbox_and_input.h>

#include <boost/algorithm/string.hpp>
#include <boost/program_options.hpp>
#include <fstream>
#include <print>

#include "polyhedron.h"

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
  std::unordered_set<std::string> input_names;
  std::unordered_map<const bonc::BitExpr*, bonc::dp::TraverseResult> traversed;
  std::unordered_map<bonc::SBoxInputBlock,
                     std::vector<bonc::dp::TraverseResult>>
      traversed_sbox_inputs;
  bonc::dp::MILPModel model;

  bonc::dp::TraverseResult traverseImpl(bonc::Ref<bonc::BitExpr> expr) {
    if (auto it = traversed.find(expr.get()); it != traversed.end()) {
      auto v = it->second;
      return it->second.reuse(model);
    }
    using Um = bonc::UnmodelledValue;
    using Mo = bonc::DeferredModelledValue;
    using R = bonc::dp::TraverseResult;
    auto kind = expr->getKind();
    switch (kind) {
      case bonc::BitExpr::Constant: {
        auto value =
            boost::static_pointer_cast<bonc::ConstantBitExpr>(expr)->getValue();
        return value ? Um::True : Um::False;
      }
      case bonc::BitExpr::Read: {
        auto [target, offset] =
            boost::static_pointer_cast<bonc::ReadBitExpr>(expr)
                ->getTargetAndOffset();
        if (target->getKind() == bonc::ReadTarget::Input) {
          return Um::Unspecified; // TODO
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
          int sbox_degree = 0;
          for (auto i = 0u; i < sbox->getOutputWidth(); i++) {
            auto anf = lookup_expr->getTable()->getANFRepresentation(i);
            auto max_term = *std::ranges::max_element(
                std::views::iota(0uz, anf.size()), {},
                [&anf](size_t index) -> int { return index * +anf.test(index); });
            auto degree = std::popcount(max_term);
            sbox_degree = std::max(sbox_degree, degree);
          }
          // TODO
        }
        return outputs.at(lookup_expr->getOutputOffset());
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
        auto single_side_modelled_visitor = [kind](Um lhs, Mo rhs) -> R {
          if (kind == bonc::BinaryBitExpr::And && lhs.type == Um::False) {
            return Um::False;
          } else if (kind == bonc::BinaryBitExpr::Or && lhs.type == Um::True) {
            return Um::True;
          } else {
            return Um::Unspecified;
          }
        };
        return std::visit(
            Overload{
                [kind](Um lhs, Um rhs) -> R {
                  if (lhs.type == Um::Unspecified
                      || rhs.type == Um::Unspecified) {
                    return Um::Unspecified;
                  }
                  if (kind == bonc::BitExpr::And) {
                    return (lhs.type == Um::True && rhs.type == Um::True)
                             ? Um::True
                             : Um::False;
                  } else {  // kind == bonc::BitExpr::Or
                    return (lhs.type == Um::False && rhs.type == Um::False)
                             ? Um::False
                             : Um::True;
                  }
                },
                single_side_modelled_visitor,
                [kind, single_side_modelled_visitor](Mo lhs, Um rhs) -> R {
                  return single_side_modelled_visitor(rhs, lhs);
                },
                [this](Mo lhs, Mo rhs) -> R { return model.and_(lhs, rhs); },
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
                    return Um::Unspecified;
                  }
                  return (lhs.type == rhs.type) ? Um::False : Um::True;
                },
                [](Um lhs, Mo rhs) -> R { return rhs; },
                [](Mo lhs, Um rhs) -> R { return lhs; },
                [this](Mo lhs, Mo rhs) -> R { return model.xor_(lhs, rhs); },
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

  void addInputNames(std::span<std::string> names) {
    input_names.insert_range(names);
  }

  bonc::dp::TraverseResult traverse(bonc::Ref<bonc::BitExpr> expr) {
    auto result = traverseImpl(expr);
    auto [it, suc] = traversed.insert({expr.get(), result});
    assert(suc);
    return result;
  }
};

#define main main2

int main(int argc, char** argv) {
  namespace po = boost::program_options;
  po::options_description desc("Allowed options");
  // clang-format off
  desc.add_options()
    ("help", "Print help message")
    ("input", po::value<std::string>()->required(), "Input file containing the frontend result in JSON format")
    ("input-bits,I", po::value<std::string>()->default_value(""), "BONC Input bits' name, format \"name1,name2...\"")
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
  std::vector<std::string> input_names;
  boost::split(input_names, vm["input-bits"].as<std::string>(),
               boost::is_any_of(","));
  if (input_names.size() == 0) {
    throw std::runtime_error(
        "You should at least specify one input name in --input-bits");
  }
  modeller.addInputNames(input_names);

  for (auto& [name, size, expressions] : outputs) {
    std::println("Output: {}, Size: {}", name, size);
    for (auto& expr : expressions) {
      modeller.traverse(expr);
    }
  }
}