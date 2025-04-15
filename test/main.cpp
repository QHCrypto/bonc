#include <anf.h>
#include <frontend_result_parser.h>

#include <fstream>

using Monomial = bonc::ANFMonomial<bonc::ReadTargetAndOffset>;
using Polynomial = bonc::ANFPolynomial<bonc::ReadTargetAndOffset>;

std::unordered_map<bonc::ReadTargetAndOffset, int> read_expr_degs;

std::unordered_map<Monomial, bonc::ReadTargetAndOffset> monomial_better_bound;

int bitExprDegree(bonc::ReadTargetAndOffset rto);

auto numericMappingSubstitute = [](const bonc::ReadTargetAndOffset& rto,
                                   const Monomial& mono) -> Polynomial {
  if (mono.size() < 2) {
    return Polynomial::fromVariable(rto);
  }
  auto [read_target, offset] = rto;
  auto kind = read_target->getKind();
  auto name = read_target->getName();
  if (kind == bonc::ReadTarget::Input) {
    return Polynomial::fromVariable(rto);
  } else {
    auto poly = bonc::bitExprToANF(read_target->update_expressions.at(offset));
    for (auto& monomial : poly) {
      if (monomial.size() > 1) {
        monomial_better_bound.insert_or_assign(monomial, rto);
      }
    }
    return poly;
  }
};

#include <generator>

std::generator<std::vector<Monomial>> monomialPartition(const Monomial& mono) {
  const auto end = mono.end();
  using IterType = decltype(mono.begin());
  auto impl = [&](this auto&& self, IterType i, std::vector<Monomial>& current)
      -> std::generator<std::vector<Monomial>> {
    if (i == end) {
      // 所有元素都已分配，添加当前划分到结果
      co_yield current;
      co_return;
    }

    // 尝试将元素 *i 添加到每个现有块中
    for (size_t j = 0; j < current.size(); j++) {
      auto [it, _] = current[j].variables.insert(*i);
      co_yield std::ranges::elements_of(self(std::ranges::next(i), current));
      current[j].variables.erase(it);  // 回溯
    }

    // 尝试为元素 *i 创建一个新块
    current.push_back(Monomial{.variables = {*i}});
    co_yield std::ranges::elements_of(self(std::ranges::next(i), current));
    current.pop_back();  // 回溯
  };
  std::vector<Monomial> current;
  co_yield std::ranges::elements_of(impl(mono.begin(), current));
}

int monomialDeg(const Monomial& monomial) {
  int result = 0;
  static bool test = true;
  if (monomial.size() == 3 && test) {
    test = false;
    for (auto partition : monomialPartition(monomial)) {
      std::cout << "{ ";
      for (const auto& mono : partition) {
        std::cout << "{ ";
        for (auto elem : mono.variables) {
          elem.print(std::cout);
        }
        std::cout << "} ";
      }
      std::cout << "}\n";
    }
  }
  for (auto rto : monomial) {
    result += bitExprDegree(rto.data);
  }
  return result;
}

int numericMapping(const Polynomial& poly) {
  int poly_deg = 0;
  for (auto& monomial : poly) {
    poly_deg = std::max(poly_deg, monomialDeg(monomial));
  }
  return poly_deg;
};

int bitExprDegree(bonc::ReadTargetAndOffset rto) {
  auto [read_target, offset] = rto;
  auto kind = read_target->getKind();
  auto name = read_target->getName();
  if (kind == bonc::ReadTarget::Input) {
    if (name == "iv") {
      return 1;
    } else {
      return 0;
    }
  } else {
    auto it = read_expr_degs.find(rto);
    if (it != read_expr_degs.end()) {
      return it->second;
    } else {
      auto anf = bonc::bitExprToANF(read_target->update_expressions.at(offset));
      auto result = numericMapping(
          bonc::expandANF(anf.translate(numericMappingSubstitute)));
      read_expr_degs[rto] = result;
      return result;
    }
  }
}

int main() {
  std::ifstream ifs("bonc.json");
  bonc::FrontendResultParser parser(ifs);

  std::vector<Polynomial> output_polys;
  for (auto& info : parser.parseAll()) {
    std::cout << "Output: " << info.name << ", Size: " << info.size << "\n";
    for (auto& expr : info.expressions) {
      output_polys.push_back(bitExprToANF(expr));
    }
  }
  auto one_poly = output_polys.at(31);

  auto one_poly_expanded =
      bonc::expandANF(one_poly.translate(numericMappingSubstitute));
  std::cout << numericMapping(one_poly_expanded) << '\n';
  std::cout << monomial_better_bound.size() << '\n';
}