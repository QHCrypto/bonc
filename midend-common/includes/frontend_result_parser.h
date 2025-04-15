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
  unsigned size;
  std::vector<Ref<BitExpr>> expressions;
};

class FrontendResultParser {
private:
  nlohmann::json value;
  std::map<std::string, Ref<ReadTarget>> read_targets;
  std::map<std::string, Ref<LookupTable>> lookup_tables;

public:
  FrontendResultParser(std::istream& json_content);

  std::vector<OutputInfo> parseAll();

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

  virtual Kind getKind() const = 0;
  virtual void print(std::ostream& os) const = 0;

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

  static Ref<ConstantBitExpr> create(bool value) {
    return new ConstantBitExpr(value);
  }

  Kind getKind() const override {
    return kind;
  }

  bool getValue() const {
    return value;
  }

  void print(std::ostream& os) const override {
    os << value;
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

  static Ref<ReadBitExpr> create(Ref<ReadTarget> target, unsigned offset) {
    return new ReadBitExpr(std::move(target), offset);
  }

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

  static Ref<LookupBitExpr> create(Ref<LookupTable> table,
                                   std::vector<Ref<BitExpr>> inputs,
                                   unsigned output_offset) {
    return new LookupBitExpr(std::move(table), std::move(inputs),
                             output_offset);
  }

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
};

class NotBitExpr : public BitExpr {
public:
  static const Kind kind = Not;

private:
  Ref<BitExpr> expr;

public:
  NotBitExpr(Ref<BitExpr> expr) : expr{expr} {}

  static Ref<NotBitExpr> create(Ref<BitExpr> expr) {
    return new NotBitExpr(expr);
  }

  Kind getKind() const override {
    return kind;
  }
  Ref<BitExpr> getExpr() const {
    return expr;
  }

  void print(std::ostream& os) const override;
};

class BinaryBitExpr : public BitExpr {
private:
  Kind kind;
  Ref<BitExpr> left, right;

public:
  BinaryBitExpr(Kind kind, Ref<BitExpr> left, Ref<BitExpr> right)
      : kind{kind}, left{left}, right{right} {
    assert(And <= kind && kind <= Xor && "invalid kind");
  }

  static Ref<BinaryBitExpr> create(Kind kind, Ref<BitExpr> left,
                                   Ref<BitExpr> right) {
    return new BinaryBitExpr(kind, left, right);
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
