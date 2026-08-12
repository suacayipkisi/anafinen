#pragma once

#include <string>
namespace Info {
    static constexpr std::string m_name{"ANAFINEN"};
    static constexpr std::string m_info{"INFO"}; 
    static constexpr std::string m_warn{"WARN"};
    static constexpr std::string m_error{"ERROR"};

    inline const std::string nameMessage() {return ("[" + m_name + "] ");}
    inline const std::string infoMessage() { return ("[" + m_info + "] ");}
    inline const std::string warnMessage() {return ("[" + m_warn + "] ");}
    inline const std::string errorMessage() {return ("[" + m_error + "] ");}

    inline const std::string name() {return m_name + " ";}
    inline const std::string info() {return m_info + " ";}
    inline const std::string warn() {return m_warn + " ";}
    inline const std::string error() {return m_error + " ";}
}