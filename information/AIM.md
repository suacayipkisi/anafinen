# ANAFINEN (Analyze Finite Element Engineering) Master Plan

### Libraries
- Calculation: Eigen, Spectra
- Visualisation and GUI: OpenGL, GLAD, GFLW, ImGUI, ImGuizmo, ImPlot
- Multithreading: OpenMP

## Phase 1: Basic Data Structures & Graphic Scene
- Calculating 1D element truss structure in 3D space.
- Calculating longitudional vibration on a Bar or Rod (1D elements)

#### Phase 1 Steps
- [Done] Creating displacement result of the applied force on nodes on 1D Truss in 3D space.
- [Done] Visualize the calculation results in a GUI.
- [Done] Implement calculation validator(energy method).
- [Processing] Add beam calculations
- [Processing] Add import-export to files (.vtk, .msh)

## Phase 2: 
- [Processing] Add various and self build truss types and add import option and implement model tree.
- [Processing] Calculate and visualize dynamic load (longitudional vibration),