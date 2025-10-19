#pragma once

#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

namespace detail {
template <typename T>
struct is_variant : std::false_type {};
template <typename... Ts>
struct is_variant<std::variant<Ts...>> : std::true_type {};
template <typename T>
concept Variant = (is_variant<std::remove_cv_t<T>>::value);

template <typename T, typename Var>
struct is_one_of_variant;
template <typename T, typename... Ts>
struct is_one_of_variant<T, std::variant<Ts...>>
    : std::bool_constant<(std::is_same_v<T, Ts> || ...)> {};
template <class T, class Var>
inline constexpr bool is_one_of_variant_v =
    is_one_of_variant<T, std::remove_cv_t<Var>>::value;
}  // namespace detail

/**
 * @brief Assert that a variant value is one of the specified types
 * 
 * @example
 *     std::variant<A,B,C> v = ...;
 *     auto ab1 = assert_into<A,B>(v);         // （若持有 C 则抛异常）
 *
 */
template <class... Keep>
auto assert_into(auto&& v) -> std::variant<Keep...>
  requires(detail::Variant<std::remove_cvref_t<decltype(v)>>
           && sizeof...(Keep) > 0)
{
  using Var = std::remove_cvref_t<decltype(v)>;
  static_assert((detail::is_one_of_variant_v<Keep, Var> && ...),
                "Keep types must be a subset of V's alternatives");
  using Ret = std::variant<Keep...>;
  return std::visit(
      [](auto&& x) -> Ret {
        using X = std::decay_t<decltype(x)>;
        if constexpr ((std::is_same_v<X, Keep> || ...)) {
          return Ret{std::forward<decltype(x)>(x)};
        } else {
          throw std::runtime_error("unexpected alternative");
        }
      },
      std::forward<decltype(v)>(v));
}


/**
 * @brief Parse comma-separated numbers from a string, support a-b for contiguous ranges
 * 
 */
inline std::unordered_set<int> parseCommaSeparatedNumbers(const std::string& str) {
  std::unordered_set<int> result;
  std::stringstream ss(str);
  std::string token;
  while (std::getline(ss, token, ',')) {
    size_t dash_pos = token.find('-');
    if (dash_pos != std::string::npos) {
      int start = std::stoi(token.substr(0, dash_pos));
      int end = std::stoi(token.substr(dash_pos + 1));
      assert(start <= end);
      for (int i = start; i <= end; ++i) {
        result.insert(i);
      }
    } else {
      result.insert(std::stoi(token));
    }
  }
  return result;
}