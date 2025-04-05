#pragma once

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
    if constexpr (requires { data.print(os);}) {
      data.print(os);
    } else if constexpr (requires { data->print(os); }) {
      data->print(os);
    } else {
      os << data;
    }
  }

  struct Hash {
    std::size_t operator()(const ANFVariable<T>& var) const {
      return std::hash<T>()(var.data);
    }
  };
};

template <typename T>
class ANFMonomial {
public:
  std::unordered_set<ANFVariable<T>, typename ANFVariable<T>::Hash> variables;

  bool friend operator==(const ANFMonomial<T>& lhs, const ANFMonomial<T>& rhs) {
    return lhs.variables == rhs.variables;
  }

  // See boost::hash_combine
  struct Hash {
    std::size_t operator()(const ANFMonomial<T>& mono) const {
      std::size_t seed = 0;
      for (const auto& var : mono.variables) {
        seed ^= typename ANFVariable<T>::Hash{}(var) + 0x9e3779b9 + (seed << 6)
              + (seed >> 2);
      }
      return seed;
    }
  };

  friend ANFMonomial<T> operator*(const ANFMonomial<T>& lhs,
                                  const ANFMonomial<T>& rhs) {
    ANFMonomial<T> result;
    result.variables = lhs.variables;
    std::copy(rhs.variables.begin(), rhs.variables.end(),
              std::inserter(result.variables, result.variables.end()));
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
};

template <typename T>
class ANFPolynomial {
public:
  std::unordered_set<ANFMonomial<T>, typename ANFMonomial<T>::Hash> monomials;
  bool constant{};

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
};

}  // namespace bonc