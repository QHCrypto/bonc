#include "lib.h"

#include <iostream>
#include <fstream>

#include <boost/program_options.hpp>

namespace po = boost::program_options;

int main(int argc, char** argv) {
  po::options_description desc("Allowed options");
  desc.add_options()
    ("help", "Print help message")
    ("input", po::value<std::string>(), "Input file containing the frontend result in JSON format")
    ("input-degree,d", po::value<std::string>()->default_value(""), "BONC Input degree, format \"name1=value1,name2=value2,...\"")
    ("default-input-degree,D", po::value<int>()->default_value(0), "Default BONC Input degree")
    ("expand", po::value<int>(&expand_times)->default_value(1), "Expand substitute operation n times");

  po::positional_options_description p;
  p.add("input", -1);

  po::variables_map vm;
  po::store(po::command_line_parser(argc, argv)
                .options(desc)
                .positional(p)
                .run(),
            vm);
  po::notify(vm);

  if (vm.count("help")) {
    std::cout << desc << '\n';
    return 0;
  }

  std::string filename;
  if (vm.count("input")) {
    filename = vm["input"].as<std::string>();
  } else {
    std::cerr << "No input file specified!\n";
    return 1;
  }

  std::cout << "Reading file: " << filename << '\n';
  // suppressed_read.reserve(1024);
  std::ifstream ifs(filename);
  bonc::FrontendResultParser parser(ifs);

  auto input_degree_str = vm["input-degree"].as<std::string>();
  auto default_input_degree = vm["default-input-degree"].as<int>();
  std::unordered_map<std::string, int> input_degree_map;
  std::stringstream ss(input_degree_str);
  std::string item;
  while (std::getline(ss, item, ',')) {
    auto pos = item.find('=');
    if (pos != std::string::npos) {
      std::string name = item.substr(0, pos);
      int value = std::stoi(item.substr(pos + 1));
      input_degree_map[name] = value;
    }
  }
  setInputDegree(std::move(input_degree_map), default_input_degree);

  auto [_, iterations, outputs] = parser.parseAll();

  std::vector<Polynomial> output_polys;
  for (auto& info : outputs) {
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