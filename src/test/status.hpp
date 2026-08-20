#pragma once

#include "test.hpp"

class AllStatus{
private:
    int m_eigenStaus{1};
public:
    AllStatus(
    ):
        m_eigenStaus(testEigen())
    {}

    inline void setEigeneStatus(const int status){
        m_eigenStaus = status;
    }
};