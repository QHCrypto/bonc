#include <frontend_result_parser.h>

#include <boost/process.hpp>
#include <fstream>

struct ModelVariable;

struct SATLiteral {
  const ModelVariable* variable;
  bool negated;

  friend bool operator==(const SATLiteral& lhs,
                         const SATLiteral& rhs) = default;
};

struct ModelVariable {
  const std::string name;

  SATLiteral pos() const {
    return {this, false};
  }
  SATLiteral neg() const {
    return {this, true};
  }
};

struct SATClause {
  std::vector<SATLiteral> literals;

  SATClause() = default;
  SATClause(std::initializer_list<SATLiteral> lits) : literals(lits) {}

  auto begin(this auto&& self) {
    return self.literals.begin();
  }
  auto end(this auto&& self) {
    return self.literals.end();
  }

  void push_back(const SATLiteral& literal) {
    literals.push_back(literal);
  }
  void push_back(SATLiteral&& literal) {
    literals.push_back(std::move(literal));
  }
};

class TableSATTemplate {
public:
  enum Entry { Positive, Negative, NotTaken };

private:
  std::vector<std::vector<Entry>> clauses;

public:
  static TableSATTemplate fromEspressoOutput(std::istream& espresso_output) {
    TableSATTemplate template_;
    std::string line;
    while (std::getline(espresso_output, line)) {
      if (line.empty() || line[0] == '.') {
        continue;  // Skip empty lines and comments
      }
      std::vector<Entry> clause;
      for (char c : line) {
        if (c == '1') {
          clause.push_back(Entry::Negative);
        } else if (c == '0') {
          clause.push_back(Entry::Positive);
        } else if (c == '-') {
          clause.push_back(Entry::NotTaken);
        } else {
          break;
        }
      }
      if (!clause.empty()) {
        template_.clauses.push_back(std::move(clause));
      }
    }
    return template_;
  }

  void print(std::ostream& os) const {
    for (const auto& clause : clauses) {
      for (const auto& entry : clause) {
        switch (entry) {
          case Entry::Positive: os << "1 "; break;
          case Entry::Negative: os << "0 "; break;
          case Entry::NotTaken: os << "- "; break;
        }
      }
      os << "\n";
    }
  }

  auto begin(this auto&& self) {
    return self.clauses.begin();
  }
  auto end(this auto&& self) {
    return self.clauses.end();
  }
};

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

class DifferentialSATModel {
  std::unordered_set<std::unique_ptr<ModelVariable>> variables;

  using Var = const ModelVariable*;
  Var TRUE;
  Var FALSE;

  std::vector<SATClause> constraints;

  std::unordered_map<const bonc::LookupTable*,
                     std::unique_ptr<TableSATTemplate>>
      tableTemplates;
  std::unordered_map<SBoxInputBlock, std::vector<Var>> modelledSboxInputs;

  const TableSATTemplate* buildTableTemplate(const bonc::LookupTable* lookup) {
    assert(lookup);
    if (auto it = tableTemplates.find(lookup); it != tableTemplates.end()) {
      return it->second.get();
    }
    auto& table = lookup->getDDT();
    assert(table.size() > 1 && table.at(0).size() > 1);
    auto input_width = std::bit_width(table.size() - 1);
    auto output_width = std::bit_width(table.at(0).size() - 1);
    auto get_weight = [output_width](std::uint64_t x) {
      return output_width - int(std::log2(x));
    };
    std::string espresso_input;
    espresso_input +=
        std::format(".i {}\n.o 1\n", input_width + 2 * output_width);
    for (auto i = 0uz; i < table.size(); i++) {
      const auto& row = table.at(i);
      for (auto j = 0uz; j < row.size(); j++) {
        auto val = row.at(j);
        if (val) {
          std::string input_bitvec = std::format("{:0{}b}", i, input_width);
          std::string output_bitvec = std::format("{:0{}b}", j, output_width);
          auto weight = get_weight(val);
          std::string weight_vec = std::format(
              "{:0>{}}{:1>{}}", "", output_width - weight, "", weight);
          espresso_input += std::format("{}{}{} 1\n", input_bitvec,
                                        output_bitvec, weight_vec);
        }
      }
    }
    espresso_input += ".e\n";
    boost::process::opstream proc_input;
    boost::process::ipstream proc_output;
    boost::process::child c(
        "/home/guyutongxue/Downloads/espresso-logic/bin/espresso", "-epos",
        boost::process::std_in<proc_input, boost::process::std_out>
            proc_output);
    proc_input << espresso_input;
    proc_input.close();
    c.wait();
    auto template_ = TableSATTemplate::fromEspressoOutput(proc_output);
    template_.print(std::cout);
    auto ptr = std::make_unique<TableSATTemplate>(std::move(template_));
    const auto* ptr_raw = ptr.get();
    tableTemplates[lookup] = std::move(ptr);
    return ptr_raw;
  }

  bonc::Ref<bonc::LookupTable> AND_TABLE;
  bonc::Ref<bonc::LookupTable> OR_TABLE;

  Var generate_from_lookup_table(SBoxInputBlock block, int output_offset) {
    auto [inputs, table] = std::move(block);
    auto template_ = buildTableTemplate(table.get());
    std::vector<Var> output_vars;
    if (auto modelled_it =
            modelledSboxInputs.find(SBoxInputBlock{inputs, table});
        modelled_it != modelledSboxInputs.end()) {
      output_vars = modelled_it->second;
    } else {
      std::vector<Var> vars;
      vars.reserve(inputs.size());
      for (const auto& input : inputs) {
        vars.push_back(generate(input));
      }
      for (auto i = 0uz; i < table->getOutputWidth(); i++) {
        output_vars.push_back(
            createVariable(std::format("{}_output_{}", table->getName(), i)));
      }
      auto weight_vars = createWeightVariables(
          std::format("{}_weight", table->getName()), table->getOutputWidth());
      std::ranges::copy(output_vars, std::back_inserter(vars));
      std::ranges::copy(weight_vars, std::back_inserter(vars));

      auto template_ = buildTableTemplate(table.get());
      for (auto& clause : *template_) {
        SATClause sat_clause;
        for (std::size_t i = 0; i < clause.size(); i++) {
          const auto& entry = clause[i];
          if (entry == TableSATTemplate::Positive) {
            sat_clause.push_back(vars.at(i)->pos());
          } else if (entry == TableSATTemplate::Entry::Negative) {
            sat_clause.push_back(vars.at(i)->neg());
          } else if (entry == TableSATTemplate::Entry::NotTaken) {
            // Do nothing, this input is not used
          }
        }
        constraints.push_back(std::move(sat_clause));
      }
      modelledSboxInputs.emplace(SBoxInputBlock{inputs, table}, output_vars);
    }
    if (output_offset >= output_vars.size()) {
      // Preprocess always runs on 8-bits unit, but s-box can be smaller width
      return TRUE;
    }
    return output_vars.at(output_offset);
  }

public:
  DifferentialSATModel() {
    TRUE = createVariable("TRUE");
    FALSE = createVariable("FALSE");
    constraints.push_back(SATClause{TRUE->pos()});
    constraints.push_back(SATClause{FALSE->neg()});

    AND_TABLE = bonc::LookupTable::create("AND", 2, 1, {0, 0, 0, 1});
    OR_TABLE = bonc::LookupTable::create("OR", 2, 1, {0, 1, 1, 1});
  }

  Var createVariable(const std::string& name) {
    auto var = new ModelVariable{name};
    variables.insert(std::unique_ptr<ModelVariable>(var));
    return var;
  }

  std::vector<std::vector<Var>> weightVariables;
  std::vector<Var> createWeightVariables(const std::string& name,
                                         std::size_t size) {
    std::vector<Var> vars;
    vars.reserve(size);
    for (std::size_t i = 0; i < size; i++) {
      vars.push_back(createVariable(name + "_" + std::to_string(i)));
    }
    weightVariables.push_back(std::move(vars));
    return weightVariables.back();
  }

  Var generate(bonc::Ref<bonc::BitExpr> expr) {
    // TODO cache
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
            return createVariable("IV_" + std::to_string(offset));
          } else {
            return TRUE;
          }
        }
        auto expr = target->update_expressions.at(offset);
        return generate(expr);
      }
      case bonc::BitExpr::Lookup: {
        auto lookup_expr =
            boost::static_pointer_cast<bonc::LookupBitExpr>(expr);
        return generate_from_lookup_table(
            SBoxInputBlock{lookup_expr->getInputs(), lookup_expr->getTable()},
            lookup_expr->getOutputOffset());
      }
      case bonc::BitExpr::Not: {
        auto not_expr = boost::static_pointer_cast<bonc::NotBitExpr>(expr);
        return generate(not_expr->getExpr());
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
        auto left = generate(xor_expr->getLeft());
        auto right = generate(xor_expr->getRight());
        if (left == TRUE) {
          return right;
        }
        if (right == TRUE) {
          return left;
        }
        auto result = createVariable(
            std::format("{}_xor_{}", left->name, right->name));
        constraints.push_back(SATClause{left->neg(), right->neg(), result->neg()});
        constraints.push_back(SATClause{left->pos(), right->pos(), result->neg()});
        constraints.push_back(SATClause{left->neg(), right->pos(), result->pos()});
        constraints.push_back(SATClause{left->pos(), right->neg(), result->pos()});
        break;
      }
      default: {
        throw std::runtime_error("Unknown BitExpr kind");
      }
    }
    // TODO
    return TRUE;
  }
};

int main() {
  std::ifstream ifs("bonc_round1.json");
  bonc::FrontendResultParser parser(ifs);

  DifferentialSATModel model;
  for (auto& info : parser.parseAll()) {
    std::cout << "Output: " << info.name << ", Size: " << info.size << "\n";
    for (auto& expr : info.expressions) {
      model.generate(expr);
    }
  }
}