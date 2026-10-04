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
- [Done] Add beam / frame calculations (Euler-Bernoulli, Timoshenko, sections, end releases; released in v0.2.0-alpha)
- [Processing] Add import-export to files (.vtk, .msh)
- [Planned] Exchange complete models and results with other programs (.inp, .frd, .bdf; see INTEROP_PLAN.md)
- [Planned] Semi-rigid beam joints: rotational springs at released ends, after modal analysis

## Phase 2: 
- [Processing] Add various and self build truss types and add import option and implement model tree.
- [Planned] Calculate and visualize dynamic load: modal analysis first (mass matrix, Spectra), then harmonic and transient. Not available in v0.2.0-alpha; the GUI only shows the inputs.