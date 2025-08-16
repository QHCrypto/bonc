#pragma once

#include <memory>
#include <string>

#include "table-template.h"

namespace bonc::sat_modeller::espresso_cxx {

struct PLADestructor {
  void operator()(void* pla) const;
};

using PPLA = std::unique_ptr<void, PLADestructor>;

void setPos(bool pos);
PPLA readPlaForEspresso(const std::string& input);
std::string plaToString(const PPLA& pla);

TableTemplate plaToTableTemplate(const PPLA& pla);

}  // namespace espresso