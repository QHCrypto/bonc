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
};

enum class Comparator {
  LessEqual,
  GreaterEqual,
  Equal,
};

template <typename T>
class LinearConstraint {
private:
  LinearExpr<T> lhs;
  Comparator comparator;
  double rhs;

public:
  LinearConstraint(LinearExpr<T> lhs, Comparator comparator, double rhs)
      : lhs{std::move(lhs)}, comparator{comparator}, rhs{rhs} {}
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

class DeferredMILPModel {
private:
  std::vector<std::unique_ptr<ModelVar>> variables;
  std::vector<std::unique_ptr<DeferredModelVar>> deferred_values;
  std::vector<LinearConstraint<ModelledValue>> constraints;
  std::vector<LinearConstraint<DeferredModelledValue>> deferred_constraints;

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

  void addConstraint(LinearConstraint<ModelledValue> constr) {
    constraints.emplace_back(std::move(constr));
  }
  void addConstraint(LinearConstraint<DeferredModelledValue> constr) {
    deferred_constraints.emplace_back(std::move(constr));
  }
};

}