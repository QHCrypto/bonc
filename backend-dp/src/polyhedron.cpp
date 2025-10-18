#include "polyhedron.h"

#include <ppl.hh>
#include <ranges>

namespace PPL = Parma_Polyhedra_Library;

PPL::Constraint_System pplGsToCs(const PPL::Generator_System& genRep) {
  PPL::C_Polyhedron poly(genRep);
  return poly.minimized_constraints();
}

PPL::Generator vertexToPPLGenerator(const PolyhedronVertex& vertex) {
  auto dim = vertex.dimension();
  PPL::Linear_Expression expr;
  expr.set_space_dimension(dim);
  for (auto [i, x] : std::views::enumerate(vertex)) {
    expr.set_coefficient(PPL::Variable{static_cast<std::size_t>(i)}, x);
  }
  return PPL::Generator::point(expr);
}

int gmpToInt(const PPL::GMP_Integer& gmpInt) {
  // Assuming the GMP_Integer fits into an int
  assert(gmpInt.fits_sint_p() && "GMP_Integer does not fit into int");
  return static_cast<int>(gmpInt.get_si());
}

std::vector<PolyhedronInequality> vToH(
    std::span<const PolyhedronVertex> vertices) {
  PPL::Generator_System genSys;
  for (const auto& vertex : vertices) {
    genSys.insert(vertexToPPLGenerator(vertex));
  }
  PPL::Constraint_System consSys = pplGsToCs(genSys);
  std::vector<PolyhedronInequality> inequalities;
  for (const auto& cons : consSys) {
    PolyhedronInequality ineq;
    auto dim = cons.space_dimension();
    ineq.coefficients.resize(dim);
    for (auto i = 0uz; i < dim; ++i) {
      ineq.coefficients.at(i) = gmpToInt(cons.coefficient(PPL::Variable{i}));
    }
    ineq.constant_term = gmpToInt(cons.inhomogeneous_term());
    inequalities.push_back(std::move(ineq));
  }
  return inequalities;
}
