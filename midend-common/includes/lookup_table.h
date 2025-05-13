#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <memory>
#include <boost/dynamic_bitset.hpp>

#include "ref.h"

namespace bonc {

class LookupTableImpl;

class LookupTable : public boost::intrusive_ref_counter<LookupTable> {
private:
  std::unique_ptr<LookupTableImpl> impl;

public:
  LookupTable(const std::string& name, std::uint64_t input_width,
              std::uint64_t output_width,
              const std::vector<std::uint64_t>& values);

  const std::string& getName() const;

  boost::dynamic_bitset<> getANFRepresentation(std::uint64_t index) const;

  ~LookupTable();
};

}