#pragma once

#include "test.hpp"
#include <array>

class AllStatus{
private:
    int m_eigenStaus{1};
    std::array<int, 2> m_graphicsStatus{1, 1};
public:
    AllStatus(
    ):
        m_eigenStaus(testEigen())
    {}

    inline void setEigeneStatus(const int eigenStatus){
        m_eigenStaus = eigenStatus;
    }

    inline void setGraphicsStatus(const std::array<int, 2>& graphicsStatus){
        m_graphicsStatus = graphicsStatus;
    }
};
