#pragma once

#include <vector>

#include "builder.hpp"

/**
 *
 *               |<------------ target_d ------------->|
 *               .                                     .
 * -------------+=+-+=+---+=+-----------               .
 *  | | | | | | | | | | | | | | | | | |                .   current_reg_index
 * -------------+=+-+=+---+-+-----------               .
 *               |   |     |  diff                     .
 *             +-------------+                         .
 *              \    AND    /                          .
 *               +---------+            ...            .
 *                    \                  |             .
 *                     +-----------------+-------------+
 *                                                     |
 *                                                     v
 * ---------------------------------------------------+=+
 *  | | | | | | | | | | | | | | | | | | | | | | | | | | |  target_reg_index
 * ---------------------------------------------------+=+
 *
 *
 */

struct OptimizeRule {
  std::vector<int> diff;
  std::size_t current_reg_index;
  int target_d;
  std::size_t target_reg_index;
  OptimizeRule(const Monomial& monomial, std::size_t target_reg_index)
      : target_reg_index{target_reg_index} {
    if (monomial.size() < 2) {
      throw std::runtime_error(
          "Monomial must have at least 2 bits as a optimize rule");
    }
    const auto& first_bit = monomial.at(0);
    current_reg_index = first_bit.reg_index;
    auto base_offset = first_bit.offset;
    target_d = -base_offset;
    for (const auto& bit : monomial | std::views::drop(1)) {
      if (bit.reg_index != current_reg_index) {
        throw std::runtime_error(
            "Optimized monomial must based on same register");
      }
      diff.push_back(bit.offset - base_offset);
    }
  }
};

constexpr const bool OPTIMIZE_TWO_TO_ONE = true;

class MonomialOptimizer {
  std::vector<OptimizeRule> rules;

public:
  template <std::ranges::range R>
    requires std::same_as<std::ranges::range_value_t<R>, Rule>
  MonomialOptimizer(R&& rules) {
    for (auto index = 0uz; index < std::ranges::size(rules); index++) {
      auto& rule = rules[index];
      for (const auto& monomial : rule.polynomial) {
        if (monomial.size() > 1
            && std::ranges::all_of(
                monomial | std::views::drop(1), [&](const auto& bit) {
                  return bit.reg_index == monomial[0].reg_index;
                })) {
          this->rules.emplace_back(monomial, index);
        }
      }
    }
    std::ranges::sort(this->rules, [](const auto& lhs, const auto& rhs) {
      return lhs.diff.size() > rhs.diff.size();
    });
  }

  std::vector<Monomial> optimize_one(std::size_t current_reg_index,
                                     const std::vector<int>& offsets) const {
    auto available_rules =
        rules
        | std::views::filter([current_reg_index](const OptimizeRule& rule) {
            return rule.current_reg_index == current_reg_index;
          });

    std::vector<Monomial> all_possible_monomials;

    auto search = [&](this auto&& self, std::vector<int> offsets,
                      Monomial& current_monomial) -> void {
      if (offsets.size() > 1) {
        const auto base = offsets.at(0);
        for (const auto& rule : available_rules) {
          if (rule.target_d + base >= 0) {
            continue;
          }
          std::vector<int> new_offsets;
          auto it = offsets.begin() + 1;
          bool found = true;
          for (auto rule_diff : rule.diff) {
            auto expected_offset = base + rule_diff;
            auto new_it = std::ranges::find(it, offsets.end(), expected_offset);
            if (new_it == offsets.end()) {
              found = false;
              break;
            }
            new_offsets.insert(new_offsets.end(), it, new_it);
            it = new_it + 1;
          }
          if (found) {
            new_offsets.insert(new_offsets.end(), it, offsets.end());
            current_monomial.push_back({.reg_index = rule.target_reg_index,
                                        .offset = rule.target_d + base});
            self(std::move(new_offsets), current_monomial);
            current_monomial.pop_back();
          }
        }
        current_monomial.push_back(
            {.reg_index = current_reg_index, .offset = base});
        self(std::vector<int>(offsets.begin() + 1, offsets.end()),
             current_monomial);
        current_monomial.pop_back();
      } else {
        for (const auto& offset : offsets) {
          current_monomial.push_back(
              {.reg_index = current_reg_index, .offset = offset});
        }
        all_possible_monomials.push_back(current_monomial);
        current_monomial.erase(current_monomial.end() - offsets.size(),
                               current_monomial.end());
      }
    };

    Monomial current_monomial;
    search(offsets, current_monomial);

    // for (const auto& monomial : all_possible_monomials) {
    //   for (const auto& bit : monomial) {
    //     std::print("{}[{}]", bit.reg_index, bit.offset);
    //   }
    //   std::print("\n");
    // }
    if (OPTIMIZE_TWO_TO_ONE && all_possible_monomials.size() == 2) {
      // 当优化结果只有两种可能的时候，只能是这种情形：
      // (1) 原始的乘积 a_1 * a_2 * A
      // (2) 单一的一个优化规则 c * A
      // OPTIMIZE_TWO_TO_ONE 选项指只保留 (2) 这一种情形，因为 (1) “不太可能” 比 (2) 更好
      auto selected = *std::ranges::min_element(all_possible_monomials, std::ranges::less{}, &Monomial::size);
      return {selected};
    }
    return all_possible_monomials;
  }
};