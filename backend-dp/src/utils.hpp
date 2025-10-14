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

// 用法：
// std::variant<A,B,C> v = ...;
// auto ab1 = assert_into<A,B>(v);         // （若持有 C 则抛异常）