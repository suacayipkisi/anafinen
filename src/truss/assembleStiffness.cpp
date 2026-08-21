#include "element.hpp"
#include "node.hpp"
#include "../anafInfo.hpp"
#include "assembleStiffness.hpp"

#include <Eigen/Core>
#include <Eigen/Sparse>
#include <Eigen/SparseCore>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

const std::vector<Eigen::Triplet<double>> assembleStiffness(const std::vector<TrussElement_1D>& elements, const std::uint32_t nodeNum){
    const size_t elementNum{elements.size()};
    std::vector<Eigen::Triplet<double>> globalStiffnessMatrix;
    globalStiffnessMatrix.reserve(nodeNum * 3 * nodeNum * 3);

    // fills global stiffness matrix
    for (size_t index{0}; index < elementNum; ++index){

        // 1d element in 3D space, 6 dof (element dgrees of freedom)
        const Eigen::Matrix<double, 6, 6>& elementStiffness{elements[index].getEleStiffness()};
        const std::array<std::shared_ptr<Node>, 2>& elementNodes{elements[index].getEleNodes()};
        const std::uint32_t globalMatrixIndex_1{elementNodes[0]->getNodeID()};
        const std::uint32_t globalMatrixIndex_2{elementNodes[1]->getNodeID()};
        const std::uint32_t zeroPos_1{6 * globalMatrixIndex_1};
        const std::uint32_t zeroPos_2{6 * globalMatrixIndex_2};

        // first send assebmle up half of the element stiffness matrix
        // send half column by half column
        for(size_t lambda_topIndex{0}; lambda_topIndex < 3; ++lambda_topIndex){
            for(size_t lambda_rightIndex{0}; lambda_rightIndex < 3; ++lambda_rightIndex){
                globalStiffnessMatrix.emplace_back(
                    zeroPos_1 + lambda_rightIndex, 
                    zeroPos_1 + lambda_topIndex, 
                    elementStiffness(lambda_rightIndex, lambda_topIndex)
                );
                globalStiffnessMatrix.emplace_back(
                    zeroPos_1 + lambda_rightIndex, 
                    zeroPos_2 + lambda_topIndex, 
                    elementStiffness(lambda_rightIndex, lambda_topIndex + 3)
                );
                globalStiffnessMatrix.emplace_back(
                    zeroPos_2 + lambda_rightIndex, 
                    zeroPos_1 + lambda_topIndex, 
                    elementStiffness(lambda_rightIndex + 3, lambda_topIndex)
                );
                globalStiffnessMatrix.emplace_back(
                    zeroPos_2 + lambda_rightIndex, 
                    zeroPos_2 + lambda_topIndex, 
                    elementStiffness(lambda_rightIndex + 3, lambda_topIndex + 3)
                );
            }
        }
    }
    
    anafLog::info("Global Stiffness Matrix Created, size: {}x{}", (nodeNum * 3), (nodeNum * 3));
    return globalStiffnessMatrix;
}
