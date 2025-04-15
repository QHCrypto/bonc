#pragma once

#include <boost/functional/hash.hpp>
#include <ostream>
#include <unordered_set>

namespace bonc {

template <typename T>
class ANFVariable {
public:
  T data;

  const T* operator->() const {
    return &data;
  }
  T* operator->() {
    return &data;
  }

  bool friend operator==(const ANFVariable<T>& lhs,
                         const ANFVariable<T>& rhs) = default;

  void print(std::ostream& os) const {
    if constexpr (requires { data.print(os); }) {
      data.print(os);
    } else if constexpr (requires { data->print(os); }) {
      data->print(os);
    } else {
      os << data;
    }
  }

  friend std::size_t hash_value(const ANFVariable<T>& var) {
    return std::hash<T>()(var.data);
  }
};

template <typename T>
class ANFMonomial {
public:
  std::unordered_set<ANFVariable<T>> variables;

  bool friend operator==(const ANFMonomial<T>& lhs, const ANFMonomial<T>& rhs) = default;

  friend ANFMonomial<T> operator*(const ANFMonomial<T>& lhs,
                                  const ANFMonomial<T>& rhs) {
    ANFMonomial<T> result;
    result.variables = lhs.variables;
    std::copy(rhs.variables.begin(), rhs.variables.end(),
              std::inserter(result.variables, result.variables.end()));
    return result;
  }

  template <typename U, std::invocable<const T&, const ANFMonomial<T>&> F>
  ANFMonomial<U> translate(F&& f) const {
    ANFMonomial<U> result;
    for (const auto& var : variables) {
      result.variables.insert(ANFVariable<U>{f(var.data, *this)});
    }
    return result;
  }

  void print(std::ostream& os) const {
    for (auto it = variables.begin(); it != variables.end(); ++it) {
      if (it != variables.begin()) {
        os << "*";
      }
      it->print(os);
    }
  }

  auto begin() const {
    return variables.begin();
  }
  auto end() const {
    return variables.end();
  }

  std::size_t size() const {
    return variables.size();
  }

  friend std::size_t hash_value(const ANFMonomial<T>& mono) {
    std::size_t seed = 0;
    for (const auto& var : mono.variables) {
      boost::hash_combine(seed, var.data);
    }
    return seed;
  }
};

template <typename T>
class ANFPolynomial {
public:
  std::unordered_set<ANFMonomial<T>> monomials;
  bool constant{};

  bool friend operator==(const ANFPolynomial<T>& lhs, const ANFPolynomial<T>& rhs) = default;

  explicit ANFPolynomial(bool constant = false) : constant(constant) {}

  static ANFPolynomial<T> fromMonomial(const ANFMonomial<T>& monomial) {
    ANFPolynomial<T> polynomial;
    polynomial.monomials.insert(monomial);
    return polynomial;
  }
  static ANFPolynomial<T> fromVariable(const T& variable) {
    ANFPolynomial<T> polynomial;
    polynomial.monomials.insert({.variables = {ANFVariable{variable}}});
    return polynomial;
  }
  static ANFPolynomial<T> fromConstant(bool constant) {
    return ANFPolynomial<T>(constant);
  }

  void addMonomial(const ANFMonomial<T>& monomial) {
    if (monomials.find(monomial) == monomials.end()) {
      monomials.insert(monomial);
    } else {
      monomials.erase(monomial);
    }
  }

  template <std::invocable<const T&, const ANFMonomial<T>&> F>
  auto translate(F&& f) const {
    using U = decltype(f(std::declval<T>(), std::declval<ANFMonomial<T>>()));
    ANFPolynomial<U> result;
    result.constant = constant;
    for (const auto& mono : monomials) {
      result.monomials.insert(mono.template translate<U>(f));
    }
    return result;
  }

  void print(std::ostream& os) const {
    if (constant) {
      os << "1";
    }
    for (auto it = monomials.begin(); it != monomials.end(); ++it) {
      if (it != monomials.begin() || constant) {
        os << " + ";
      }
      it->print(os);
    }
  }

  auto begin() const {
    return monomials.begin();
  }
  auto end() const {
    return monomials.end();
  }

  friend ANFPolynomial<T> operator+(const ANFPolynomial<T>& lhs,
                                    const ANFPolynomial<T>& rhs) {
    ANFPolynomial<T> result = lhs;

    // Toggle constant if the other polynomial has constant 1
    result.constant ^= rhs.constant;

    // Toggle monomials (add or remove based on existence)
    for (const auto& mono : rhs.monomials) {
      result.addMonomial(mono);
    }

    return result;
  }

  friend ANFPolynomial<T> operator*(const ANFPolynomial<T>& lhs,
                                    const ANFPolynomial<T>& rhs) {
    ANFPolynomial<T> result;

    // If either polynomial has constant 1, include the other's monomials
    if (lhs.constant) {
      // Copy all monomials from rhs
      result.monomials = rhs.monomials;
      // Set constant based on rhs's constant
      result.constant = rhs.constant;
    }

    if (rhs.constant) {
      // Add all monomials from lhs
      for (const auto& mono : lhs.monomials) {
        result.addMonomial(mono);
      }
    }

    // Cartesian product of monomials
    for (const auto& lhsMono : lhs.monomials) {
      for (const auto& rhsMono : rhs.monomials) {
        ANFMonomial<T> newMono = lhsMono * rhsMono;
        result.addMonomial(newMono);
      }
    }

    return result;
  }

  ANFPolynomial<T> operator!() const {
    ANFPolynomial<T> result = *this;
    result.constant = !result.constant;
    return result;
  }

  friend std::size_t hash_value(const ANFPolynomial& poly) {
    std::size_t seed = 0;
    for (const auto& var : poly.monomials) {
      boost::hash_combine(seed, var);
    }
    boost::hash_combine(seed, poly.constant);
    return seed;
  }
};

template <typename T>
ANFPolynomial<T> expandANF(const ANFPolynomial<ANFPolynomial<T>>& poly) {
  ANFPolynomial<T> result;
  for (const auto& mono : poly.monomials) {
    ANFPolynomial<T> expandedMono(true);
    for (const auto& varPoly : mono) {
      expandedMono = expandedMono * varPoly.data;
    }
    result = result + expandedMono;
  }
  return result;
}

}  // namespace bonc

template <typename T>
  requires requires(const T& object) {
    { hash_value(object) } -> std::convertible_to<std::size_t>;
  }
struct std::hash<T> {
  std::size_t operator()(const T& var) const {
    return hash_value(var);
  }
};
