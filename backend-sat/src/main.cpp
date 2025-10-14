#include <frontend_result_parser.h>
#include <sat_modeller.h>
#include <sbox_and_input.h>
#include <table_template.h>

#include <boost/algorithm/string.hpp>
#include <boost/regex.hpp>
#include <fstream>
#include <print>

#ifdef USE_CRYPTOMINISAT5
#include "cmsat_adapter.hpp"
#endif

class Modeller {
public:
  enum class ModellingType { DDT, LAT };

  const ModellingType type;
  bonc::sat_modeller::SATModel model;
  const bonc::sat_modeller::Variable FALSE;

private:
  std::unordered_set<bonc::sat_modeller::Variable> weight_vars;
  std::unordered_set<bonc::sat_modeller::Variable> input_vars;
  std::unordered_set<std::string> input_names;

  bonc::Ref<bonc::LookupTable> AND_TABLE;
  bonc::Ref<bonc::LookupTable> OR_TABLE;

  std::unordered_map<const bonc::LookupTable*,
                     std::unique_ptr<bonc::sat_modeller::TableTemplate>>
      known_templates;
  std::unordered_map<const bonc::BitExpr*, bonc::sat_modeller::Variable>
      modelled_exprs;
  std::unordered_map<bonc::SBoxInputBlock,
                     std::vector<bonc::sat_modeller::Variable>>
      modelled_sbox_inputs;

public:
  explicit Modeller(ModellingType type)
      : type{type}, model{}, FALSE{model.createVariable("FALSE")} {
    model.addClause({-FALSE});
    AND_TABLE = bonc::LookupTable::create("AND", 2, 1, {0, 0, 0, 1});
    OR_TABLE = bonc::LookupTable::create("OR", 2, 1, {0, 1, 1, 1});
  }

  void addInputNames(std::span<std::string> names) {
    input_names.insert_range(names);
  }

  bonc::sat_modeller::Literal::ValueT getExprIndex(
      bonc::Ref<bonc::BitExpr> expr) {
    if (auto it = modelled_exprs.find(expr.get()); it != modelled_exprs.end()) {
      return it->second.getIndex();
    } else {
      return -1;
    }
  }

  const auto& getWeightVars() const {
    return weight_vars;
  }

private:
  const bonc::sat_modeller::TableTemplate* buildTableTemplate(
      const bonc::LookupTable* lookup) {
    using namespace bonc::sat_modeller;
    assert(lookup);
    if (auto it = known_templates.find(lookup); it != known_templates.end()) {
      return it->second.get();
    }
    auto& table =
        type == ModellingType::DDT ? lookup->getDDT() : lookup->getLAT();
    auto input_width = lookup->getInputWidth();
    SATModel::GetWeightFunction ddt_weight_fn =
        [input_width](int x) -> std::size_t {
      return input_width - int(std::log2(x));
    };
    SATModel::GetWeightFunction lat_weight_fn =
        [input_width](int x) -> std::size_t {
      return input_width - int(std::log2(std::abs(x))) - 1;
    };
    auto weight_fn = type == ModellingType::DDT ? ddt_weight_fn : lat_weight_fn;
    auto template_ = model.buildTableTemplate(table, std::move(weight_fn));
    auto template_ptr = std::make_unique<TableTemplate>(std::move(template_));
    auto raw_ptr = template_ptr.get();
    known_templates.emplace(lookup, std::move(template_ptr));
    return raw_ptr;
  }

  bonc::sat_modeller::Variable generateFromLookupTable(
      bonc::SBoxInputBlock block, int output_offset) {
    std::vector<bonc::sat_modeller::Variable> output_vars;
    if (auto modelled_it = modelled_sbox_inputs.find(block);
        modelled_it != modelled_sbox_inputs.end()) {
      output_vars = modelled_it->second;
    } else {
      auto [inputs, table] = block;
      std::vector<bonc::sat_modeller::Variable> input_vars;
      input_vars.reserve(inputs.size());
      std::ranges::transform(inputs, std::back_inserter(input_vars),
                             std::bind_front(&Modeller::traverse, this));
      output_vars = model.createVariables(
          table->getOutputWidth(), std::format("{}_o", table->getName()));

      auto template_ = buildTableTemplate(table.get());

      auto weight_vars =
          model.addWeightTableClauses(*template_, input_vars, output_vars);
      this->weight_vars.insert_range(weight_vars);
      modelled_sbox_inputs.emplace(block, output_vars);
    }
    if (output_offset >= int(output_vars.size())) {
      // Preprocess always runs on 8-bits unit, but s-box can be smaller width
      return FALSE;
    }
    return output_vars.at(output_offset);
  }

  bonc::sat_modeller::Variable traverse_impl(bonc::Ref<bonc::BitExpr> expr) {
    switch (expr->getKind()) {
      case bonc::BitExpr::Constant: {
        if (this->type == ModellingType::DDT) {
          return FALSE;
        } else {
          return model.createVariable("const");
        }
      }
      case bonc::BitExpr::Read: {
        auto read_expr = boost::static_pointer_cast<bonc::ReadBitExpr>(expr);
        auto target = read_expr->getTarget();
        auto offset = read_expr->getOffset();
        auto name = target->getName();
        if (target->getKind() == bonc::ReadTarget::Input) {
          bool is_input_bit = this->input_names.contains(name);
          if (this->type == ModellingType::LAT || is_input_bit) {
            auto input =
                model.createVariable(std::format("input_{}_{}", name, offset));
            if (is_input_bit) {
              this->input_vars.insert(input);
            }
            return input;
          } else {
            return FALSE;
          }
        }
        auto expr = target->update_expressions.at(offset);
        return traverse(expr);
      }
      case bonc::BitExpr::Lookup: {
        auto lookup_expr =
            boost::static_pointer_cast<bonc::LookupBitExpr>(expr);
        return generateFromLookupTable(
            bonc::SBoxInputBlock{lookup_expr->getInputs(),
                                 lookup_expr->getTable()},
            lookup_expr->getOutputOffset());
      }
      case bonc::BitExpr::Not: {
        // NOT 不改变差分传播/线性掩码
        auto not_expr = boost::static_pointer_cast<bonc::NotBitExpr>(expr);
        return traverse(not_expr->getExpr());
      }
      case bonc::BitExpr::And: {
        auto and_expr = boost::static_pointer_cast<bonc::BinaryBitExpr>(expr);
        return generateFromLookupTable(
            bonc::SBoxInputBlock{{and_expr->getLeft(), and_expr->getRight()},
                                 AND_TABLE},
            0);
      }
      case bonc::BitExpr::Or: {
        auto or_expr = boost::static_pointer_cast<bonc::BinaryBitExpr>(expr);
        return generateFromLookupTable(
            bonc::SBoxInputBlock{{or_expr->getLeft(), or_expr->getRight()},
                                 OR_TABLE},
            0);
      }
      case bonc::BitExpr::Xor: {
        auto xor_expr = boost::static_pointer_cast<bonc::BinaryBitExpr>(expr);
        auto left = traverse(xor_expr->getLeft());
        auto right = traverse(xor_expr->getRight());
        if (this->type == ModellingType::DDT) {
          if (left == FALSE) {
            return right;
          }
          if (right == FALSE) {
            return left;
          }
          auto result = model.createVariable("xor");
          model.addXorClause({left, right}, result);
          return result;
        } else {
          // 线性传播过 XOR 要求两侧掩码相同
          model.addEquivalentClause({left, right});
          return left;
        }
      }
      default: {
        throw std::runtime_error("Unknown BitExpr kind");
      }
    }
  }

public:
  bonc::sat_modeller::Variable traverse(bonc::Ref<bonc::BitExpr> expr) {
    auto expr_raw = expr.get();
    if (auto it = modelled_exprs.find(expr_raw); it != modelled_exprs.end()) {
      return it->second;
    }
    auto variable = traverse_impl(std::move(expr));
    modelled_exprs.emplace(expr_raw, variable);
    return variable;
  }

  void complete(std::optional<std::size_t> max_weight = std::nullopt) {
    std::cerr << this->input_vars.size() << '\n';
    // for (auto i : input_vars) {
    //   std::cerr << this->model.getVariableDetail(i.getIndex()).name << '\n';
    // }
    this->setWeightLessThen(max_weight ? *max_weight
                                       : (this->type == ModellingType::DDT
                                              ? this->input_vars.size()
                                              : this->input_vars.size() / 2));
    this->assureInputNotEmpty();
  }

#ifdef USE_CRYPTOMINISAT5
  void debugSolution(const std::vector<bonc::SolvedModelValue>& values) const {
    for (auto& [expr, var] : modelled_exprs) {
      std::cout << values.at(var.getIndex()) << " | ";
      std::cout << std::setw(20) << model.getVariableDetail(var.getIndex()).name
                << " | ";
      expr->print(std::cout);
      std::cout << "\n";
    }
  }
#endif

private:
  void setWeightLessThen(int k) {
    assert(k > 0);
    model.addSequentialCounterLessEqualClause(
        std::vector(std::from_range, weight_vars), k);
  }
  void assureInputNotEmpty() {
    if (input_vars.empty()) {
      return;
    }
    auto clause =
        std::vector<bonc::sat_modeller::Literal>(std::from_range, input_vars);
    model.addClause(clause);
  }
};

void printStateValue(std::vector<bonc::SolvedModelValue> values) {
  unsigned short value = 0;  // per 4 bits
  for (auto i = 0uz; i < values.size(); i++) {
    if (values.at(i) == bonc::SolvedModelValue::True) {
      value |= (1 << (i % 4));
    }
    if (i % 4 == 3) {
      if (value) {
        std::print("{:x}", value);
        value = 0;
      } else {
        std::print("-");
      }
    }
  }
  std::println("");
}

int test_sbox_modelling() {
  bonc::sat_modeller::SATModel model;
  auto TRUE = model.createVariable("TRUE");
  auto FALSE = model.createVariable("FALSE");
  model.addClause({TRUE});
  model.addClause({-FALSE});

  auto a = model.createVariable("a");
  auto b = model.createVariable("b");
  model.addEquivalentClause({a, b});
  model.print(std::cout, true);

  auto table =
      bonc::LookupTable::create("test", 4, 4,
                                {0xE, 0x4, 0xD, 0x1, 0x2, 0xF, 0xB, 0x8, 0x3,
                                 0xA, 0x6, 0xC, 0x5, 0x9, 0x0, 0x7});
  auto ddt = table->getLAT();
  for (const auto& row : ddt) {
    for (const auto& col : row) {
      std::print("{:>2} ", col);
    }
    std::println();
  }
  // auto template_ = model.buildTableTemplate(ddt);
  // auto output_vars = model.createVariables(4, "outputs");
  // auto weight_vars = model.addWeightTableClauses(
  //     template_, {FALSE, FALSE, FALSE, TRUE}, output_vars);
  // auto assignments = bonc::solve(model);
  // if (!assignments) {
  //   return 1;
  // }

  // for (auto i = 0uz; i < assignments->size(); i++) {
  //   std::cout << model.getVariableDetail(i).name << " = " <<
  //   assignments->at(i)
  //             << '\n';
  // }

  return 0;
}

// #define main main2

#include <boost/program_options.hpp>
#include <ranges>

namespace po = boost::program_options;

int main(int argc, char** argv) {
  bool is_differential = false;
  bool is_linear = false;
  bool solve = false;

  po::options_description desc("Allowed options");
  // clang-format off
  desc.add_options()
    ("help", "Print help message")
    ("input", po::value<std::string>()->required(), "Input file containing the frontend result in JSON format")
    ("differential,d", po::bool_switch(&is_differential), "Construct differential propagation model")
    ("linear,l", po::bool_switch(&is_linear), "Construct linear propagation model")
    ("input-bits,I", po::value<std::string>()->default_value(""), "BONC Input bits' name, format \"name1,name2...\"")
    ("max-weight,w", po::value<int>(), "Max weight (probability or correlation) allowed; defaults to input size / 2 for linear, input size for differential")
    ("output", po::value<std::string>(), "Output file to write the model in DIMACS format")
    ("solve", po::bool_switch(&solve), "Solve the model using cryptominisat5")
    ("print-states", po::value<std::string>()->default_value(".*"), "A regex pattern to filter state variable solutions to print")
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
    std::cout << desc << '\n';
    return 0;
  }

  std::string input_file = vm["input"].as<std::string>();

  std::ifstream ifs(input_file);
  bonc::FrontendResultParser parser(ifs);

  if (is_differential == is_linear) {
    throw std::runtime_error(
        "Cannot specify both --differential and --linear nor none of each");
  }
  auto modelling_type =
      is_linear ? Modeller::ModellingType::LAT : Modeller::ModellingType::DDT;

  Modeller modeller(modelling_type);

  std::vector<std::string> input_names;
  boost::split(input_names, vm["input-bits"].as<std::string>(),
               boost::is_any_of(","));
  if (input_names.size() == 0) {
    throw std::runtime_error(
        "You should at least specify one input name in --input-bits");
  }
  modeller.addInputNames(input_names);

  auto [inputs, iterations, outputs] = parser.parseAll();
  for (auto& info : outputs) {
    std::cout << "Output: " << info.name << ", Size: " << info.size << "\n";
    for (auto& expr : info.expressions) {
      modeller.traverse(expr);
    }
  }
  std::optional<std::size_t> max_weight;
  if (vm.count("max-weight")) {
    max_weight = vm["max-weight"].as<int>();
    if (max_weight <= 0) {
      throw std::runtime_error("--max-weight must be positive");
    }
  }
  modeller.complete(max_weight);

  if (vm.count("output")) {
    auto out_file = vm["output"].as<std::string>();
    std::ofstream ofs(out_file);
    modeller.model.printDIMACS(ofs);
  }

  if (solve) {
    auto values = bonc::solve(modeller.model);
    if (!values) {
      std::println("UNSATISFIABLE");
      return 1;
    }
    std::println("SATISFIABLE");

    std::size_t weight = 0;
    for (auto var : modeller.getWeightVars()) {
      auto var_index = var.getIndex();
      if (values->at(var_index) == bonc::SolvedModelValue::True) {
        weight++;
      }
    }
    std::println("{}: 2^-{}", is_differential ? "Probability" : "Correlation",
                 weight);

    auto state_name_regex = boost::regex(vm["print-states"].as<std::string>());

    for (bonc::Ref<bonc::ReadTarget>& target :
         std::views::concat(inputs, iterations)) {
      auto& name = target->getName();
      if (!boost::regex_match(name, state_name_regex)) {
        continue;
      }
      std::println("State {}: ", name);
      std::vector<bonc::SolvedModelValue> state_values;
      for (auto index : std::views::iota(0uz, target->getSize() * CHAR_BIT)) {
        auto expr = parser.createExpr<bonc::ReadBitExpr>(target, index);
        auto var_index = modeller.getExprIndex(expr);
        if (var_index <= 0) {
          state_values.push_back(bonc::SolvedModelValue::Undefined);
          continue;
        }
        state_values.push_back(values->at(var_index));
      }
      printStateValue(state_values);
    }
  }
}