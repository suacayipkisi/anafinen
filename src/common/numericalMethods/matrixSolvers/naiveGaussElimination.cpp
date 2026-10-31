// Copyright (c) 2026 Abdurrahman Konuk (professionally known as Ufuk Deniz Konuk)
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "naiveGaussElimination.hpp"
#include "log/anaf_info.hpp"

#include <Eigen/Core>

namespace anaf::NUMERIC {
  
  std::optional<Eigen::VectorXd> naiveGaussElimination(Eigen::MatrixXd& A, Eigen::VectorXd& b) {
    
    long long theSizeOfMatrix; // max index is (theSizeOfMatrix -1)
    if (A.cols() == A.rows()) {
      theSizeOfMatrix = A.cols();
    } else {
      anaf::LOG::warn("The matrix row and columns are not equal r,c = {},{}. Canot use naive gauss eliminaiton!", 
                      A.cols(), A.cols());
      
      return std::nullopt;
    }
    
    Eigen::MatrixXd theMatrix_A = A;
    Eigen::VectorXd theVector_b = b;
    
    for(long long piv = 0; piv < (theSizeOfMatrix - 1); ++piv) {
      for(long long row = (piv + 1); row < theSizeOfMatrix; ++row) {
        double temp = theMatrix_A(row, piv) /  theMatrix_A(piv, piv);
        theVector_b(row) -= (theVector_b(piv) * temp);
        for (long long col = (piv + 1); col < theSizeOfMatrix; ++col) {
          theMatrix_A(row, col) -= (theMatrix_A(piv, col) * temp);
        }
      }
    }
    
    Eigen::VectorXd resultVec(theSizeOfMatrix);
    
    // omp parellel calc is not implemented because of assuming the matrix is too small to use that method
    for (long long index = (theSizeOfMatrix - 1); index >= 0; --index) {
      double sum = 0.0;
      for(long long j = index + 1; j < theSizeOfMatrix; ++j) {
        sum += theMatrix_A(index, j) * resultVec(j);
      }
      resultVec(index) = (theVector_b(index) - sum ) / theMatrix_A(index, index);
    }
    
    return resultVec;
  }
  
} // namespace anaf::NUMERIC end
