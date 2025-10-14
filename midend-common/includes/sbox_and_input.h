#pragma once

#include <vector>

#include "frontend_result_parser.h"

namespace bonc {
struct SBoxInputBlock {
  std::vector<bonc::Ref<bonc::BitExpr>> inputs;
  bonc::Ref<bonc::LookupTable> table;

  friend bool operator==(const SBoxInputBlock& lhs,
                         const SBoxInputBlock& rhs) = default;

  friend std::size_t hash_value(const SBoxInputBlock& block);
};

}