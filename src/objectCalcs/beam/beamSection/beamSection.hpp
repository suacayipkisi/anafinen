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

#pragma once

// Beam cross-sections as library objects: elements reference one by index (BeamElement::
// sectionID), like materials. A section is a shape with its dimensions; the solver gets the
// numbers from computeProperties().
//
// Only doubly symmetric shapes: the centroid is the shear center and the local y / z axes are
// principal axes, which is what the beam element assumes. Channels, angles and tees need a
// principal angle and a shear center offset and come later.
//
// Section plane: origin at the centroid, local y along the height (the web of an I-section,
// so its strong axis is z and a horizontal beam with the default orientation stands upright),
// local z along the width. Lengths in m.

#include <beam/beamProperties/element.hpp>

#include <array>
#include <cstdint>
#include <expected>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace FEM::BEAM {

  // Properties entered directly (no shape: no outline, no stresses later).
  struct GeneralSection { SectionProperties values; };
  struct RectangleSection { double height{}; double width{}; };
  struct CircleSection { double diameter{}; };
  struct PipeSection { double outerDiameter{}; double wallThickness{}; };
  // Hollow rectangle (RHS / SHS). Corner radii as in EN 10210 (hot finished: outer 1.5 t,
  // inner 1.0 t) or EN 10219 (cold formed: inner = outer - t); 0 = sharp corners.
  struct BoxSection {
    double height{};
    double width{};
    double wallThickness{};
    double outerCornerRadius{};
    double innerCornerRadius{};
  };
  // Doubly symmetric I / H (IPE, HEA, HEB, ...): web along local y, root radius between web
  // and flanges.
  struct ISection {
    double height{};
    double flangeWidth{};
    double webThickness{};
    double flangeThickness{};
    double rootRadius{};
  };

  using SectionShape = std::variant<GeneralSection, RectangleSection, CircleSection, PipeSection, BoxSection, ISection>;

  // Key of the shape in JSON files and the UI: "general", "rectangle", "circle", "pipe", "box", "i".
  const char* shapeKey(const SectionShape& shape) noexcept;

  // Why the dimensions do not describe a valid shape (non-positive or non-finite values,
  // walls thicker than the section, radii that do not fit), or nothing.
  std::expected<void, std::string> validateShape(const SectionShape& shape);

  // A, Iy, Iz, J, Asy, Asz of a valid shape. The shear areas use Cowper's shear coefficients
  // (J. Appl. Mech. 33, 1966), which depend on Poisson's ratio, so the material is needed;
  // a GeneralSection returns its values unchanged.
  //   rectangle  exact A, I; J from the series formula (Roark), kappa = 10(1+v)/(12+11v)
  //   circle     exact A, I, J; kappa = 6(1+v)/(7+6v)
  //   pipe       exact A, I, J; Cowper's hollow circle
  //   box        exact A, I with the corner radii; J from EN 10219-2 / 10210-2 (Bredt + open
  //              part); Cowper's thin-walled box per direction
  //   I          exact A, I with the root fillets; J from the ArcelorMittal fillet formula;
  //              Asy from Cowper's thin-walled I (shear along the web), Asz = kappa_rect * 2 b tf
  SectionProperties computeProperties(const SectionShape& shape, double poissonsRatio);

  // Boundary loops of the section, points {y, z} in m. Loop 0 is the outer boundary,
  // counter-clockwise in the (z, y) plane; further loops are holes, clockwise. Arcs use
  // segmentsPerQuarter chords per 90 degrees. Empty for a GeneralSection.
  std::vector<std::vector<std::array<double, 2>>> sectionOutline(const SectionShape& shape, int segmentsPerQuarter = 8);

  class BeamSection {
  private:
    std::string m_name;
    SectionShape m_shape;
    bool m_isBuiltin{false};
    std::uint32_t m_sectionID{}; // stable ID, never reused (built-ins: 0..n-1 from the catalog file)

  public:
    BeamSection(std::string name, SectionShape shape, const bool isBuiltin = false, const std::uint32_t sectionID = 0) :
      m_name(std::move(name)),
      m_shape(std::move(shape)),
      m_isBuiltin(isBuiltin),
      m_sectionID(sectionID)
    {}

    const std::string& getName() const {return m_name;}
    const SectionShape& getShape() const {return m_shape;}
    bool getIsBuiltin() const {return m_isBuiltin;}
    std::uint32_t getSectionID() const {return m_sectionID;}
  };

} // namespace FEM::BEAM end
