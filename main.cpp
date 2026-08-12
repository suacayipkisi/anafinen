

#include "src/info.h"
#include <iostream>

int main(){
    std::cout << '\n' << Info::nameMessage() << Info::name() << "is running..." << '\n';

    std::cout << Info::nameMessage() << Info::name() << "is stopping..." << '\n';
    return 0;
}