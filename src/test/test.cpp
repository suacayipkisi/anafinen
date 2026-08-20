#include <Eigen/Dense>
#include <Eigen/Sparse>

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "../anafInfo.hpp"
#include "test.hpp"

int testEigen(){
    Eigen::Matrix2d K;
    K <<  2.0, -1.0,
         -1.0,  2.0;

    static int eigenStatus {1};

    double det = K.determinant();
    if (det == 3.0) {
        anafLog::success("Eigen math backend verified. Det(K) = {}", det);
        eigenStatus = 0;
    } else {
        anafLog::warn("Eigen determinant mismatch. Result: {}", det);
        eigenStatus = 1;
    }

    return eigenStatus;
}