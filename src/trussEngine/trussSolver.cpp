#include "trussSolver.hpp"

#include <cstdint>
#include <iostream>

void trussCalculator(
    std::uint32_t cubeNumX,
    std::uint32_t cubeNumY,
    std::uint32_t cubeNumZ,
    double cubeEdgeLength,
    double area,
    std::uint32_t type
) {
    std::cout << cubeNumX << '\n'
              << cubeNumY << '\n'
              << cubeNumZ << '\n'
              << type <<'\n'
              << cubeEdgeLength << '\n'
              << area / 10000 <<'\n' ;
    
    
}
    