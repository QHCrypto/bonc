#pragma once

#include <cassert>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>

#include "lookup_table.h"
#include "read_target.h"

namespace bonc {

struct OutputInfo {
  std::string name;
  unsigned size;
  std::vector<std::shared_ptr<BitExpr>> expressions;
};

class FrontendResultParser {
private:
  nlohmann::json value;
  std::map<std::string, std::shared_ptr<ReadTarget>> read_targets;
  std::map<std::string, std::shared_ptr<LookupTable>> lookup_tables;

public:
  FrontendResultParser(std::istream& json_content);

  std::vector<OutputInfo> parseAll();

  std::shared_ptr<ReadTarget> getReadTarget(const std::string& name) const;
  std::shared_ptr<LookupTable> getLookupTable(const std::string& name) const;
};

class BitExpr {
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

  static std::shared_ptr<BitExpr> fromJSON(const FrontendResultParser& parser,
                                           const nlohmann::json& j);
};

class ConstantBitExpr : public BitExpr {
public:
  static const Kind kind = Constant;

private:
  const bool value;

public:
  ConstantBitExpr(bool value) : value{value} {}

  static std::shared_ptr<ConstantBitExpr> create(bool value) {
    return std::make_shared<ConstantBitExpr>(value);
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

class ReadBitExpr : public BitExpr {
public:
  static const Kind kind = Read;

private:
  std::shared_ptr<ReadTarget> target;
  unsigned offset;

public:
  ReadBitExpr(std::shared_ptr<ReadTarget> target, unsigned offset)
      : target{std::move(target)}, offset{offset} {}

  static std::shared_ptr<ReadBitExpr> create(std::shared_ptr<ReadTarget> target,
                                             unsigned offset) {
    return std::make_shared<ReadBitExpr>(std::move(target), offset);
  }

  std::shared_ptr<ReadTarget> getTarget() const {
    return target;
  }
  unsigned getOffset() const {
    return offset;
  }

  Kind getKind() const override {
    return kind;
  }

  void print(std::ostream& os) const override;
};

class LookupBitExpr : public BitExpr {
public:
  static const Kind kind = Lookup;

private:
  std::shared_ptr<LookupTable> table;
  std::vector<std::shared_ptr<BitExpr>> inputs;
  unsigned output_offset;

public:
  LookupBitExpr(std::shared_ptr<LookupTable> table,
                std::vector<std::shared_ptr<BitExpr>> inputs,
                unsigned output_offset)
      : table{table}, inputs{std::move(inputs)}, output_offset{output_offset} {}

  static std::shared_ptr<LookupBitExpr> create(
      std::shared_ptr<LookupTable> table,
      std::vector<std::shared_ptr<BitExpr>> inputs, unsigned output_offset) {
    return std::make_shared<LookupBitExpr>(std::move(table), std::move(inputs),
                                           output_offset);
  }

  std::shared_ptr<LookupTable> getTable() const {
    return table;
  }
  const std::vector<std::shared_ptr<BitExpr>>& getInputs() const {
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
  std::shared_ptr<BitExpr> expr;

public:
  NotBitExpr(std::shared_ptr<BitExpr> expr) : expr{expr} {}

  static std::shared_ptr<NotBitExpr> create(std::shared_ptr<BitExpr> expr) {
    return std::make_shared<NotBitExpr>(expr);
  }

  Kind getKind() const override {
    return kind;
  }
  std::shared_ptr<BitExpr> getExpr() const {
    return expr;
  }

  void print(std::ostream& os) const override;
};

class BinaryBitExpr : public BitExpr {
private:
  Kind kind;
  std::shared_ptr<BitExpr> left, right;

public:
  BinaryBitExpr(Kind kind, std::shared_ptr<BitExpr> left,
                std::shared_ptr<BitExpr> right)
      : kind{kind}, left{left}, right{right} {
    assert(And <= kind && kind <= Xor && "invalid kind");
  }

  static std::shared_ptr<BinaryBitExpr> create(Kind kind,
                                               std::shared_ptr<BitExpr> left,
                                               std::shared_ptr<BitExpr> right) {
    return std::make_shared<BinaryBitExpr>(kind, left, right);
  }

  Kind getKind() const override {
    return kind;
  }

  std::shared_ptr<BitExpr> getLeft() const {
    return left;
  }
  std::shared_ptr<BitExpr> getRight() const {
    return right;
  }

  void print(std::ostream& os) const override;
};

}  // namespace bonc