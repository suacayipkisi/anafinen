#include "node.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>
#include "../anafInfo.hpp"

const double nodeDistance(std::array<std::shared_ptr<Node>, 2> nodes){
    if(!(nodes[0] && nodes[1])){
        anafLog::warn("All nodes have not initialized yet, cannot calculate distance!");
        return 0.0;
    }
    const std::array<double, 3>& loc_1{nodes[0]->getLocation()};
    const std::array<double, 3>& loc_2{nodes[1]->getLocation()};
    return (
        std::sqrt(
            (loc_1[0] - loc_2[0]) * (loc_1[0] - loc_2[0]) +
            (loc_1[1] - loc_2[1]) * (loc_1[1] - loc_2[1]) +
            (loc_1[2] - loc_2[2]) * (loc_1[2] - loc_2[2])
        )
    );
}
