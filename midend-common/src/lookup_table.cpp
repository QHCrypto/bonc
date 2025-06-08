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

  std::optional<std::vector<boost::dynamic_bitset<>>> anf_bits;

  std::optional<LookupTable::DDT>ddt;

  LookupTableImpl(const std::string& name, std::uint64_t input_width,
                  std::uint64_t output_width,
                  const std::vector<std::uint64_t>& values)
      : name{name},
        input_width{input_width},
        output_width{output_width},
        values{values} {
    this->values.resize(1 << input_width, 0);
  }

  void genAnfBits() {
    if (anf_bits.has_value()) {
      return;  // Already generated
    }

    anf_bits =
        std::vector(output_width, boost::dynamic_bitset<>(1 << input_width));
    for (auto i = 0u; i < values.size(); i++) {
      for (auto j = 0u; j < output_width; j++) {
        anf_bits->at(j)[i] = (values.at(i) >> j) & 1;
      }
    }
    for (auto i = 0u; i < input_width; i++) {
      for (auto j = 0u; j < values.size(); j += (1 << (i + 1))) {
        for (auto k = 0u; k < (1 << i); k++) {
          auto left = j + k;
          auto right = j + k + (1 << i);
          for (auto& bits : *anf_bits) {
            bits[right] ^= bits[left];
          }
        }
      }
    }
  }

  void genDDT() {
    if (ddt.has_value()) {
      return;  // Already generated
    }

    ddt = LookupTable::DDT(1 << input_width, std::vector<std::uint64_t>(1 << output_width, 0));
    for (std::uint64_t x1 = 0; x1 < (1 << input_width); ++x1) {
      for (std::uint64_t x2 = 0; x2 < (1 << input_width); ++x2) {
        auto input = x1 ^ x2;
        auto output = values[x1] ^ values[x2];
        ddt->at(input).at(output)++;
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

  std::uint64_t LookupTable::getInputWidth() const {
    return impl->input_width;
  }
  std::uint64_t LookupTable::getOutputWidth() const {
    return impl->output_width;
  }

  boost::dynamic_bitset<> LookupTable::getANFRepresentation(
      std::uint64_t index) const {
        impl->genAnfBits();
    return impl->anf_bits->at(index);
  }

  const LookupTable::DDT& LookupTable::getDDT() const {
    impl->genDDT();
    return *impl->ddt;
  }

  LookupTable::~LookupTable() = default;

}  // namespace bonc