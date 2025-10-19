#pragma once

#include "dp-milp-model.hpp"
#include "utils.hpp"

#include <utility>

namespace bonc {

namespace dp {

class TraverseResult {
public:
  using UnmodelledValueType = UnmodelledValue::Type;
  using ModelledValueType = ConstDeferredModelledValue;
  using ValueType = std::variant<UnmodelledValue, DeferredModelledValue>;

private:
  ValueType value;

  TraverseResult(UnmodelledValue::Type type) : value{UnmodelledValue{type}} {
  }

  TraverseResult(DeferredModelledValue var) : value{std::move(var)} {}

public:
  static TraverseResult makeUnmodelled(UnmodelledValue::Type type) {
    return TraverseResult{type};
  }
  static TraverseResult makeModelled(ConstDeferredModelledValue modelled, MILPModel& model) {
    auto mutable_value = model.createDeferredVariable(modelled->getVar());
    return TraverseResult{std::move(mutable_value)};
  }

  // TraverseResult(ValueType value) : value{std::move(value)} {}

  // TraverseResult(const TraverseResult&) = default;
  // TraverseResult(TraverseResult&&) = default;
  // TraverseResult& operator=(const TraverseResult&) = default;
  // TraverseResult& operator=(TraverseResult&&) = default;

  TraverseResult& reuse(MILPModel& model) {
    if (auto modelled = std::get_if<DeferredModelledValue>(&value)) {
      auto copied_value = model.copy(*modelled);
      this->value = copied_value;
    }
    return *this;
  }
  auto variant() const {
    return this->value;
  }
  auto visit(auto&& fn) {
    return std::visit(std::forward<decltype(fn)>(fn), this->variant());
  }
  bool modelled() const {
    return std::holds_alternative<DeferredModelledValue>(this->value);
  }
};

}
}