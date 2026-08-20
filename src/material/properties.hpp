#pragma once

#include <string>
class Material{
private:
    float m_ductility{}; // elongation at break / %
    float m_poissonsRatio{}; // uniteless
    double m_youngModulus{}; // GPa (only axial)
    double m_ultimateTensileStrength{}; // GPa
    double m_yieldTensileStrength{}; // GPa
    double m_bulkModulus{}; // GPa (all side force)
    double m_shearModulus{}; // GPa

    double m_elasticityModulus{}; // GPa

    std::string m_materialType{};
public:
    Material(
        std::string name,
        double elasticityModulusE,
        double shearModulusG,
        double bulkModulusK,
        double yieldTensileStrength,
        double ultimateTensileStrength,
        double youngModulus,
        float poissonsRatio,
        float ductility
    ):
        m_materialType(name),
        m_elasticityModulus(elasticityModulusE),
        m_shearModulus(shearModulusG),
        m_bulkModulus(bulkModulusK),
        m_yieldTensileStrength(yieldTensileStrength),
        m_ultimateTensileStrength(ultimateTensileStrength),
        m_youngModulus(youngModulus),
        m_poissonsRatio(poissonsRatio),
        m_ductility(ductility)
    {}

    // if all properties not determined we can determine later
    inline void setDuctility(const float ductility) {
        if(m_ductility == 0) m_ductility = ductility;
    }
    inline void setPoisson(const float poissons) {
        if(m_poissonsRatio == 0) m_poissonsRatio = poissons;
    }
    inline void setYoungModulus(const double youngModulus) {
        if(m_youngModulus == 0) m_youngModulus = youngModulus;
    }
    inline void setUltTensile(const double ultimateTensileStrength) {
        if(m_ultimateTensileStrength == 0) m_ultimateTensileStrength = ultimateTensileStrength;
    }
    inline void setYieldTensile(const double yieldTensileStrength)  {
        if(m_yieldTensileStrength == 0) m_yieldTensileStrength = yieldTensileStrength;
    }
    inline void setBulkModulus(const double bulkModulus) {
        if(m_bulkModulus == 0) m_bulkModulus = bulkModulus;
    }
    inline void setShearModulues(const double shearModulus) {
        if(m_shearModulus == 0) m_shearModulus = shearModulus;
    }
    inline void setMaterialType(const std::string type) {
        if(m_materialType.empty()) m_materialType = type;
    }

    inline const float getDuctility() const {return m_ductility;}
    inline const float getPoisson() const {return m_poissonsRatio;}
    inline const double getYoungModulus() const {return m_youngModulus;}
    inline const double getUltTensile() const {return m_ultimateTensileStrength;}
    inline const double getYieldTensile() const {return m_yieldTensileStrength;}
    inline const double getBulkModulus() const {return m_bulkModulus;}
    inline const double getShearModulues() const {return m_shearModulus;}
    inline const std::string getMaterialType() const {return m_materialType;}
};
