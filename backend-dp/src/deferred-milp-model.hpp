#pragma once

#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace bonc {
class UnmodelledValue {
public:
  enum Type {
    Unspecified,
    True,
    False,
  };
  Type type{Unspecified};
};

class ModelVar {
public:
  std::string name;
};

class DeferredModelVar {
private:
  const ModelVar* var;

public:
  DeferredModelVar(const ModelVar* var) : var{var} {}

  const ModelVar* getVar() const {
    return var;
  }
  void setVar(const ModelVar* newVar) {
    var = newVar;
  }
};

using ModelledValue = const ModelVar*;
using DeferredModelledValue = DeferredModelVar*;
using ConstDeferredModelledValue = const DeferredModelVar*;

template <typename T>
struct LinearExprItem {
  T var;
  double coefficient{1.0};

  LinearExprItem() = default;
  LinearExprItem(T var, double coeff = 1.0) : var{var}, coefficient{coeff} {}
};

template <typename T>
class LinearConstraint;

template <typename T>
class LinearExpr {
private:
  std::vector<LinearExprItem<T>> items;
  double constant{0.0};

public:
  LinearExpr() = default;
  LinearExpr(double constant) : constant{constant} {}
  LinearExpr(LinearExprItem<T> item) : items{item} {}
  LinearExpr(std::vector<LinearExprItem<T>> items, double constant = 0.0)
      : items{std::move(items)}, constant{constant} {}

  LinearExpr operator+(LinearExprItem<T> item) {
    return *this + LinearExpr(std::move(item));
  }
  LinearExpr operator-(LinearExprItem<T> item) {
    return *this - LinearExpr(std::move(item));
  }

  friend LinearExpr operator+(LinearExpr lhs, LinearExpr rhs) {
    lhs.items.insert(lhs.items.end(), rhs.items.begin(), rhs.items.end());
    lhs.constant += rhs.constant;
    return lhs;
  }
  friend LinearExpr operator-(LinearExpr lhs, LinearExpr rhs) {
    for (auto& item : rhs.items) {
      item.coefficient = -item.coefficient;
    }
    lhs.items.insert(lhs.items.end(), rhs.items.begin(), rhs.items.end());
    lhs.constant -= rhs.constant;
    return lhs;
  }

  LinearConstraint<T> operator==(double rhs);
  LinearConstraint<T> operator<=(double rhs);
  LinearConstraint<T> operator>=(double rhs);

  auto&& getItems(this auto&& self) {
    return self.items;
  }
  double getConstant() const {
    return constant;
  }
};

enum class Comparator {
  LessEqual,
  GreaterEqual,
  Equal,
};

template <typename T>
struct LinearConstraint {
  LinearExpr<T> lhs;
  Comparator comparator;
  double rhs;
};

template <typename T>
struct Objective {
  LinearExpr<T> expr;
  bool maximize{};
};

template <typename T>
LinearConstraint<T> LinearExpr<T>::operator==(double rhs) {
  return LinearConstraint<T>{*this, Comparator::Equal, rhs};
}
template <typename T>
LinearConstraint<T> LinearExpr<T>::operator<=(double rhs) {
  return LinearConstraint<T>{*this, Comparator::LessEqual, rhs};
}
template <typename T>
LinearConstraint<T> LinearExpr<T>::operator>=(double rhs) {
  return LinearConstraint<T>{*this, Comparator::GreaterEqual, rhs};
}

struct MaterializedResult {
  std::unordered_map<const ModelVar*, std::string> variableNames;
  std::string lpContent;
};

class DeferredMILPModel {
private:
  std::vector<std::unique_ptr<ModelVar>> variables;
  std::vector<std::unique_ptr<DeferredModelVar>> deferred_values;
  std::vector<LinearConstraint<ModelledValue>> constraints;
  std::vector<LinearConstraint<DeferredModelledValue>> deferred_constraints;

  const bool allVariablesBinary{true};

  std::optional<Objective<DeferredModelledValue>> objective;

public:
  ModelledValue createVariable(const std::string& name = "") {
    auto ptr = std::make_unique<ModelVar>(ModelVar{name});
    auto result = ptr.get();
    variables.push_back(std::move(ptr));
    return result;
  }
  DeferredModelledValue createDeferredVariable(const std::string& name = "") {
    return this->createDeferredVariable(this->createVariable(name));
  }
  DeferredModelledValue createDeferredVariable(ModelledValue value) {
    auto deferred = std::make_unique<DeferredModelVar>(value);
    auto result = deferred.get();
    deferred_values.push_back(std::move(deferred));
    return result;
  }
  DeferredModelledValue createDeferredConstant(bool value) {
    auto var = this->createVariable();
    if (value) {
      this->addConstraint(LinearExpr<ModelledValue>{var} == 1);
    } else {
      this->addConstraint(LinearExpr<ModelledValue>{var} == 0);
    }
    return this->createDeferredVariable(var);
  }

  void addConstraint(LinearConstraint<ModelledValue> constr) {
    constraints.emplace_back(std::move(constr));
  }
  void addConstraint(LinearConstraint<DeferredModelledValue> constr) {
    deferred_constraints.emplace_back(std::move(constr));
  }

  void setObjective(LinearExpr<DeferredModelledValue> obj,
                    bool maximize = false) {
    objective = {std::move(obj), maximize};
  }

  MaterializedResult gurobiLpFormat() const {
    using namespace std::literals;
    std::unordered_map<const ModelVar*, std::string> var_names;
    for (auto i = 0uz; i < variables.size(); ++i) {
      var_names[variables[i].get()] = "x"s + std::to_string(i);
    }
    auto printVar = [&](const auto& var) -> std::string {
      using T = std::remove_cvref_t<decltype(var)>;
      if constexpr (std::is_same_v<T, ModelledValue>) {
        return var_names.at(var);
      } else if constexpr (std::is_same_v<T, DeferredModelledValue>) {
        return var_names.at(var->getVar());
      } else {
        static_assert(sizeof(T) == 0, "Unsupported var type");
        return "";
      }
    };
    auto printLin = [&](auto&& expr) {
      std::string result;
      for (auto& [var, coeff] : expr.getItems()) {
        result += coeff >= 0 ? " + " : " - ";
        result += std::format("{} {}", std::abs(coeff), printVar(var));
      }
      return result;
    };
    std::string lp;
    if (objective.has_value()) {
      auto& [expr, maximize] = *objective;
      lp += (maximize ? "Maximize\n" : "Minimize\n");
      lp += printLin(expr) + "\n";
    }
    lp += "Subject To\n";
    for (auto& [lhs, comparator, rhs] : constraints) {
      lp += printLin(lhs) + " ";
      switch (comparator) {
        case Comparator::Equal: lp += "= "; break;
        case Comparator::LessEqual: lp += "<= "; break;
        case Comparator::GreaterEqual: lp += ">= "; break;
      }
      lp += std::to_string(rhs - lhs.getConstant()) + "\n";
    }
    for (auto& [lhs, comparator, rhs] : deferred_constraints) {
      lp += printLin(lhs) + " ";
      switch (comparator) {
        case Comparator::Equal: lp += "= "; break;
        case Comparator::LessEqual: lp += "<= "; break;
        case Comparator::GreaterEqual: lp += ">= "; break;
      }
      lp += std::to_string(rhs - lhs.getConstant()) + "\n";
    }
    if (allVariablesBinary) {
      lp += "Binary\n";
      for (auto& [_, name] : var_names) {
        lp += name + "\n";
      }
    }
    return {.variableNames = std::move(var_names), .lpContent = std::move(lp)};
  }
};

}  // namespace bonc