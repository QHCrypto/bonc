#pragma once

#include <boost/container_hash/hash.hpp>
#include <cassert>
#include <span>
#include <vector>

class PolyhedronVertex {
  std::vector<int> coordinates;

public:
  template <std::ranges::range Range>
    requires std::convertible_to<std::ranges::range_value_t<Range>, int>
  PolyhedronVertex(Range&& coords)
      : coordinates{std::ranges::begin(coords), std::ranges::end(coords)} {}

  PolyhedronVertex(std::initializer_list<int> coords)
      : coordinates{coords} {}

  friend bool operator==(const PolyhedronVertex& lhs,
                         const PolyhedronVertex& rhs) = default;

  static PolyhedronVertex fromIntBits(std::uint64_t value,
                                      std::size_t bit_count) {
    assert(bit_count <= 64 && "bit_count must be <= 64");
    std::vector<int> bits(bit_count, 0);
    for (auto i = 0uz; i < bit_count; i++) {
      bits.at(i) = static_cast<int>((value >> i) & 0x1u);
    }
    return PolyhedronVertex{bits};
  }

  auto begin(this auto&& self) {
    return self.coordinates.begin();
  }
  auto&& at(this auto&& self, std::size_t index) {
    return self.coordinates.at(index);
  }
  auto end(this auto&& self) {
    return self.coordinates.end();
  }
  std::size_t dimension() const {
    return coordinates.size();
  }
};

template <>
class std::hash<PolyhedronVertex> {
public:
  std::size_t operator()(const PolyhedronVertex& vertex) const {
    std::size_t seed = 0;
    for (const auto& coord : vertex) {
      boost::hash_combine(seed, coord);
    }
    return seed;
  }
};

/**
 * @brief Represent a Hyperplane Inequality: `c0*x0 + c1*x1 + ... + cn*xn + ct
 * >= 0`
 *
 * - coefficients: [c0, c1, ..., cn]
 *
 * - constant_term: ct
 */
struct PolyhedronInequality {
  std::vector<int> coefficients;
  int constant_term;

  std::size_t dimension() const {
    return coefficients.size();
  }
};

std::vector<PolyhedronInequality> vToH(
    std::span<const PolyhedronVertex> vertices);
