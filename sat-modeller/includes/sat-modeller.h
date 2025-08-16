#pragma once

#include <cassert>
#include <cmath>
#include <format>
#include <string>
#include <unordered_set>
#include <vector>
#include <iostream>

namespace bonc::sat_modeller {

struct VariableDetail {
  std::string name;
};

class Literal;

class Variable {
private:
  std::size_t index;

public:
  explicit Variable(std::size_t index) : index(index) {}
  friend bool operator==(const Variable& lhs, const Variable& rhs) = default;

  Variable(const Variable&) = default;
  Variable(Variable&&) = default;
  Variable& operator=(const Variable&) = default;
  Variable& operator=(Variable&&) = default;

  std::size_t getIndex() const {
    return index;
  }

  Literal operator-() const;
};

class Literal {
public:
  using ValueT = decltype(0z);

private:
  ValueT index;

public:
  explicit Literal(ValueT index) : index(index) {}
  Literal(Variable var) : index(var.getIndex()) {}
  friend bool operator==(const Literal& lhs, const Literal& rhs) = default;

  Literal(const Literal&) = default;
  Literal(Literal&&) = default;
  Literal& operator=(const Literal&) = default;
  Literal& operator=(Literal&&) = default;

  ValueT getIndex() const {
    return index;
  }
};

inline Literal Variable::operator-() const {
  return Literal(-Literal::ValueT(index));
}

}

template <>
struct std::hash<bonc::sat_modeller::Variable> {
  std::size_t operator()(const bonc::sat_modeller::Variable& var) const {
    return std::hash<std::size_t>()(var.getIndex());
  }
};

template <>
struct std::hash<bonc::sat_modeller::Literal> {
  std::size_t operator()(const bonc::sat_modeller::Literal& lit) const {
    return std::hash<std::size_t>()(lit.getIndex());
  }
};

namespace bonc::sat_modeller {

using RawTable = std::vector<std::vector<std::uint64_t>>;

class SATModel {
public:
  struct Clause {
    std::vector<Literal> lits;
  };

private:
  std::vector<VariableDetail> variables{{}};
  std::vector<Clause> clauses;

public:
  Variable createVariable(const std::string& name = "");
  std::vector<Variable> createVariables(std::size_t count, const std::string& name_prefix = "");
  void addClause(const std::vector<Literal>& lits);
  std::vector<Variable> addWeightTableClauses(
      const RawTable& table, const std::vector<Variable>& inputs,
      const std::vector<Variable>& outputs,
      const std::vector<std::vector<int>>& weights);
  void addXorClause(const std::vector<Variable>& values, Variable result);
  void addAndClause(const std::vector<Variable>& values, Variable result);
  void addOrClause(const std::vector<Variable>& values, Variable result);
  void addSequentialCounterLessEqualClause(std::vector<Variable> x, int k);
  void printLiteral(std::ostream& os, Literal lit, bool print_name) const;
  void print(std::ostream& os, bool print_names = true) const;
  void printDIMACS(std::ostream& os);
};

}  // namespace bonc::sat_modeller