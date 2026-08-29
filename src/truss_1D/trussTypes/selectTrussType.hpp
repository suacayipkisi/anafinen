#pragma once

#include "../node.hpp"
#include "../element.hpp"
#include "../appliedForce.hpp"

#include <array>
#include <cstdint>
#include <span>

struct SimpleQuadranglePrismTrussInput{
    std::array<std::uint32_t, 3> cubeNum;
    double cubeEdgeLength;
    double area;
    std::uint32_t type;
};

enum TrussType{
    simpleQuadranglePrism,
    selfBuild
};

class TrussBuild{
private:
    TrussType m_trussType;
    std::span<const Node> m_nodes;
    std::span<const TrussElement_1D> m_elements;
    std::span<const ForceApplied> m_forces;
public:
    TrussBuild( TrussType trussType) : m_trussType(trussType) {
        switch (trussType){
            case simpleQuadranglePrism:

            case selfBuild:
        }
    }

};
