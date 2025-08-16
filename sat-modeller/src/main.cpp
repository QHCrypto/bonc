#include "sat-modeller.h"

int main() {
  using namespace bonc::sat_modeller;
//   espresso_cxx::setPos(true);
//   auto pla = espresso_cxx::readPlaForEspresso(R"(
// .i 12
// .o 8
// ----1------0 10000000
// ----0-----0- 10000000
// 0----------- 10000000
// -----1-----0 01000000
// -----0----0- 01000000
// -0---------- 01000000
// ------1----0 00100000
// ------0---0- 00100000
// --0--------- 00100000
// -------1---0 00010000
// -------0--0- 00010000
// ---0-------- 00010000
// 0---1---0--- 00001000
// 0---0----0-- 00001000
// -0---1--0--- 00000100
// -0---0---0-- 00000100
// --0---1-0--- 00000010
// --0---0--0-- 00000010
// ---0---10--- 00000001
// )");
//   auto table = espresso_cxx::plaToTableTemplate(pla);
//   for (const auto& clause : table) {
//     for (const auto& entry : clause) {
//       switch (entry) {
//         case TableTemplate::Entry::Unknown: std::cout << "?"; break;
//         case TableTemplate::Entry::Positive: std::cout << "+"; break;
//         case TableTemplate::Entry::Negative: std::cout << "-"; break;
//         case TableTemplate::Entry::NotTaken: std::cout << "0"; break;
//       }
//     }
//     std::cout << std::endl;
//   }
//   auto str = espresso_cxx::plaToString(pla);
//   std::cout << str << std::endl;

  SATModel model;
  auto a = model.createVariable("a");
  auto b = model.createVariable("b");
  auto c = model.createVariable("c");
  // auto r = model.createVariable("r");
  auto true_ = model.createVariable("true");
  model.addClause({ true_ });
  // model.addXorClause({a, b, c}, r);
  // model.addAndClause({a, b, c}, r);
  model.addSequentialCounterLessEqualClause({a, b, c}, 2);
  model.addAndClause({a, b}, true_);
  model.printDIMACS(std::cout);
}