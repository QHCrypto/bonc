#pragma once

#include <generator>
#include <ranges>
#include <vector>
#include <numeric>

template <template <typename V> typename ResultContainer,
          std::ranges::random_access_range R>
  requires std::copyable<std::ranges::range_value_t<R>>
auto combinations(R &&input, std::size_t k)
    -> std::generator<ResultContainer<std::ranges::range_value_t<R>>> {
  using V = std::ranges::range_value_t<R>;

  // Materialize the input so the generator is independent of the input
  // lifetime.
  std::vector<V> elems;
  if constexpr (std::ranges::sized_range<R>) {
    elems.reserve(std::ranges::size(input));
  }
  for (auto &&v : input) {
    elems.emplace_back(v);
  }

  const std::size_t n = elems.size();
  if (k > n) co_return;

  if (k == 0) {
    co_yield ResultContainer<V>{};
    co_return;
  }

  // Initial index combination [0, 1, ..., k-1]
  std::vector<std::size_t> idx(k);
  std::ranges::iota(idx, 0);

  while (true) {
    // Emit current combination
    co_yield idx
        | std::views::transform([&](std::size_t i) { return elems[i]; })
        | std::ranges::to<ResultContainer>();

    // Find rightmost index to increment
    std::size_t i = k;
    while (i > 0 && idx[i - 1] == (i - 1) + n - k) {
      --i;
    }
    if (i == 0) break;  // done

    --i;  // increment this position
    ++idx[i];
    for (std::size_t j = i + 1; j < k; ++j) {
      idx[j] = idx[j - 1] + 1;
    }
  }
}