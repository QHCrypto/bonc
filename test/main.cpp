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
  const bonc::sat_modeller::Variable TRUE;
  std::vector<bonc::sat_modeller::Variable> weight_vars;

  bonc::Ref<bonc::LookupTable> AND_TABLE;
  bonc::Ref<bonc::LookupTable> OR_TABLE;

  std::unordered_map<const bonc::LookupTable*,
                     std::unique_ptr<bonc::sat_modeller::TableTemplate>>
      known_templates;
  std::unordered_map<SBoxInputBlock, std::vector<bonc::sat_modeller::Variable>>
      modelled_sbox_inputs;

  DifferentialSATModeller() : model{}, TRUE{model.createVariable("TRUE")} {
    model.addClause({TRUE});
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

  bonc::sat_modeller::Variable generate_from_lookup_table(SBoxInputBlock block,
                                                          int output_offset) {
    auto [inputs, table] = std::move(block);
    std::vector<bonc::sat_modeller::Variable> output_vars;
    if (auto modelled_it =
            modelled_sbox_inputs.find(SBoxInputBlock{inputs, table});
        modelled_it != modelled_sbox_inputs.end()) {
      output_vars = modelled_it->second;
    } else {
      std::vector<bonc::sat_modeller::Variable> input_vars;
      input_vars.reserve(inputs.size());
      for (const auto& input : inputs) {
        input_vars.push_back(traverse(input));
      }
      auto output_vars = model.createVariables(
          table->getOutputWidth(), std::format("{}_o", table->getName()));

      auto template_ = buildTableTemplate(table.get());
      auto weight_vars =
          model.addWeightTableClauses(*template_, input_vars, output_vars);
      this->weight_vars.append_range(weight_vars);
      modelled_sbox_inputs.emplace(SBoxInputBlock{inputs, table}, output_vars);
    }
    if (output_offset >= output_vars.size()) {
      // Preprocess always runs on 8-bits unit, but s-box can be smaller width
      return TRUE;
    }
    return output_vars.at(output_offset);
  }

  bonc::sat_modeller::Variable traverse(bonc::Ref<bonc::BitExpr> expr) {
    switch (expr->getKind()) {
      case bonc::BitExpr::Constant: {
        return TRUE;
      }
      case bonc::BitExpr::Read: {
        auto read_expr = boost::static_pointer_cast<bonc::ReadBitExpr>(expr);
        auto target = read_expr->getTarget();
        auto offset = read_expr->getOffset();
        auto name = target->getName();
        if (target->getKind() == bonc::ReadTarget::Input) {
          if (name == "iv") {
            return model.createVariable("iv_" + std::to_string(offset));
          } else {
            return TRUE;
          }
        }
        auto expr = target->update_expressions.at(offset);
        return traverse(expr);
      }
      case bonc::BitExpr::Lookup: {
        auto lookup_expr =
            boost::static_pointer_cast<bonc::LookupBitExpr>(expr);
        return generate_from_lookup_table(
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
        return generate_from_lookup_table(
            SBoxInputBlock{{and_expr->getLeft(), and_expr->getRight()},
                           AND_TABLE},
            0);
      }
      case bonc::BitExpr::Or: {
        auto or_expr = boost::static_pointer_cast<bonc::BinaryBitExpr>(expr);
        return generate_from_lookup_table(
            SBoxInputBlock{{or_expr->getLeft(), or_expr->getRight()}, OR_TABLE},
            0);
      }
      case bonc::BitExpr::Xor: {
        auto xor_expr = boost::static_pointer_cast<bonc::BinaryBitExpr>(expr);
        auto left = traverse(xor_expr->getLeft());
        auto right = traverse(xor_expr->getRight());
        if (left == TRUE) {
          return right;
        }
        if (right == TRUE) {
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

  void setWeightLessThen(int k) {
    assert(k > 0);
    model.addSequentialCounterLessEqualClause(weight_vars, k);
  }
};

int main() {
  std::ifstream ifs("bonc_round1.json");
  bonc::FrontendResultParser parser(ifs);

  DifferentialSATModeller modeller;
  for (auto& info : parser.parseAll()) {
    std::cout << "Output: " << info.name << ", Size: " << info.size << "\n";
    for (auto& expr : info.expressions) {
      modeller.traverse(expr);
      goto end_loop;
    }
  }
end_loop:;
  modeller.setWeightLessThen(4);
  std::ofstream out("out.cnf");
  modeller.model.printDIMACS(out);
}