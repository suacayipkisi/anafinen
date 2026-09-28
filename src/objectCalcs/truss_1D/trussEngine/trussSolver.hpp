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

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

#include <bridge/generalStatus.hpp>
#include <trussProperties/appliedForce.hpp>
#include <trussTypes/simpleQuadranglePrismTrussCreate.hpp>
#include <io/model/meshModel.hpp>
#include "trussSolver/deformationUnderConstForce.hpp"

namespace FEM::TRUSS{

  // simple quadrangle prism truss
  class Truss_SQPT {
  private:
    std::vector<ForceApplied> m_force;
    std::vector<double> m_forceVec;
    SimpleTruss m_truss;
    Truss_1D_Container m_container;
  public:
    Truss_SQPT(
      [[maybe_unused]] anaf::BRIDGE::Gui_Calc_Bridge& bridge,
      [[maybe_unused]] std::stop_token st,
      std::uint32_t cubeNumX,
      std::uint32_t cubeNumY,
      std::uint32_t cubeNumZ,
      double elementLength,
      double area,
      std::uint32_t type
    ) :
      // area is entered in cm^2; the model works in m^2
      m_truss({{cubeNumX, cubeNumY, cubeNumZ}, elementLength, area * 1e-4, type})
    {}

    // fixedDOFsByNode must be a copy owned by the worker, not bridge.fixedDOFsByNode:
    // the GUI thread may modify the bridge map while the solve runs.
    void trussSetAndSetFix_SQPT(
      anaf::BRIDGE::Gui_Calc_Bridge& bridge,
      std::stop_token st,
      const anaf::BRIDGE::FixedDOFMap& fixedDOFsByNode
    );

    void trussSetForce_SQRT(anaf::BRIDGE::Gui_Calc_Bridge& bridge, std::stop_token st, std::vector<ForceApplied> force);

    void setContainer(anaf::BRIDGE::Gui_Calc_Bridge& bridge, std::stop_token st);

    void calculate(anaf::BRIDGE::Gui_Calc_Bridge& bridge, std::stop_token st, std::span<anaf::MATERIAL::Material> materials);

    const std::vector<Node>& getNodes() const { return m_truss.getNodes(); }
    const std::vector<TrussElement_1D>& getElements() const { return m_truss.getElements(); }

  };

  // Truss imported from a file or entered by hand in the model editor. The model arrives as
  // a snapshot: node ids are the 0-based node indices, bar materials index the material list
  // the worker copied together with the snapshot. Same container, referee and solvers as
  // Truss_SQPT; only the model source differs.
  class Truss_Imported_or_Entered {
  private:
    std::vector<Node> m_nodes;
    std::vector<TrussElement_1D> m_elements;
    std::vector<std::size_t> m_renderIndex; // m_elements[i] comes from mesh.trussElements[m_renderIndex[i]]
    std::vector<double> m_forceVec;
    Truss_1D_Container m_container;
  public:
    Truss_Imported_or_Entered() = default;

    // Builds the solver nodes and bars and applies the fixity (a worker-owned copy, as for
    // Truss_SQPT). Wireframe edges are skipped. Nodes that no bar uses are held fixed, so a
    // stray node does not make the stiffness matrix singular. Returns why the model cannot
    // be solved (no bars, bars without area, unknown material, zero-length bar, ...).
    std::expected<void, std::string> setModel(
      anaf::BRIDGE::Gui_Calc_Bridge& bridge,
      std::stop_token st,
      const anaf::BRIDGE::MeshData& mesh,
      const anaf::BRIDGE::FixedDOFMap& fixedDOFsByNode,
      std::span<const anaf::MATERIAL::Material> materials
    );

    void setForce(anaf::BRIDGE::Gui_Calc_Bridge& bridge, std::stop_token st, const std::vector<ForceApplied>& force);

    void setContainer(anaf::BRIDGE::Gui_Calc_Bridge& bridge, std::stop_token st);

    void calculate(anaf::BRIDGE::Gui_Calc_Bridge& bridge, std::stop_token st, std::span<const anaf::MATERIAL::Material> materials);

    // Copy of mesh with the results: displacements on the nodes, stress on the solved bars.
    // Wireframe edges, loads and the deformation scale are kept as they are.
    std::shared_ptr<anaf::BRIDGE::MeshData> buildResultMesh(
      const anaf::BRIDGE::MeshData& mesh,
      std::span<const anaf::MATERIAL::Material> materials
    ) const;

    const std::vector<Node>& getNodes() const { return m_nodes; }
    const std::vector<TrussElement_1D>& getElements() const { return m_elements; }
  };

  namespace detail {
    // Shared static solve: stiffness, self weight, displacements, stresses, energy check and
    // the result log. The container must already point at nodes / elements / force vector.
    void runStaticSolve(
      anaf::BRIDGE::Gui_Calc_Bridge& bridge,
      std::stop_token st,
      Truss_1D_Container& container,
      const std::vector<Node>& nodes,
      const std::vector<TrussElement_1D>& elements,
      std::span<const anaf::MATERIAL::Material> materials
    );
  } // namespace detail end

} // namespace FEM::TRUSS end
