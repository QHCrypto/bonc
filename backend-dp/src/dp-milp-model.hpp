#include "deferred-milp-model.hpp"

namespace bonc {
namespace dp {

class MILPModel : public DeferredMILPModel {
public:
  MILPModel() : DeferredMILPModel{} {}

  /**
   * Denote (a) → (b0, b1) a division trail of Copy function, the following
   * inequality is sufficient to describe the division propagation of Copy.
   * `a − b0 − b1 = 0`
   * `a, b0 , b1 are binaries`
   * @ref http://doi.org/10.1007/978-3-662-53887-6_24
   */
  DeferredModelledValue copy(DeferredModelledValue from) {
    // std::abort();
    auto a = from->getVar();
    auto b0 = this->createVariable();
    auto b1 = this->createVariable();
    this->addConstraint(LinearExpr<ModelledValue>{a} - b0 - b1 == 0);
    from->setVar(b0);
    auto b1_deferred = this->createDeferredVariable(b1);
    return b1_deferred;
  }

  DeferredModelledValue xor_(DeferredModelledValue a0,
                             DeferredModelledValue a1) {
    auto b = this->createDeferredVariable();
    this->addConstraint(LinearExpr<DeferredModelledValue>{} + a0 + a1 - b == 0);
    return b;
  }

  DeferredModelledValue and_(DeferredModelledValue a0,
                             DeferredModelledValue a1) {
    auto b = this->createDeferredVariable();
    auto b_expr = LinearExpr<DeferredModelledValue>{b};
    this->addConstraint(b_expr - a0 >= 0);
    this->addConstraint(b_expr - a1 >= 0);
    this->addConstraint(b_expr - a0 - a1 <= 0);
    return b;
  }
};

}  // namespace dp
}  // namespace bonc