#include <anf.h>
#include <frontend_result_parser.h>

#include <fstream>

struct Defer {
  std::function<void()> func;
  Defer(std::function<void()> func) : func{std::move(func)} {}
  Defer(const Defer&) = delete;
  Defer(Defer&& other) {
    func = std::move(other.func);
    other.func = nullptr;
  };
  Defer& operator=(const Defer&) = delete;
  Defer& operator=(Defer&& other) {
    if (this != &other) {
      func = std::move(other.func);
      other.func = nullptr;
    }
    return *this;
  }
  ~Defer() {
    if (func) {
      func();
    }
  }
};
struct DeferHelper {
  template <typename F>
  Defer operator+(F&& f) {
    return Defer{std::forward<F>(f)};
  }
};
#define defer auto _defer_helper##__LINE__ = DeferHelper() + [&]()

using Monomial = bonc::ANFMonomial<bonc::ReadTargetAndOffset>;
using Polynomial = bonc::ANFPolynomial<bonc::ReadTargetAndOffset>;

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

static std::vector<std::string> debugLinePrefix{""s};
[[gnu::always_inline]] inline auto pushDebugLinePrefix(const std::string& str = "| ") {
  debugLinePrefix.push_back(debugLinePrefix.back() + str);
  return Defer{[&] { debugLinePrefix.pop_back(); }};
}

[[gnu::always_inline]] inline std::ostream& printDebugNewline() {
  return std::cout << '\n' << debugLinePrefix.back();
}

int monomialDegree(const Monomial& monomial) {
  auto debug_scope = pushDebugLinePrefix();
  bool apply_optimization = ENABLE_MONOMIAL_OPTIMIZATION && monomial.size() > 1
                         && monomial.size() <= 6;
  printDebugNewline() << "BEGIN EMN ";
  monomial.print(std::cout);
  if (apply_optimization) {
    int result = std::numeric_limits<int>::max();
    printDebugNewline() << "Finding optimization";
    {
      auto debug_scope = pushDebugLinePrefix();
      for (auto partition : monomialPartition(monomial)) {
        printDebugNewline() << "BEGIN OPT { ";
        for (const auto& mono : partition) {
          mono.print(std::cout);
          std::cout << ", ";
        }
        std::cout << "}";
        int deg = 0;
        for (const auto& mono : partition) {
          if (mono.size() == 1) {
            deg += variableDegree(mono.variables.begin()->data);
            continue;
          }
          auto it = monomial_better_bound.find(mono);
          if (it == monomial_better_bound.end()) {
            printDebugNewline() << "END   OPT { ";
            for (const auto& mono : partition) {
              mono.print(std::cout);
              std::cout << ", ";
            }
            std::cout << "} (";
            mono.print(std::cout);
            std::cout << ") not found";
            goto next_partition;
          }
          if (suppressed_read.contains(it->second)) {
            printDebugNewline() << "END   OPT { ";
            for (const auto& mono : partition) {
              mono.print(std::cout);
              std::cout << ", ";
            }
            std::cout << "} (";
            mono.print(std::cout);
            std::cout << ") suppressed";
            goto next_partition;
          }
          deg += variableDegree(it->second);
        }
        printDebugNewline() << "END   OPT { ";
        for (const auto& mono : partition) {
          mono.print(std::cout);
          std::cout << ", ";
        }
        std::cout << "} deg: " << deg;
        result = std::min(result, deg);
      next_partition:;
      }
    }
    printDebugNewline() << "END   EMN ";
    monomial.print(std::cout);
    std::cout << " as " << result;
    return result;
  } else {
    int result = 0;
    for (auto rto : monomial) {
      int varDeg = variableDegree(rto.data);
      result += varDeg;
    }
    printDebugNewline() << "END   EMN ";
    monomial.print(std::cout);
    std::cout << " as " << result;
    return result;
  }
}

int numericMapping(const Polynomial& poly) {
  auto debug_scope = pushDebugLinePrefix();
  printDebugNewline() << "BEGIN EPL ";
  poly.print(std::cout);
  int poly_deg = poly.constant ? 0 : std::numeric_limits<int>::min();
  for (auto& monomial : poly) {
    poly_deg = std::max(poly_deg, monomialDegree(monomial));
  }
  printDebugNewline() << "END   EPL ";
  poly.print(std::cout);
  std::cout << " as " << poly_deg;
  return poly_deg;
};

int variableDegree(bonc::ReadTargetAndOffset rto) {
  auto debug_scope = pushDebugLinePrefix();
  auto [read_target, offset] = rto;
  auto [it, suc] = suppressed_read.insert(rto);
  defer {
    if (suc) {
      suppressed_read.erase(it);
    }
  };
  printDebugNewline() << "BEGIN EVR ";
  rto.print(std::cout);

  auto kind = read_target->getKind();
  auto name = read_target->getName();
  if (kind == bonc::ReadTarget::Input) {
    printDebugNewline() << "END   EVR ";
    rto.print(std::cout);
    std::cout << " as input";
    if (name == "iv") {
      return 1;
    } else {
      return 0;
    }
  } else {
    auto it = read_expr_degs.find(rto);
    if (it != read_expr_degs.end()) {
      printDebugNewline() << "END   EVR ";
      rto.print(std::cout);
      std::cout << " as previously founded " << it->second;
      return it->second;
    } else {
      auto anf = readState(rto);
      anf = expandANF(anf.translate(numericMappingSubstitute));
      printDebugNewline() << "expand ANF to ";
      anf.print(std::cout);
      auto result = numericMapping(anf);
      read_expr_degs[rto] = result;
      printDebugNewline() << "END   EVR ";
      rto.print(std::cout);
      std::cout << " as " << result;
      return result;
    }
  }
}

int main() {
  suppressed_read.reserve(1024);
  std::ifstream ifs("bonc.json");
  bonc::FrontendResultParser parser(ifs);

  std::vector<Polynomial> output_polys;
  for (auto& info : parser.parseAll()) {
    std::cout << "Output: " << info.name << ", Size: " << info.size << "\n";
    for (auto& expr : info.expressions) {
      output_polys.push_back(bitExprToANF(expr));
    }
  }
  for (int i = 1151; i >= 1152 - 1000 - 2; i--) {
    auto one_poly = output_polys.at(i);

    one_poly = bonc::expandANF(one_poly.translate(numericMappingSubstitute));
    // one_poly = bonc::expandANF(one_poly.translate(numericMappingSubstitute));
    std::cerr << (1152 - i - 2) << ':' << numericMapping(one_poly) << '\n';
  }
  // int i = 921;
  // auto one_poly = output_polys.at(i);
  // std::cerr << (1152 - i - 2) << ':' << numericMapping(one_poly) << '\n';
}