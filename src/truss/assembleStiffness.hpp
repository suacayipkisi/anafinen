#pragma once

#include "element.hpp"

#include <Eigen/Core>
#include <Eigen/Sparse>
#include <Eigen/SparseCore>
#include <vector>

const std::vector<Eigen::Triplet<double>> assembleStiffness(const std::vector<TrussElement_1D>& elements, const std::uint32_t nodeNum);
