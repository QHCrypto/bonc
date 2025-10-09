#pragma once

#include <boost/functional/hash.hpp>
#include <cassert>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <tuple>

#include "anf.h"
#include "lookup_table.h"
#include "read_target.h"
#include "ref.h"

namespace bonc {

struct OutputInfo {
  std::string name;
  std::size_t size;
  std::vector<Ref<BitExpr>> expressions;
};

struct FrontendResult {
  std::vector<Ref<ReadTarget>> inputs;
  std::vector<Ref<ReadTarget>> iterations;
  std::vector<OutputInfo> outputs;
};

class ExprStoreHash {
public:
  std::size_t operator()(const Ref<BitExpr>& expr) const;
};

class ExprStoreEqual {
public:
  bool operator()(const Ref<BitExpr>& lhs, const Ref<BitExpr>& rhs) const;
};

using ExprStore = std::unordered_set<Ref<BitExpr>, ExprStoreHash, ExprStoreEqual>;

class FrontendResultParser {
private:
  nlohmann::json value;
  std::map<std::string, Ref<ReadTarget>> read_targets;
  std::map<std::string, Ref<LookupTable>> lookup_tables;

  mutable ExprStore expr_store;

public:
  FrontendResultParser(std::istream& json_content);

  template <std::derived_from<BitExpr> T, typename... Args>
  Ref<T> createExpr(Args&&... args) const {
    Ref expr = new T(std::forward<Args>(args)...);
    if (auto it = expr_store.find(expr); it != expr_store.end()) {
      return boost::static_pointer_cast<T>(*it);
    }
    auto [it, suc] = expr_store.insert(std::move(expr));
    return boost::static_pointer_cast<T>(*it);
  }

  FrontendResult parseAll();

  Ref<ReadTarget> getReadTarget(const std::string& name) const;
  Ref<LookupTable> getLookupTable(const std::string& name) const;
};

class BitExpr : public boost::intrusive_ref_counter<BitExpr> {
public:
  enum Kind {
    Constant = 0,
    Read,
    Lookup,
    Not,
    And,
    Or,
    Xor,
  };
  BitExpr() = default;
  virtual ~BitExpr() = default;
  friend bool operator==(const BitExpr& lhs, const BitExpr& rhs) {
    return lhs.equals(rhs);
  }

  virtual Kind getKind() const = 0;
  virtual void print(std::ostream& os) const = 0;
  virtual bool equals(const BitExpr& rhs) const = 0;
  virtual std::size_t hash_value() const = 0;

  static Ref<BitExpr> fromJSON(const FrontendResultParser& parser,
                               const nlohmann::json& j);
};

class ConstantBitExpr : public BitExpr {
public:
  static const Kind kind = Constant;

private:
  const bool value;

public:
  ConstantBitExpr(bool value) : value{value} {}

  Kind getKind() const override {
    return kind;
  }

  bool getValue() const {
    return value;
  }

  void print(std::ostream& os) const override {
    os << value;
  }
  bool equals(const BitExpr& rhs) const override {
    if (auto other = dynamic_cast<const ConstantBitExpr*>(&rhs)) {
      return value == other->value;
    }
    return false;
  }
  std::size_t hash_value() const override {
    return std::hash<bool>()(value);
  }
};

struct ReadTargetAndOffset {
  Ref<ReadTarget> target;
  unsigned offset;

  ReadTargetAndOffset(Ref<ReadTarget> target, unsigned offset)
      : target{std::move(target)}, offset{offset} {}

  friend bool operator==(const ReadTargetAndOffset& lhs,
                         const ReadTargetAndOffset& rhs) = default;

  friend std::size_t hash_value(const ReadTargetAndOffset& rto) {
    return boost::hash<bonc::Ref<bonc::ReadTarget>>{}(rto.target) ^ rto.offset;
  }
  void print(std::ostream& os) const {
    os << target->getName() << "[" << offset << "]";
  }
};

class ReadBitExpr : public BitExpr {
public:
  static const Kind kind = Read;

private:
  ReadTargetAndOffset target_and_offset;

public:
  ReadBitExpr(Ref<ReadTarget> target, unsigned offset)
      : target_and_offset(std::move(target), offset) {}

  const ReadTargetAndOffset& getTargetAndOffset() const {
    return target_and_offset;
  }

  Ref<ReadTarget> getTarget() const {
    return target_and_offset.target;
  }
  unsigned getOffset() const {
    return target_and_offset.offset;
  }

  Kind getKind() const override {
    return kind;
  }

  void print(std::ostream& os) const override {
    target_and_offset.print(os);
  }
  bool equals(const BitExpr& rhs) const override {
    if (auto other = dynamic_cast<const ReadBitExpr*>(&rhs)) {
      return target_and_offset == other->target_and_offset;
    }
    return false;
  }
  std::size_t hash_value() const override {
    return boost::hash<ReadTargetAndOffset>()(target_and_offset);
  }
};

class LookupBitExpr : public BitExpr {
public:
  static const Kind kind = Lookup;

private:
  Ref<LookupTable> table;
  std::vector<Ref<BitExpr>> inputs;
  unsigned output_offset;

public:
  LookupBitExpr(Ref<LookupTable> table, std::vector<Ref<BitExpr>> inputs,
                unsigned output_offset)
      : table{table}, inputs{std::move(inputs)}, output_offset{output_offset} {}

  Ref<LookupTable> getTable() const {
    return table;
  }
  const std::vector<Ref<BitExpr>>& getInputs() const {
    return inputs;
  }
  unsigned getOutputOffset() const {
    return output_offset;
  }

  Kind getKind() const override {
    return kind;
  }

  void print(std::ostream& os) const override;
  bool equals(const BitExpr& rhs) const override {
    if (auto other = dynamic_cast<const LookupBitExpr*>(&rhs)) {
      return table == other->table && inputs == other->inputs &&
             output_offset == other->output_offset;
    }
    return false;
  }
  std::size_t hash_value() const override {
    std::size_t seed = boost::hash<Ref<LookupTable>>{}(table);
    boost::hash_range(seed, inputs.begin(), inputs.end());
    boost::hash_combine(seed, output_offset);
    return seed;
  }
};

class NotBitExpr : public BitExpr {
public:
  static const Kind kind = Not;

private:
  Ref<BitExpr> expr;

public:
  NotBitExpr(Ref<BitExpr> expr) : expr{expr} {}

  Kind getKind() const override {
    return kind;
  }
  Ref<BitExpr> getExpr() const {
    return expr;
  }

  void print(std::ostream& os) const override;
  bool equals(const BitExpr& rhs) const override {
    if (auto other = dynamic_cast<const NotBitExpr*>(&rhs)) {
      return expr == other->expr;
    }
    return false;
  }
  std::size_t hash_value() const override {
    return boost::hash<Ref<BitExpr>>{}(expr);
  }
};

class BinaryBitExpr : public BitExpr {
private:
  Kind kind;
  Ref<BitExpr> left, right;

public:
  BinaryBitExpr(Kind kind, Ref<BitExpr> left, Ref<BitExpr> right)
      : kind{kind}, left{left}, right{right} {
    assert(And <= kind && kind <= Xor && "invalid kind");
    if (left.get() > right.get()) {
      std::swap(left, right);
    }
  }

  Kind getKind() const override {
    return kind;
  }

  Ref<BitExpr> getLeft() const {
    return left;
  }
  Ref<BitExpr> getRight() const {
    return right;
  }

  void print(std::ostream& os) const override;
  bool equals(const BitExpr& rhs) const override {
    if (auto other = dynamic_cast<const BinaryBitExpr*>(&rhs)) {
      return kind == other->kind && left == other->left &&
             right == other->right;
    }
    return false;
  }

  std::size_t hash_value() const override {
    std::size_t seed = boost::hash<Kind>()(kind);
    boost::hash_combine(seed, left);
    boost::hash_combine(seed, right);
    return seed;
  }
};

ANFPolynomial<ReadTargetAndOffset> bitExprToANF(Ref<BitExpr> expr,
                                                int read_depth = 0);

// Get methods for tuple-like access
template <std::size_t I>
auto get(const ReadTargetAndOffset& rto) {
  if constexpr (I == 0) {
    return rto.target;
  } else if constexpr (I == 1) {
    return rto.offset;
  } else {
    static_assert(I < 2, "Index out of bounds for ReadTargetAndOffset");
  }
}

}  // namespace bonc

template <>
struct std::tuple_size<bonc::ReadTargetAndOffset>
    : std::integral_constant<size_t, 2> {};

template <size_t I>
struct std::tuple_element<I, bonc::ReadTargetAndOffset> {
  using type = std::conditional_t<I == 0, bonc::Ref<bonc::ReadTarget>, unsigned>;
};
