#include <fstream>

#include <frontend_result_parser.h>

int main() {
  std::ifstream ifs("bonc.json");
  bonc::FrontendResultParser parser(ifs);
  for (auto& info : parser.parseAll()) {
    std::cout << "Output: " << info.name << ", Size: " << info.size << "\n";
    for (auto& expr : info.expressions) {
      expr->print(std::cout);
      bitExprToANF(expr).print(std::cout);
      std::cout << "\n";
    }
  }
}