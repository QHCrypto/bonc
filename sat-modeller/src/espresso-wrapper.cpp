#include "espresso-wrapper.h"

#include <stdexcept>

#include "defer.hpp"

extern "C" {
#define register
#include <espresso.h>
#undef register
#ifdef bool
#undef bool
#endif
}

namespace bonc::sat_modeller::espresso_cxx {

static int init = []() {
#define INIT_STRING(index, string) ::total_name[index] = const_cast<char*>(string)

  INIT_STRING(READ_TIME, "READ       ");
  INIT_STRING(WRITE_TIME, "WRITE      ");
  INIT_STRING(COMPL_TIME, "COMPL      ");
  INIT_STRING(REDUCE_TIME, "REDUCE     ");
  INIT_STRING(EXPAND_TIME, "EXPAND     ");
  INIT_STRING(ESSEN_TIME, "ESSEN      ");
  INIT_STRING(IRRED_TIME, "IRRED      ");
  INIT_STRING(GREDUCE_TIME, "REDUCE_GASP");
  INIT_STRING(GEXPAND_TIME, "EXPAND_GASP");
  INIT_STRING(GIRRED_TIME, "IRRED_GASP ");
  INIT_STRING(MV_REDUCE_TIME, "MV_REDUCE  ");
  INIT_STRING(RAISE_IN_TIME, "RAISE_IN   ");
  INIT_STRING(VERIFY_TIME, "VERIFY     ");
  INIT_STRING(PRIMES_TIME, "PRIMES     ");
  INIT_STRING(MINCOV_TIME, "MINCOV     ");

#undef INIT_STRING
  // option = 0;			/* default -D: ESPRESSO */
  // out_type = F_type;		/* default -o: default is ON-set only */
  ::debug = 0;             /* default -d: no debugging info */
  ::verbose_debug = FALSE; /* default -v: not verbose */
  ::print_solution = TRUE; /* default -x: print the solution (!) */
  ::summary = FALSE;       /* default -s: no summary */
  ::trace = FALSE;         /* default -t: no trace information */
  // strategy = 0;		/* default -S: strategy number */
  // first = -1;			/* default -R: select range */
  // last = -1;
  ::remove_essential = TRUE; /* default -e: */
  ::force_irredundant = TRUE;
  ::unwrap_onset = TRUE;
  ::single_expand = FALSE;
  ::pos = FALSE;
  ::recompute_onset = FALSE;
  ::use_super_gasp = FALSE;
  ::use_random_order = FALSE;
  ::kiss = FALSE;
  ::echo_comments = TRUE;
  ::echo_unknown_commands = TRUE;
  // exact_cover = FALSE; /* for -qm option, the default */
  return 0;
}();

void PLADestructor::operator()(void* pla) const {
  if (pla) {
    free_PLA(static_cast<pPLA>(pla));
  }
}

void setPos(bool pos) {
  ::pos = pos;
}

PPLA readPlaForEspresso(const std::string& input) {
  memset(&::cube, 0, sizeof(::cube));
  memset(&::temp_cube_save, 0, sizeof(::temp_cube_save));
  memset(&::cdata, 0, sizeof(::cdata));
  memset(&::temp_cdata_save, 0, sizeof(::temp_cdata_save));

  auto fp = fmemopen(const_cast<char*>(input.c_str()), input.size(), "r");
  if (!fp) {
    throw std::runtime_error("Failed to open memory stream for PLA input");
  }
  defer {
    fclose(fp);
  };
  pPLA pla;
  if (::read_pla(fp, TRUE, TRUE, FD_type, &pla) == EOF) {
    throw std::runtime_error("Failed to read PLA from input");
  }
  return PPLA(pla);
}

std::string plaToString(const PPLA& pla) {
  if (!pla) {
    throw std::runtime_error("PLA pointer is null");
  }
  char* bufloc = nullptr;
  size_t sizeloc = 0;
  FILE* fp = open_memstream(&bufloc, &sizeloc);
  if (!fp) {
    throw std::runtime_error("Failed to open memory stream for PLA output");
  }
  defer {
    fclose(fp);
  };
  ::fprint_pla(fp, static_cast<pPLA>(pla.get()), F_type);
  fflush(fp);
  return std::string(bufloc, sizeloc);
}

TableTemplate plaToTableTemplate(const PPLA& pla) {
  if (!pla) {
    throw std::runtime_error("PLA pointer is null");
  }
  auto pp = static_cast<pPLA>(pla.get());

  TableTemplate table;
  pcube last, p;
  foreach_set(pp->F, last, p) {
    std::vector<TableTemplate::Entry> clause;
    for (int var = 0; var < cube.num_vars - 1; var++) {
      auto ch = static_cast<TableTemplate::Entry>(GETINPUT(p, var));
      clause.push_back(ch);
    }
    table.addClause(clause);
  }
  return table;
}

}