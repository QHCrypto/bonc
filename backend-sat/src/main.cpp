#include <frontend_result_parser.h>
#include <sat-modeller.h>
#include <table-template.h>

#include <boost/process.hpp>
#include <fstream>
#include <print>

struct SBoxInputBlock {
  std::vector<bonc::Ref<bonc::BitExpr>> inputs;
  bonc::Ref<bonc::LookupTable> table;

  friend bool operator==(const SBoxInputBlock& lhs,
                         const SBoxInputBlock& rhs) = default;

  friend std::size_t hash_value(const SBoxInputBlock& block) {
    std::size_t seed = 0;
    for (const auto& input : block.inputs) {
      boost::hash_combine(seed, input);
    }
    boost::hash_combine(seed, block.table);
    return seed;
  }
};

struct DifferentialSATModeller {
  bonc::sat_modeller::SATModel model;
  const bonc::sat_modeller::Variable FALSE;
  std::unordered_set<bonc::sat_modeller::Variable> weight_vars;
  std::unordered_set<bonc::sat_modeller::Variable> input_vars;

  bonc::Ref<bonc::LookupTable> AND_TABLE;
  bonc::Ref<bonc::LookupTable> OR_TABLE;

  std::unordered_map<const bonc::LookupTable*,
                     std::unique_ptr<bonc::sat_modeller::TableTemplate>>
      known_templates;
  std::unordered_map<const bonc::BitExpr*, bonc::sat_modeller::Variable>
      modelled_exprs;
  std::unordered_map<SBoxInputBlock, std::vector<bonc::sat_modeller::Variable>>
      modelled_sbox_inputs;

  DifferentialSATModeller() : model{}, FALSE{model.createVariable("FALSE")} {
    model.addClause({-FALSE});
    AND_TABLE = bonc::LookupTable::create("AND", 2, 1, {0, 0, 0, 1});
    OR_TABLE = bonc::LookupTable::create("OR", 2, 1, {0, 1, 1, 1});
  }
  const bonc::sat_modeller::TableTemplate* buildTableTemplate(
      const bonc::LookupTable* lookup) {
    assert(lookup);
    if (auto it = known_templates.find(lookup); it != known_templates.end()) {
      return it->second.get();
    }
    auto& table = lookup->getDDT();
    auto template_ = model.buildTableTemplate(table);
    auto template_ptr = std::make_unique<bonc::sat_modeller::TableTemplate>(
        std::move(template_));
    auto raw_ptr = template_ptr.get();
    known_templates.emplace(lookup, std::move(template_ptr));
    return raw_ptr;
  }

  bonc::sat_modeller::Variable generateFromLookupTable(SBoxInputBlock block,
                                                       int output_offset) {
    std::vector<bonc::sat_modeller::Variable> output_vars;
    if (auto modelled_it = modelled_sbox_inputs.find(block);
        modelled_it != modelled_sbox_inputs.end()) {
      output_vars = modelled_it->second;
    } else {
      auto [inputs, table] = block;
      std::vector<bonc::sat_modeller::Variable> input_vars;
      input_vars.reserve(inputs.size());
      for (const auto& input : inputs) {
        input_vars.push_back(traverse(input));
      }
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
        return FALSE;
      }
      case bonc::BitExpr::Read: {
        auto read_expr = boost::static_pointer_cast<bonc::ReadBitExpr>(expr);
        auto target = read_expr->getTarget();
        auto offset = read_expr->getOffset();
        auto name = target->getName();
        if (target->getKind() == bonc::ReadTarget::Input) {
          if (name == "plaintext") {
            auto input = model.createVariable("iv_" + std::to_string(offset));
            input_vars.insert(input);
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
            SBoxInputBlock{lookup_expr->getInputs(), lookup_expr->getTable()},
            lookup_expr->getOutputOffset());
      }
      case bonc::BitExpr::Not: {
        // NOT 不改变差分传播
        auto not_expr = boost::static_pointer_cast<bonc::NotBitExpr>(expr);
        return traverse(not_expr->getExpr());
      }
      case bonc::BitExpr::And: {
        auto and_expr = boost::static_pointer_cast<bonc::BinaryBitExpr>(expr);
        return generateFromLookupTable(
            SBoxInputBlock{{and_expr->getLeft(), and_expr->getRight()},
                           AND_TABLE},
            0);
      }
      case bonc::BitExpr::Or: {
        auto or_expr = boost::static_pointer_cast<bonc::BinaryBitExpr>(expr);
        return generateFromLookupTable(
            SBoxInputBlock{{or_expr->getLeft(), or_expr->getRight()}, OR_TABLE},
            0);
      }
      case bonc::BitExpr::Xor: {
        auto xor_expr = boost::static_pointer_cast<bonc::BinaryBitExpr>(expr);
        auto left = traverse(xor_expr->getLeft());
        auto right = traverse(xor_expr->getRight());
        if (left == FALSE) {
          return right;
        }
        if (right == FALSE) {
          return left;
        }
        auto result = model.createVariable("xor");
        model.addXorClause({left, right}, result);
        return result;
      }
      default: {
        throw std::runtime_error("Unknown BitExpr kind");
      }
    }
  }

  bonc::sat_modeller::Variable traverse(bonc::Ref<bonc::BitExpr> expr) {
    auto expr_raw = expr.get();
    if (auto it = modelled_exprs.find(expr_raw); it != modelled_exprs.end()) {
      return it->second;
    }
    auto variable = traverse_impl(std::move(expr));
    modelled_exprs.emplace(expr_raw, variable);
    return variable;
  }

  void setWeightLessThen(int k) {
    assert(k > 0);
    model.addSequentialCounterLessEqualClause(
        std::vector(std::from_range, weight_vars), k);
  }
  void assureInputDifferential() {
    if (input_vars.empty()) {
      return;
    }
    auto clause =
        std::vector<bonc::sat_modeller::Literal>(std::from_range, input_vars);
    model.addClause(clause);
  }
};

#include "cmsat-adapter.hpp"

int test_sbox_modelling() {
  bonc::sat_modeller::SATModel model;
  auto TRUE = model.createVariable("TRUE");
  auto FALSE = model.createVariable("FALSE");
  model.addClause({TRUE});
  model.addClause({-FALSE});

  auto table =
      bonc::LookupTable::create("test", 4, 4,
                                {0xE, 0x4, 0xD, 0x1, 0x2, 0xF, 0xB, 0x8, 0x3,
                                 0xA, 0x6, 0xC, 0x5, 0x9, 0x0, 0x7});
  auto ddt = table->getDDT();
  for (const auto& row : ddt) {
    for (const auto& col : row) {
      std::print("{} ", col);
    }
    std::println();
  }
  auto template_ = model.buildTableTemplate(ddt);
  auto output_vars = model.createVariables(4, "outputs");
  auto weight_vars = model.addWeightTableClauses(
      template_, {FALSE, FALSE, FALSE, TRUE}, output_vars);
  auto assignments = bonc::solve(model);
  if (!assignments) {
    return 1;
  }

  for (auto i = 0uz; i < assignments->size(); i++) {
    std::cout << model.getVariableDetail(i).name << " = " << assignments->at(i)
              << '\n';
  }

  return 0;
}

// #define main main2

int main(int argc, char** argv) {
  assert(argc >= 2);
  std::ifstream ifs(argv[1]);
  bonc::FrontendResultParser parser(ifs);

  DifferentialSATModeller modeller;
  for (auto& info : parser.parseAll()) {
    std::cout << "Output: " << info.name << ", Size: " << info.size << "\n";
    for (auto& expr : info.expressions) {
      modeller.traverse(expr);
    }
  }
  modeller.assureInputDifferential();
  modeller.setWeightLessThen(63);
  // std::cout << modeller.modelled_sbox_inputs.size() << '\n';
  // for (auto [key, _] : modeller.modelled_sbox_inputs) {
  //   auto [inputs, table] = key;
  //   inputs.at(0)->print(std::cout);
  //   std::cout << '\n';
  // }

  auto values = bonc::solve(modeller.model);

  if (!values) {
    return 1;
  }

  for (auto& [expr, var] : modeller.modelled_exprs) {
    std::cout << values->at(var.getIndex()) << " | ";
    std::cout << std::setw(20) << modeller.model.getVariableDetail(var.getIndex()).name
              << " | ";
    expr->print(std::cout);
    std::cout << "\n";
  }

  // std::ofstream out("out.cnf");
  // modeller.model.printDIMACS(out);

  // modeller.model.print(std::cout, true);
}