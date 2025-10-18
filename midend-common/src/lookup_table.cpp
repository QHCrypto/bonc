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

  std::optional<LookupTable::DistributionTable> ddt;
  std::optional<LookupTable::DistributionTable> lat;

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
    auto input_size = 1 << input_width;
    auto output_size = 1 << output_width;

    ddt = LookupTable::DistributionTable(input_size,
                                         std::vector<int>(output_size, 0));
    for (auto x1 = 0uz; x1 < input_size; x1++) {
      for (auto x2 = 0uz; x2 < input_size; x2++) {
        auto input = x1 ^ x2;
        auto output = values.at(x1) ^ values.at(x2);
        ddt->at(input).at(output)++;
      }
    }
  }
  void genLAT() {
    if (lat.has_value()) {
      return;  // Already generated
    }
    auto input_size = 1 << input_width;
    auto output_size = 1 << output_width;

    lat = LookupTable::DistributionTable(
        input_size, std::vector<int>(output_size, -input_size / 2));
    for (auto a = 0uz; a < input_size; a++) {
      for (auto b = 0uz; b < output_size; b++) {
        for (auto x = 0uz; x < input_size; x++) {
          auto input = std::popcount(x & a) % 2;
          auto output = std::popcount(values.at(x) & b) % 2;
          if (input == output) {
            lat->at(a).at(b)++;
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

std::uint64_t LookupTable::getInputWidth() const {
  return impl->input_width;
}
std::uint64_t LookupTable::getOutputWidth() const {
  return impl->output_width;
}

const std::vector<std::uint64_t>& LookupTable::tableData() const {
  return impl->values;
}

std::size_t LookupTable::tableSize() const {
  return impl->values.size();
}

boost::dynamic_bitset<> LookupTable::getANFRepresentation(
    std::uint64_t index) const {
  impl->genAnfBits();
  return impl->anf_bits->at(index);
}

const LookupTable::DistributionTable& LookupTable::getDDT() const {
  impl->genDDT();
  return *impl->ddt;
}

const LookupTable::DistributionTable& LookupTable::getLAT() const {
  impl->genLAT();
  return *impl->lat;
}

LookupTable::~LookupTable() = default;

}  // namespace bonc