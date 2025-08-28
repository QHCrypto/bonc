#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ref.h"

namespace bonc {

class BitExpr;

class ReadTarget : public boost::intrusive_ref_counter<ReadTarget> {
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
  std::vector<Ref<BitExpr>> update_expressions;

  ReadTarget(Kind kind, std::string name, std::size_t size)
      : kind{kind}, name{std::move(name)}, size{size} {}

  Kind getKind() const {
    return kind;
  }
  const std::string &getName() const {
    return name;
  }
  std::size_t getSize() const {
    return size;
  }
};

}  // namespace bonc