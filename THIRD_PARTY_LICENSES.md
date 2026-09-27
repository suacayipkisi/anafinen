# Third-Party Notices and Licenses

This project incorporates and builds upon the following third-party software and fonts:

1. **Gmsh**
   - License: GNU General Public License v2 or later (GPL-2.0-or-later)
   - Copyright (C) Christophe Geuzaine, Jean-François Remacle

2. **Eigen 3**
   - License: Mozilla Public License 2.0 (MPL-2.0)
   - Copyright (C) Eigen authors

3. **Spectra**
   - License: Mozilla Public License 2.0 (MPL-2.0)
   - Copyright (C) Yixuan Qiu

4. **Dear ImGui / ImGuizmo / ImPlot**
   - License: MIT License
   - Copyright (C) Omar Cornut, Cedric Guillemet, Evan Pezent

5. **GLFW**
   - License: zlib/libpng License
   - Copyright (C) Camilla Löwy

6. **GLAD**
   - License: Public Domain / MIT

7. **portable-file-dialogs** (vendored header, `external/portable-file-dialogs/`)
   - License: WTFPL v2 (see [external/portable-file-dialogs/COPYING](external/portable-file-dialogs/COPYING))
   - Copyright (C) Sam Hocevar

8. **zlib** (VTU compression, via the system library)
   - License: zlib License
   - Copyright (C) Jean-loup Gailly and Mark Adler

9. **SuiteSparse / CHOLMOD** (optional sparse Cholesky solver)
   - License: CHOLMOD modules Core, Check, Cholesky, Partition, Utility: GNU LGPL v2.1 or later; modules MatrixOps, Modify, Supernodal: GNU GPL v2 or later
   - Copyright (C) Timothy A. Davis and the SuiteSparse authors

10. **Open CASCADE Technology** (CAD import / export, used through Gmsh)
   - License: GNU LGPL v2.1 with the Open CASCADE exception
   - Copyright (C) Open CASCADE SAS

11. **GLM**
   - License: MIT License (or the Happy Bunny License, at the user's choice)
   - Copyright (C) G-Truc Creation

12. **libpng**
   - License: PNG Reference Library License version 2
   - Copyright (C) Cosmin Truta and the PNG Reference Library authors

13. **Inter Font**
   - See license at [assets/fonts/Inter/LICENSE.txt](assets/fonts/Inter/LICENSE.txt)

14. **CascadiaCode Font**
   - See license at [assets/fonts/CascadiaCode/LICENSE.txt](assets/fonts/CascadiaCode/LICENSE.txt)

## License of anafinen

anafinen is licensed under the GNU General Public License v3.0 or later. The full text is in the
`LICENSE` file shipped with every package (Linux: `/usr/share/doc/anafinen/LICENSE`; Windows: next to `anafinen.exe`).

## Source code of GPL / LGPL components

Binary packages (in particular the Windows ZIP, which redistributes the Gmsh and SuiteSparse
libraries) are covered by the corresponding source code available at:

- anafinen: https://github.com/suacayipkisi/anafinen (the tag matching the package version)
- Gmsh: https://gmsh.info/src/ (the version bundled with the package, e.g. 4.15.x)
- SuiteSparse: https://github.com/DrTimothyAldenDavis/SuiteSparse
- Open CASCADE Technology: https://dev.opencascade.org/release

On request, the maintainer (konuki8523@gmail.com) provides the complete corresponding source code
of these components for at least three years after the package was distributed (GPLv3 section 6).
