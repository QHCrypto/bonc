#include "sat_modeller.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <format>
#include <unordered_set>
#include <print>

#include "combinations.hpp"
#include "espresso_wrapper.h"

namespace bonc::sat_modeller {

Variable SATModel::createVariable(const std::string& name) {
  variables.push_back({name});
  return Variable(variables.size() - 1);
}

std::vector<Variable> SATModel::createVariables(
    std::size_t count, const std::string& name_prefix) {
  std::vector<Variable> vars;
  for (std::size_t i = 0; i < count; ++i) {
    vars.push_back(createVariable(std::format("{}_{}", name_prefix, i)));
  }
  return vars;
}

void SATModel::addClause(const std::vector<Literal>& lits) {
  clauses.push_back({lits});
}

TableTemplate SATModel::buildTableTemplate(const RawTable& table, SATModel::GetWeightFunction weight_fn) {
  assert(table.size() > 1 && table.at(0).size() > 1);
  auto input_width = std::bit_width(table.size() - 1);
  auto output_width = std::bit_width(table.at(0).size() - 1);

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
        auto weight = weight_fn(val);
        std::string weight_vec = std::format("{:0>{}}{:1>{}}", "",
                                             output_width - weight, "", weight);
        espresso_input +=
            std::format("{}{}{} 1\n", input_bitvec, output_bitvec, weight_vec);
      }
    }
  }
  espresso_input += ".e\n";

  // run espresso
  espresso_cxx::setPos(true);
  auto pla = espresso_cxx::readPlaForEspresso(espresso_input);
  auto table_template = espresso_cxx::plaToTableTemplate(pla);
  return table_template;
}

std::vector<Variable> SATModel::addWeightTableClauses(
    const TableTemplate& table, const std::vector<Variable>& inputs,
    const std::vector<Variable>& outputs) {
  // generate espresso input
  assert(table.size() > 1 && table.at(0).size() > 1);
  auto input_width = inputs.size();
  auto output_width = outputs.size();
  assert(input_width + output_width * 2 == table.at(0).size());

  auto weight_vars = createVariables(output_width, "w");
  for (const auto& row : table) {
    auto clause = std::vector<Literal>();
    for (auto i = 0uz; i < row.size(); i++) {
      auto entry = row.at(i);
      auto var = i < input_width ? inputs.at(i)
               : i < input_width + output_width
                   ? outputs.at(i - input_width)
                   : weight_vars.at(i - input_width - output_width);
      switch (entry) {
        case TableTemplate::Entry::Positive: clause.push_back(var); break;
        case TableTemplate::Entry::Negative: clause.push_back(-var); break;
        case TableTemplate::Entry::Unknown: break;
        case TableTemplate::Entry::NotTaken: break;
      }
    }
    addClause(clause);
  }
  return weight_vars;
}

void SATModel::addXorClause(const std::vector<Variable>& values,
                            Variable result) {
  std::vector<Variable> operands{values};
  operands.push_back(result);
  for (auto i = 1uz; i < operands.size() + 1; i += 2) {
    for (auto subset : combinations<std::unordered_set>(operands, i)) {
      std::vector<Literal> clause;
      for (auto operand : operands) {
        if (subset.contains(operand)) {
          clause.push_back(-operand);
        } else {
          clause.push_back(operand);
        }
      }
      addClause(clause);
    }
  }
}

void SATModel::addAndClause(const std::vector<Variable>& values,
                            Variable result) {
  std::vector<Literal> clause;
  for (auto value : values) {
    clause.push_back(value);
    clause.push_back(-result);
    addClause(clause);
    clause.clear();
  }
  for (auto value : values) {
    clause.push_back(-value);
  }
  clause.push_back(result);
  addClause(clause);
}

void SATModel::addOrClause(const std::vector<Variable>& values,
                           Variable result) {
  std::vector<Literal> clause;
  for (auto value : values) {
    clause.push_back(-value);
    clause.push_back(result);
    addClause(clause);
    clause.clear();
  }
  for (auto value : values) {
    clause.push_back(value);
  }
  clause.push_back(-result);
  addClause(clause);
}

void SATModel::addEquivalentClause(const std::vector<Variable>& values) {
  std::vector<Variable> rotated_values;
  rotated_values.reserve(values.size());
  std::ranges::rotate_copy(values, values.begin() + 1, std::back_inserter(rotated_values));
  for (auto i = 0uz; i < values.size(); i++) {
    addClause({-values.at(i), rotated_values.at(i)});
  }
}

void SATModel::addSequentialCounterLessEqualClause(std::vector<Variable> x,
                                                   int k) {
  auto n = x.size();
  assert(n >= 2);
  std::vector<std::vector<Variable>> s;
  for (auto i = 0uz; i < n - 1; i++) {
    s.push_back(createVariables(k, std::format("seq_cnt_s_{}", i)));
  }
  addClause({-x.at(0), s.at(0).at(0)});
  for (auto j = 1; j < k; j++) {
    addClause({-s.at(0).at(j)});
  }
  for (auto i = 1uz; i < n - 1; i++) {
    addClause({-x.at(i), s.at(i).at(0)});
    addClause({-s.at(i - 1).at(0), s.at(i).at(0)});
    for (auto j = 1; j < k; j++) {
      addClause({-x.at(i), -s.at(i - 1).at(j - 1), s.at(i).at(j)});
    }
    for (auto j = 1; j < k; j++) {
      addClause({-s.at(i - 1).at(j), s.at(i).at(j)});
    }
    addClause({-x.at(i), -s.at(i - 1).at(k - 1)});
  }
  addClause({-x.at(n - 1), -s.at(n - 2).at(k - 1)});
}

void SATModel::printLiteral(std::ostream& os, Literal lit,
                            bool print_name) const {
  auto index = lit.getIndex();
  if (index < 0) {
    os << "-";
  }
  auto var_index = std::abs(index);
  auto name = variables.at(var_index).name;
  if (!print_name || name.empty()) {
    os << var_index;
  } else {
    os << name;
  }
}

void SATModel::print(std::ostream& os, bool print_names) const {
  for (const auto& clause : clauses) {
    for (const auto& lit : clause.lits) {
      printLiteral(os, lit, print_names);
      os << " ";
    }
    os << "\n";
  }
}

void SATModel::printDIMACS(std::ostream& os) {
  os << "p cnf " << variables.size() - 1 << " " << clauses.size() << "\n";
  for (const auto& clause : clauses) {
    for (const auto& lit : clause.lits) {
      os << lit.getIndex() << " ";
    }
    os << "0\n";
  }
}

}  // namespace bonc::sat_modeller
