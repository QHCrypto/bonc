#include "lookup_table.h"

#include <boost/dynamic_bitset.hpp>
#include <memory>

#include "anf.h"

namespace bonc {

class LookupTableImpl {
public:
  std::string name;
  std::uint64_t input_width;
  std::uint64_t output_width;
  std::vector<std::uint64_t> values;
  std::vector<boost::dynamic_bitset<>> anf_bits;

  LookupTableImpl(const std::string& name, std::uint64_t input_width,
                  std::uint64_t output_width,
                  const std::vector<std::uint64_t>& values)
      : name{name},
        input_width{input_width},
        output_width{output_width},
        values{values} {
    this->values.resize(1 << input_width, 0);
    anf_bits =
        std::vector(output_width, boost::dynamic_bitset<>(1 << input_width));
    for (auto i = 0u; i < values.size(); i++) {
      for (auto j = 0u; j < output_width; j++) {
        anf_bits.at(j)[i] = (values.at(i) >> j) & 1;
      }
    }
    for (auto i = 0u; i < input_width; i++) {
      for (auto j = 0u; j < values.size(); j += (1 << (i + 1))) {
        for (auto k = 0u; k < (1 << i); k++) {
          auto left = j + k;
          auto right = j + k + (1 << i);
          for (auto& bits : anf_bits) {
            bits[right] ^= bits[left];
          }
        }
      }
    }
  }
};

LookupTable::LookupTable(const std::string& name, std::uint64_t input_width,
                         std::uint64_t output_width,
                         const std::vector<std::uint64_t>& values)
    : impl{std::make_unique<LookupTableImpl>(name, input_width, output_width,
                                             values)} {}

const std::string& LookupTable::getName() const {
  return impl->name;
}

LookupTable::~LookupTable() = default;

}  // namespace bonc