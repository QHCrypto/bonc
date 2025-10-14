#include "sbox_and_input.h"

#include <boost/container_hash/hash.hpp>

namespace bonc {

std::size_t hash_value(const SBoxInputBlock& block) {
  auto seed = 0uz;
  for (const auto& input : block.inputs) {
    boost::hash_combine(seed, input);
  }
  boost::hash_combine(seed, block.table);
  return seed;
}

}