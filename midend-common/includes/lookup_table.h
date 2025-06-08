#pragma once

#include <boost/dynamic_bitset.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ref.h"

namespace bonc {

class LookupTableImpl;

class LookupTable : public boost::intrusive_ref_counter<LookupTable> {
private:
  std::unique_ptr<LookupTableImpl> impl;

  LookupTable(const std::string& name, std::uint64_t input_width,
              std::uint64_t output_width,
              const std::vector<std::uint64_t>& values);

public:
  static bonc::Ref<LookupTable> create(
      const std::string& name, std::uint64_t input_width,
      std::uint64_t output_width, const std::vector<std::uint64_t>& values) {
    return new LookupTable(name, input_width, output_width, values);
  }

  const std::string& getName() const;
  std::uint64_t getInputWidth() const;
  std::uint64_t getOutputWidth() const;

  boost::dynamic_bitset<> getANFRepresentation(std::uint64_t index) const;

  using DDT = std::vector<std::vector<std::uint64_t>>;
  const DDT& getDDT() const;

  ~LookupTable();
};

}  // namespace bonc