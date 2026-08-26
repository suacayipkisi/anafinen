#include "simpleTrussCreate.hpp"
#include "../element.hpp"

#include <cstdint>
#include <memory>
#include <vector>
#include <omp.h>
#include <span>

void SimpleTruss::setTruss(){
    // determien node dun in every dimention(x y z directions)
    const std::uint32_t nx = m_cubeNum[0];
    const std::uint32_t ny = m_cubeNum[1];
    const std::uint32_t nz = m_cubeNum[2];

    const std::uint32_t nx_nodes = nx + 1;
    const std::uint32_t ny_nodes = ny + 1;
    const std::uint32_t nz_nodes = nz + 1;

    const std::size_t totalNodes = static_cast<std::size_t>(nx_nodes * ny_nodes * nz_nodes);
    m_allNodes.resize(totalNodes);
    
    // id = i + j * nx_nodes + k * (nx_nodes * ny_nodes)
    // set all truss nodes
    #pragma omp parallel for collapse(3) schedule(static)
    for (std::uint32_t k = 0; k < nz_nodes; ++k) {
        for (std::uint32_t j = 0; j < ny_nodes; ++j) {
            for (std::uint32_t i = 0; i < nx_nodes; ++i) {
                std::uint32_t nodeID = i + j * nx_nodes + k * (nx_nodes * ny_nodes);
                m_allNodes[nodeID] = Node(
                    nodeID,
                    static_cast<double>(m_cubeEdgeLength * i),
                    static_cast<double>(m_cubeEdgeLength * j),
                    static_cast<double>(m_cubeEdgeLength * k)
                );
            }
        }
    }

    anaf_info("All nodes for simple truss created. NodeNum: ", totalNodes);

    // determine elements
    const std::size_t xEdgesCount = static_cast<std::size_t>(nx) * ny_nodes * nz_nodes;
    const std::size_t yEdgesCount = static_cast<std::size_t>(nx_nodes) * ny * nz_nodes;
    const std::size_t zEdgesCount = static_cast<std::size_t>(nx_nodes) * ny_nodes * nz;

    const std::size_t xyCrossCount = static_cast<std::size_t>(nx) * ny * nz_nodes * 2;
    const std::size_t xzCrossCount = static_cast<std::size_t>(nx) * ny_nodes * nz * 2;
    const std::size_t yzCrossCount = static_cast<std::size_t>(nx_nodes) * ny * nz * 2;

    const std::size_t totalElements = xEdgesCount + yEdgesCount + zEdgesCount +
                                      xyCrossCount + xzCrossCount + yzCrossCount;
    m_allElements.resize(totalElements);

    const std::size_t offset_xEdges  = 0;
    const std::size_t offset_yEdges  = offset_xEdges + xEdgesCount;
    const std::size_t offset_zEdges  = offset_yEdges + yEdgesCount;
    const std::size_t offset_xyCross = offset_zEdges + zEdgesCount;
    const std::size_t offset_xzCross = offset_xyCross + xyCrossCount;
    const std::size_t offset_yzCross = offset_xzCross + xzCrossCount;
    const std::span<const Node> allNodes{m_allNodes};

    // parallel to X-direction
    #pragma omp parallel for collapse(3) schedule(static)
    for (std::uint32_t k = 0; k < nz_nodes; ++k) {
        for (std::uint32_t j = 0; j < ny_nodes; ++j) {
            for (std::uint32_t i = 0; i < nx; ++i) {
                std::uint32_t n1 = i + j * nx_nodes + k * (nx_nodes * ny_nodes);
                std::uint32_t n2 = (i + 1) + j * nx_nodes + k * (nx_nodes * ny_nodes);
                std::size_t idx = offset_xEdges + (i + j * nx + k * (nx * ny_nodes));
                m_allElements[idx] = TrussElement_1D(m_type, m_area, n1, n2, allNodes);
            }
        }
    }

    // parallel to Y-directiom
    #pragma omp parallel for collapse(3) schedule(static)
    for (std::uint32_t k = 0; k < nz_nodes; ++k) {
        for (std::uint32_t j = 0; j < ny; ++j) {
            for (std::uint32_t i = 0; i < nx_nodes; ++i) {
                std::uint32_t n1 = i + j * nx_nodes + k * (nx_nodes * ny_nodes);
                std::uint32_t n2 = i + (j + 1) * nx_nodes + k * (nx_nodes * ny_nodes);
                std::size_t idx = offset_yEdges + (i + j * nx_nodes + k * (nx_nodes * ny));
                m_allElements[idx] = TrussElement_1D(m_type, m_area, n1, n2, allNodes);
            }
        }
    }

    // parallel to Z-direction
    #pragma omp parallel for collapse(3) schedule(static)
    for (std::uint32_t k = 0; k < nz; ++k) {
        for (std::uint32_t j = 0; j < ny_nodes; ++j) {
            for (std::uint32_t i = 0; i < nx_nodes; ++i) {
                std::uint32_t n1 = i + j * nx_nodes + k * (nx_nodes * ny_nodes);
                std::uint32_t n2 = i + j * nx_nodes + (k + 1) * (nx_nodes * ny_nodes);
                std::size_t idx = offset_zEdges + (i + j * nx_nodes + k * (nx_nodes * ny_nodes));
                m_allElements[idx] = TrussElement_1D(m_type, m_area, n1, n2, allNodes);
            }
        }
    }

    // cross in X-Y plane
    #pragma omp parallel for collapse(3) schedule(static)
    for (std::uint32_t k = 0; k < nz_nodes; ++k) {
        for (std::uint32_t j = 0; j < ny; ++j) {
            for (std::uint32_t i = 0; i < nx; ++i) {
                std::uint32_t n00 = i + j * nx_nodes + k * (nx_nodes * ny_nodes);
                std::uint32_t n10 = (i + 1) + j * nx_nodes + k * (nx_nodes * ny_nodes);
                std::uint32_t n01 = i + (j + 1) * nx_nodes + k * (nx_nodes * ny_nodes);
                std::uint32_t n11 = (i + 1) + (j + 1) * nx_nodes + k * (nx_nodes * ny_nodes);

                std::size_t baseIdx = offset_xyCross + 2 * (i + j * nx + k * (nx * ny));
                m_allElements[baseIdx]     = TrussElement_1D(
                                                m_type, 
                                                m_area, 
                                                n00,
                                                n11,
                                                allNodes
                                            );
                m_allElements[baseIdx + 1] = TrussElement_1D(
                                                m_type, 
                                                m_area, 
                                                n10,
                                                n01,
                                                allNodes
                                            );
            }
        }
    }

    // cross in X-Z plane
    #pragma omp parallel for collapse(3) schedule(static)
    for (std::uint32_t k = 0; k < nz; ++k) {
        for (std::uint32_t j = 0; j < ny_nodes; ++j) {
            for (std::uint32_t i = 0; i < nx; ++i) {
                std::uint32_t n00 = i + j * nx_nodes + k * (nx_nodes * ny_nodes);
                std::uint32_t n10 = (i + 1) + j * nx_nodes + k * (nx_nodes * ny_nodes);
                std::uint32_t n01 = i + j * nx_nodes + (k + 1) * (nx_nodes * ny_nodes);
                std::uint32_t n11 = (i + 1) + j * nx_nodes + (k + 1) * (nx_nodes * ny_nodes);

                std::size_t baseIdx = offset_xzCross + 2 * (i + j * nx + k * (nx * ny_nodes));
                m_allElements[baseIdx]     = TrussElement_1D(
                                                m_type, 
                                                m_area, 
                                                n00,
                                                n11,
                                                allNodes
                                            );
                m_allElements[baseIdx + 1] = TrussElement_1D(
                                                m_type, 
                                                m_area, 
                                                n10,
                                                n01,
                                                allNodes
                                            );
            }
        }
    }

    // cross in Y-Z plane
    #pragma omp parallel for collapse(3) schedule(static)
    for (std::uint32_t k = 0; k < nz; ++k) {
        for (std::uint32_t j = 0; j < ny; ++j) {
            for (std::uint32_t i = 0; i < nx_nodes; ++i) {
                std::uint32_t n00 = i + j * nx_nodes + k * (nx_nodes * ny_nodes);
                std::uint32_t n10 = i + (j + 1) * nx_nodes + k * (nx_nodes * ny_nodes);
                std::uint32_t n01 = i + j * nx_nodes + (k + 1) * (nx_nodes * ny_nodes);
                std::uint32_t n11 = i + (j + 1) * nx_nodes + (k + 1) * (nx_nodes * ny_nodes);

                std::size_t baseIdx = offset_yzCross + 2 * (i + j * nx_nodes + k * (nx_nodes * ny));
                m_allElements[baseIdx]     = TrussElement_1D(
                                                m_type, 
                                                m_area, 
                                                n00,
                                                n11,
                                                allNodes
                                            );
                m_allElements[baseIdx + 1] = TrussElement_1D(
                                                m_type, 
                                                m_area, 
                                                n10,
                                                n01,
                                                allNodes
                                            );
            }
        }
    }

    anaf_info("All elements for simple truss created. ElementNum: ", totalElements);

} // end: setTruss()
