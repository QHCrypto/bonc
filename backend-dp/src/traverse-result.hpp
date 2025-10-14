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
  using ValueType = std::variant<UnmodelledValue, ConstDeferredModelledValue, DeferredModelledValue>;

private:
  ValueType value;

public:
  /** unmodelled */
  TraverseResult(UnmodelledValue::Type type = UnmodelledValue::Unspecified) : value{UnmodelledValue{type}} {}

  TraverseResult(ConstDeferredModelledValue var) : value{std::move(var)} {}

  // TraverseResult(ValueType value) : value{std::move(value)} {}

  // TraverseResult(const TraverseResult&) = default;
  // TraverseResult(TraverseResult&&) = default;
  // TraverseResult& operator=(const TraverseResult&) = default;
  // TraverseResult& operator=(TraverseResult&&) = default;

  TraverseResult& reuse(MILPModel& model) {
    if (auto modelled = std::get_if<ConstDeferredModelledValue>(&value)) {
      auto mutable_value = model.createDeferredVariable((*modelled)->getVar());
      this->value = mutable_value;
    } else if (auto modelled = std::get_if<DeferredModelledValue>(&value)) {
      auto copied_value = model.copy(*modelled);
      this->value = copied_value;
    }
    return *this;
  }
  auto variant() const {
    return assert_into<UnmodelledValue, DeferredModelledValue>(this->value);
  }
  auto visit(auto&& fn) {
    return std::visit(std::forward<decltype(fn)>(fn), this->variant());
  }
};

}
}