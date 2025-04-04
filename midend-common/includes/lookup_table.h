#pragma once

#include <cstdint>
#include <vector>

namespace bonc {

class LookupTable {
private:
  std::string name;
  std::uint64_t input_width;
  std::uint64_t output_width;
  std::vector<std::uint64_t> values;

public:
  LookupTable(const std::string& name, std::uint64_t input_width,
              std::uint64_t output_width,
              const std::vector<std::uint64_t>& values)
      : name(name),
        input_width(input_width),
        output_width(output_width),
        values(values) {}

  const std::string& getName() const {
    return name;
  }
};

}