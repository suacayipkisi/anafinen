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

// Conversion between the format-neutral anaf::IO::MeshModel and the beam model
// (FEM::BEAM::MeshData), the counterpart of FEM::TRUSS::ADAPTER.
//
// What a file carries for a beam model (every format keeps all of it):
//   elements    Line2; ElementFormulation 1 (Euler-Bernoulli) / 2 (Timoshenko); beamOrientation
//   materials   "Material:<name>" element sets (+ MaterialID index), matched by name
//   sections    "Section:<name>" element sets, SectionShape + SectionDimension1..5 (the shape),
//               and the numbers CrossSectionArea, SecondMomentY/Z, TorsionConstant,
//               ShearAreaY/Z (shear areas with the element's Poisson's ratio) for other programs
//   supports    NodeConstraint: fixed + allowedMotion (inclined), fixedRotation (global axes only)
//   loads       NodalLoad force + moment; UniformLoadGlobalX..Z / UniformLoadLocalX..Z per
//               element; global "Gravity"
//   results     Displacement, Rotation, BeamSectionForce, AxialForce, VonMisesStress

#include <beam/beamProperties/meshData.hpp>
#include <beam/beamSection/beamSection.hpp>
#include <io/model/meshModel.hpp>
#include <material/properties.hpp>

#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace FEM::BEAM::ADAPTER {

  inline constexpr std::string_view kMaterialSetPrefix = "Material:"; // same as the truss adapter
  inline constexpr std::string_view kSectionSetPrefix = "Section:";

  // True when some element of the model is a beam (ElementFormulation 1 or 2): such a file
  // goes to the beam adapter, every other one to the truss adapter.
  bool isBeamModel(const anaf::IO::MeshModel& model);

  // Model -> file. materials and sections are the lists the elements' indices refer to. A
  // rotation support that is not along the global axes cannot be stored (anaf_io keeps
  // rotational fixity per global axis): it is written as fixed about every global axis outside
  // its span, with a message in model.warnings.
  anaf::IO::MeshModel toMeshModel(const MeshData& mesh, std::span<const anaf::MATERIAL::Material> materials,
                                  std::span<const BeamSection> sections);

  struct ImportedBeam {
    std::shared_ptr<MeshData> mesh;
    // Sections the file needs that the list does not have (unknown name, or a known name with
    // other dimensions). Element sectionID values from sections.size() on refer to these, in
    // order; the caller appends them to its list. Names are unique against the list.
    std::vector<BeamSection> newSections;
    std::vector<std::string> notes; // user-facing remarks, "warning: " prefixed for problems
  };

  // File -> model. Only Line2 elements become beams (other elements are reported and
  // skipped). Every beam needs a section: its shape (SectionShape) or at least A, Iy, Iz and J.
  // Results are read when the file has Displacement and BeamSectionForce; the stresses are
  // computed again from them.
  std::expected<ImportedBeam, std::string> toMeshData(const anaf::IO::MeshModel& model,
                                                      std::span<const anaf::MATERIAL::Material> materials,
                                                      std::span<const BeamSection> sections);

} // namespace FEM::BEAM::ADAPTER end
