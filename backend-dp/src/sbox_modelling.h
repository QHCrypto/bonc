#pragma once

#include <lookup_table.h>

#include <vector>

#include "polyhedron.h"

/**
 * @brief Reduce the set of inequalities while keeping all given points
 * feasible.
 *
 * Implements the 'Algorithm 1' of [Xiang 2016]
 *
 * @ref [Xiang 2016] https://doi.org/10.1007/978-3-662-53887-6_24
 * @ref
 * https://github.com/xiangzejun/MILP_Division_Property/blob/master/algorithm1/reducelin.py
 *
 * @param inequalities
 * @param points
 * @return Reduced inequalities
 */
std::vector<PolyhedronInequality> reduceInequalities(
    const std::vector<PolyhedronInequality>& inequalities,
    const std::vector<PolyhedronVertex>& points);

std::vector<PolyhedronVertex> divisionPropertyTrail(
    const bonc::Ref<bonc::LookupTable>& sbox);