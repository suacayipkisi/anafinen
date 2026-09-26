// Copyright (c) 2026 Ufuk Deniz Konuk
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

#include <truss_1D/trussProperties/node.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace anaf::FILE {

  // from FEM::TRUSS::TrussElement_1D 
  struct LineElement {
    std::uint32_t node1{};
    std::uint32_t node2{};
    std::uint32_t materialID{}; // index into the app's material catalog
    double crossSectionArea{}; // m^2
    double stress{}; // axial stress result, 0 for a plain (un-analyzed) mesh
  };

  class MeshImportData {
  private:
    std::vector<FEM::TRUSS::Node> m_nodes;
    std::vector<LineElement> m_elements;
    std::string m_sourcePath;
    bool m_success{false};
    std::string m_errorMessage;

  public:
    MeshImportData() = default;

    void setNodes(std::vector<FEM::TRUSS::Node> nodes) { m_nodes = std::move(nodes); }
    void setElements(std::vector<LineElement> elements) { m_elements = std::move(elements); }
    void setSourcePath(std::string path) { m_sourcePath = std::move(path); }
    void setSuccess(const bool success) { m_success = success; }
    void setErrorMessage(std::string message) { m_errorMessage = std::move(message); }

    const std::vector<FEM::TRUSS::Node>& getNodes() const { return m_nodes; }
    const std::vector<LineElement>& getElements() const { return m_elements; }
    const std::string& getSourcePath() const { return m_sourcePath; }
    bool isSuccess() const { return m_success; }
    const std::string& getErrorMessage() const { return m_errorMessage; }
  };

} // namespace anaf::FILE end
