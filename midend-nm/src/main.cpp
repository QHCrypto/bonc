#include "lib.h"

#include <iostream>
#include <fstream>


int main(int argc, char** argv) {
  const char* filename = "bonc_.json";
  if (argc > 1) {
    filename = argv[1];
  }
  std::cout << "Reading file: " << filename << '\n';
  // suppressed_read.reserve(1024);
  std::ifstream ifs(filename);
  bonc::FrontendResultParser parser(ifs);

  std::vector<Polynomial> output_polys;
  for (auto& info : parser.parseAll()) {
    std::cout << "Output: " << info.name << ", Size: " << info.size << "\n";
    for (auto& expr : info.expressions) {
      output_polys.push_back(bitExprToANF(expr));
    }
  }
  for (auto& poly : output_polys) {
    std::cout << std::clamp(numericMapping(poly), -1, std::numeric_limits<int>::max()) << ',';
  }
  std::cout << '\n';

  // for (auto i = 0uz; i < output_polys.size(); i++) {
  //   if (i % (384 * 8) == 0) {
  //     auto one_poly = output_polys.at(i);
  //     one_poly = bonc::expandANF(one_poly.translate(numericMappingSubstitute));
  //     std::cout << std::clamp(numericMapping(one_poly), -1, 1000) << ',';
  //     std::cout.flush();
  //   }
  // }
  // int i = 921;
  // auto one_poly = output_polys.at(i);
  // std::cerr << (1152 - i - 2) << ':' << numericMapping(one_poly) << '\n';
}