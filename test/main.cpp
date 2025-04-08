#include <anf.h>
#include <frontend_result_parser.h>

#include <fstream>

std::unordered_map<bonc::ReadBitExpr, int, bonc::ReadBitExpr::Hash>
    read_expr_degs;

int numeric_mapping(
    bonc::ANFPolynomial<std::shared_ptr<bonc::ReadBitExpr>> poly,
    int expand_depth) {
  int poly_deg = 0;
  for (auto& monomial : poly) {
    if (monomial.size() > 1) {
      // TODO, expand
    }
    int monomial_deg = 0;
    for (auto& var : monomial) {
      auto expr = var->get();
      auto read_target = expr->getTarget();
      auto offset = expr->getOffset();
      auto kind = read_target->getKind();
      auto name = read_target->getName();
      int var_deg;
      if (kind == bonc::ReadTarget::Input) {
        if (name == "iv") {
          var_deg = 1;
        } else {
          var_deg = 0;
        }
      } else if (auto it = read_expr_degs.find(*expr);
                 it != read_expr_degs.end()) {
        var_deg = it->second;
      } else {
        auto anf = bonc::bitExprToANF(read_target->update_expressions.at(offset));
        var_deg = numeric_mapping(anf, expand_depth - 1);
        read_expr_degs[*expr] = var_deg;
      }
      monomial_deg += var_deg;
    }
    poly_deg = std::max(poly_deg, monomial_deg);
  }
  return poly_deg;
};

int main() {
  std::ifstream ifs("bonc.json");
  bonc::FrontendResultParser parser(ifs);

  std::vector<bonc::ANFPolynomial<std::shared_ptr<bonc::ReadBitExpr>>>
      output_polys;
  for (auto& info : parser.parseAll()) {
    std::cout << "Output: " << info.name << ", Size: " << info.size << "\n";
    for (auto& expr : info.expressions) {
      output_polys.push_back(bitExprToANF(expr, 2));
    }
  }
  auto one_poly = output_polys.at(0);

  std::cout << numeric_mapping(one_poly, 0);
}