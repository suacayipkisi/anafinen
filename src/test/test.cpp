#include <Eigen/Dense>
#include <Eigen/Sparse>

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "../log/anaf_info.h"
#include "test.hpp"

int testEigen(){
    Eigen::Matrix2d K;
    K <<  2.0, -1.0,
         -1.0,  2.0;

    static int eigenStatus {1};

    double det = K.determinant();
    if (det == 3.0) {
        anaf_success("Eigen math backend verified. Det(K) = {%f}", det);
        eigenStatus = 0;
    } else {
        anaf_warn("Eigen determinant mismatch. Result: {%f}", det);
        eigenStatus = 1;
    }

    return eigenStatus;
}
