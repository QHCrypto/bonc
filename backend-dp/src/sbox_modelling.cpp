#include "sbox_modelling.h"

#include <boost/dynamic_bitset.hpp>
#include <cstdint>
#include <ranges>
#include <stdexcept>
#include <unordered_set>

namespace {

int evaluateInequality(const PolyhedronVertex& point,
                       const PolyhedronInequality& inequality) {
  if (point.dimension() != inequality.dimension()) {
    throw std::invalid_argument("Point dimension and inequality size mismatch");
  }
  int sum = inequality.constant_term;
  for (auto i = 0uz; i < point.dimension(); ++i) {
    sum += point.at(i) * inequality.coefficients.at(i);
  }
  return sum;
}

constexpr bool isPowerOfTwo(std::uint64_t n) {
  return std::popcount(n) == 1;
}

/**
 * @brief Returns $\bm x^\bm u$, or noted as $\pi_\bm u(\bm x)$.
 *
 * @param x
 * @param u
 * @return Single bit
 */
constexpr std::uint64_t bitPower(std::uint64_t x, std::uint64_t u) {
  return (x & u) == u;
}

}  // namespace

std::vector<PolyhedronInequality> reduceInequalities(
    const std::vector<PolyhedronInequality>& inequalities,
    const std::vector<PolyhedronVertex>& points) {
  if (points.empty() || inequalities.empty()) {
    throw std::invalid_argument("Points and inequalities must not be empty");
  }
  const std::size_t dimension = points.front().dimension();
  for (const auto& p : points) {
    if (p.dimension() != dimension) {
      throw std::invalid_argument("All points must share the same dimension");
    }
  }
  for (const PolyhedronInequality& ineq : inequalities) {
    if (ineq.dimension() != dimension) {
      throw std::invalid_argument("Inequality size must equal dimension");
    }
  }

  std::unordered_set<PolyhedronVertex> point_set;
  point_set.reserve(points.size());
  for (const auto& p : points) {
    point_set.insert(p);
  }

  const std::size_t total_points = 1ull << dimension;
  std::vector<PolyhedronVertex> complement;
  complement.reserve(total_points - points.size());
  for (auto idx = 0uz; idx < total_points; idx++) {
    auto candidate = PolyhedronVertex::fromIntBits(idx, dimension);
    if (!point_set.count(candidate)) {
      complement.push_back(std::move(candidate));
    }
  }

  std::vector<PolyhedronInequality> remaining = inequalities;
  std::vector<PolyhedronInequality> result;
  result.reserve(remaining.size());

  while (!complement.empty()) {
    auto best_index = remaining.size();
    std::vector<std::size_t> violates_indices;

    for (std::size_t idx = 0; idx < remaining.size(); ++idx) {
      std::vector<std::size_t> current_violation_indices;
      for (std::size_t pt_idx = 0; pt_idx < complement.size(); ++pt_idx) {
        if (evaluateInequality(complement[pt_idx], remaining[idx]) < 0) {
          current_violation_indices.push_back(pt_idx);
        }
      }
      if (current_violation_indices.size() > violates_indices.size()) {
        best_index = idx;
        violates_indices = std::move(current_violation_indices);
      }
    }

    if (best_index == remaining.size() || violates_indices.empty()) {
      throw std::runtime_error(
          "Failed to reduce inequalities: insufficient separating power");
    }

    PolyhedronInequality chosen = remaining[best_index];
    result.push_back(chosen);

    std::vector<PolyhedronVertex> filtered;
    filtered.reserve(complement.size() - violates_indices.size());
    boost::dynamic_bitset<> to_remove(complement.size());
    for (std::size_t idx : violates_indices) {
      to_remove.set(idx, true);
    }
    for (std::size_t idx = 0; idx < complement.size(); ++idx) {
      if (!to_remove.test(idx)) {
        filtered.push_back(std::move(complement[idx]));
      }
    }
    complement = std::move(filtered);
    remaining.erase(remaining.begin()
                    + static_cast<std::ptrdiff_t>(best_index));
  }

  return result;
}

std::vector<PolyhedronVertex> divisionPropertyTrail(
    const bonc::Ref<bonc::LookupTable>& sbox) {
  auto input_width = sbox->getInputWidth();
  auto input_masks_size = 1uz << input_width;
  auto output_width = sbox->getOutputWidth();
  auto output_masks_size = 1uz << output_width;

  auto table_data = sbox->tableData();
  assert(table_data.size() == input_masks_size
         && "Lookup table size does not match input width");

  auto anfs = std::views::iota(0uz, output_masks_size)
            | std::views::transform([&](auto output_mask) {
                auto bits = table_data
                          | std::views::transform(
                                std::bind_back(bitPower, output_mask))
                          | std::ranges::to<std::vector>();
                auto sum_value_table =
                    bonc::LookupTable::create("", input_width, 1, bits);
                return sum_value_table->getANFRepresentation(0);
              })
            | std::ranges::to<std::vector>();
  std::vector<PolyhedronVertex> trails;
  trails.emplace_back(std::views::repeat(0, input_width + output_width));

  for (std::size_t i = 1; i < input_masks_size; ++i) {
    std::vector<int> minimal_masks;
    for (std::size_t j = 1; j < output_masks_size; ++j) {
      bool covered = false;
      auto anf = anfs.at(j);
      for (auto index = 0uz; index < anf.size(); index++) {
        if (anf.test(index) && ((index | i) == index)) {
          covered = true;
          break;
        }
      }
      if (!covered) {
        continue;
      }

      bool should_add = true;
      std::vector<std::size_t> to_remove;
      for (std::size_t idx = 0; idx < minimal_masks.size(); ++idx) {
        int existing = minimal_masks[idx];
        if (((static_cast<std::size_t>(existing) | j) == j)) {
          should_add = false;
          break;
        }
        if (((static_cast<std::size_t>(existing) | j)
             == static_cast<std::size_t>(existing))) {
          to_remove.push_back(idx);
        }
      }
      if (!should_add) {
        continue;
      }
      for (auto it = to_remove.rbegin(); it != to_remove.rend(); ++it) {
        minimal_masks.erase(minimal_masks.begin()
                            + static_cast<std::ptrdiff_t>(*it));
      }
      minimal_masks.push_back(static_cast<int>(j));
    }

    for (int mask : minimal_masks) {
      PolyhedronVertex input_bits =
          PolyhedronVertex::fromIntBits(i, input_width);
      PolyhedronVertex output_bits = PolyhedronVertex::fromIntBits(
          static_cast<std::uint64_t>(mask), output_width);
      PolyhedronVertex combined(std::views::concat(input_bits, output_bits));
      trails.push_back(std::move(combined));
    }
  }

  return trails;
}
