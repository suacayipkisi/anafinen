#pragma once

#include <array>
#include <cstdint>

class Node{
private:
    std::uint32_t m_nodeID{}; // implemented with assuming definition starting with "0 (zero)"

    std::array<bool, 3> m_isMovable{true, true, true};
    std::array<double, 3> m_Location{};
    std::array<double, 3> m_displacement{};
    
public:
    Node() = default;
    Node(
        const std::uint32_t ID,
        const double locX,
        const double locY,
        const double locZ
    ):
        m_nodeID(ID),
        m_Location({locX, locY, locZ})
        //m_force(std::nullopt)
    {}

    Node(
        const std::uint32_t ID,
        const double locX,
        const double locY,
        const double locZ,
        const double forceX,
        const double forceY,
        const double forceZ
    ):
        m_nodeID(ID),
        m_Location({locX, locY, locZ})
        //m_force(std::array<double, 3>{forceX, forceY, forceZ})
    {}

    // nonMovable values must be false if they are non-movable
    inline void setMovable(const std::array<bool, 3> nonMovable) {
        m_isMovable = nonMovable;
        for(std::uint8_t i{0}; i < 3; ++i){
            if(!nonMovable[i]){
                m_Location[i] = 0.0;
            }
        }
    }

    inline void setDisplacements(std::array<double, 3> displacementOfNode) {m_displacement = displacementOfNode;}

    const std::uint32_t getNodeID() const {return m_nodeID;}
    const std::array<double, 3>& getLocation() const {return m_Location;}
    const double getLocX() const {return m_Location[0];}
    const double getLocY() const {return m_Location[1];}
    const double getLocZ() const {return m_Location[2];}

    const std::array<double, 3>& getDisplacmenet() const {return m_displacement;}
    const std::array<bool, 3>& getMovable() const {return m_isMovable;}
};
