#pragma once

#include <memory>
#include <string>
#include <vector>

namespace bonc {

class BitExpr;

class ReadTarget {
public:
  enum Kind {
    Invalid = -1,
    State = 0,
    Input,
  };

private:
  const Kind kind;
  std::string name;
  std::size_t size;

public:
  std::vector<std::shared_ptr<BitExpr>> update_expressions;

  ReadTarget(Kind kind, std::string name, std::size_t size)
      : kind{kind}, name{std::move(name)} {}

  Kind getKind() const {
    return kind;
  }
  const std::string &getName() const {
    return name;
  }
};

}  // namespace bonc