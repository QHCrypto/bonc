#include "lib.h"

#include <generator>

std::unordered_map<bonc::ReadTargetAndOffset, int> read_expr_degs;

std::unordered_map<Monomial, bonc::ReadTargetAndOffset> monomial_better_bound;
std::unordered_set<bonc::ReadTargetAndOffset> suppressed_read;

// read ANFPolynomial from a ReadTargetAndOffset.
// Record that this RTO might have a better degree bound than its monomial.
Polynomial readState(bonc::ReadTargetAndOffset rto) {
  auto poly = bitExprToANF(rto.target->update_expressions.at(rto.offset));
  for (auto& monomial : poly) {
    if (monomial.size() > 1) {
      monomial_better_bound.insert_or_assign(monomial, rto);
    }
  }
  return poly;
}

int variableDegree(bonc::ReadTargetAndOffset rto);

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
    return readState(rto);
  }
};

#include <generator>

std::generator<std::vector<Monomial>> monomialPartition(const Monomial& mono) {
  const auto end = mono.end();
  using IterType = decltype(mono.begin());
  auto impl = [&](this auto&& self, IterType i, std::vector<Monomial>& current)
      -> std::generator<std::vector<Monomial>> {
    if (i == end) {
      co_yield current;
      co_return;
    }
    // 将此变量作为一个独立的划分……
    current.push_back(Monomial{.variables = {*i}});
    co_yield std::ranges::elements_of(self(std::ranges::next(i), current));
    current.pop_back();

    // ……或者添加到之前的划分
    for (auto& part : current) {
      auto [it, _] = part.variables.insert(*i);
      co_yield std::ranges::elements_of(self(std::ranges::next(i), current));
      part.variables.erase(it);
    }
  };
  std::vector<Monomial> current;
  // 保证 current 迭代器不失效
  current.reserve(mono.size());
  co_yield std::ranges::elements_of(impl(mono.begin(), current));
}

constexpr const bool ENABLE_MONOMIAL_OPTIMIZATION = true;

using namespace std::literals;

std::unordered_map<Monomial, int> monomial_degrees;

int monomialDegree(const Monomial& monomial) {
  if (auto it = monomial_degrees.find(monomial); it != monomial_degrees.end()) {
    return it->second;
  }
  bool apply_optimization = ENABLE_MONOMIAL_OPTIMIZATION && monomial.size() > 1
                         && monomial.size() <= 6;
  if (apply_optimization) {
    int result = std::numeric_limits<int>::max();
    for (auto partition : monomialPartition(monomial)) {
      int deg = 0;
      for (const auto& mono : partition) {
        if (mono.size() == 1) {
          deg += variableDegree(mono.variables.begin()->data);
          continue;
        }
        auto it = monomial_better_bound.find(mono);
        if (it == monomial_better_bound.end()) {
          goto next_partition;
        }
        if (suppressed_read.contains(it->second)) {
          goto next_partition;
        }
        deg += variableDegree(it->second);
      }
      result = std::min(result, deg);
    next_partition:;
    }
    return result;
  } else {
    int result = 0;
    for (auto rto : monomial) {
      int varDeg = variableDegree(rto.data);
      result += varDeg;
    }
    monomial_degrees[monomial] = result;
    return result;
  }
}

std::unordered_map<Polynomial, int> polynomial_degrees;

int numericMapping(const Polynomial& poly) {
  if (auto it = polynomial_degrees.find(poly); it != polynomial_degrees.end()) {
    return it->second;
  }
  int poly_deg = poly.constant ? 0 : std::numeric_limits<int>::min();
  for (auto& monomial : poly) {
    poly_deg = std::max(poly_deg, monomialDegree(monomial));
  }
  polynomial_degrees[poly] = poly_deg;
  return poly_deg;
};

int variableDegree(bonc::ReadTargetAndOffset rto) {
  auto [read_target, offset] = rto;
  auto [it, suc] = suppressed_read.insert(rto);
  defer {
    if (suc) {
      suppressed_read.erase(it);
    }
  };

  auto kind = read_target->getKind();
  auto name = read_target->getName();
  if (kind == bonc::ReadTarget::Input) {
    if (name == "iv" || name == "plaintext") {
      return 1;
    } else {
      return 0;
    }
  } else {
    auto it = read_expr_degs.find(rto);
    if (it != read_expr_degs.end()) {
      return it->second;
    } else {
      auto anf = readState(rto);
      anf = expandANF(anf.translate(numericMappingSubstitute));
      auto result = numericMapping(anf);
      read_expr_degs[rto] = result;
      return result;
    }
  }
}