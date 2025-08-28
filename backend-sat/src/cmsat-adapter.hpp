#pragma once

#ifndef USE_CRYPTOMINISAT5
#error "cryptominisat5 is not enabled"
#endif

#include <cryptominisat5/cryptominisat.h>
#include <sat-modeller.h>

namespace bonc {

enum class SolvedModelValue {
  Undefined = CMSat::l_Undef.getValue(),
  True = CMSat::l_True.getValue(),
  False = CMSat::l_False.getValue(),
};

inline std::ostream& operator<<(std::ostream& os, SolvedModelValue val) {
  if (val == SolvedModelValue::True) {
    os << "1";
  } else if (val == SolvedModelValue::False) {
    os << "0";
  } else {
    os << "x";
  }
  return os;
}

std::optional<std::vector<SolvedModelValue>> solve(
    const bonc::sat_modeller::SATModel& model) {
  CMSat::SATSolver solver;
  solver.new_vars(model.variableSize());
  for (auto& clause : model.getClauses()) {
    std::vector<CMSat::Lit> cmsat_clause;
    for (auto& lit : clause.lits) {
      cmsat_clause.push_back(CMSat::Lit{
          static_cast<uint32_t>(std::abs(lit.getIndex())), lit.negative()});
    }
    solver.add_clause(cmsat_clause);
  }
  auto ret = solver.solve();
  if (ret == CMSat::l_True) {
    std::cout << "SATISFIABLE" << std::endl;
    return solver.get_model() | std::views::transform([](auto lit) {
             return static_cast<SolvedModelValue>(lit.getValue());
           })
         | std::ranges::to<std::vector>();
  } else {
    std::cout << "UNSATISFIABLE" << std::endl;
    return std::nullopt;
  }
}

}  // namespace bonc