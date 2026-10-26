# CAD4All

AutoCAD-like 2D CAD with parametric 3D (walls, doors, windows, slabs, roofs, stairs). C++20, Qt 6 (Widgets), CMake. Licence GPL-3.0-or-later: every dependency must be GPL-3 compatible (LibreDWG for DWG, OpenCascade for 3D later; no proprietary SDKs such as ODA).

## Build and test

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

LibreDWG is fetched with git and built at configure time (`CAD4ALL_WITH_DWG`, default ON); to reuse a local clone pass `-DFETCHCONTENT_SOURCE_DIR_LIBREDWG=/path/to/libredwg`. Only `src/io/dwg_shim.c` touches LibreDWG.

Screenshot without a display: `QT_QPA_PLATFORM=offscreen ./build/src/app/CAD4All --script docs/esempi/pianta.scr --screenshot out.png`. `--export out.dxf|out.pdf|out.c4a` writes the drawing and exits when no screenshot is asked. `--screenshot3d out.png` saves the 3D view (1200×800). `--dimensioni 1000x700` sets the window size; `QT_FONT_DPI=110` imitates the larger macOS fonts, to check that the ribbon does not crop its labels.

## Layout and rules

- `src/core` is pure C++ (no Qt). `src/engine` and `src/io` may use QtCore only, so they are testable headless. Widgets live in `src/app`.
- All document changes go through `Document::add/remove/replace` so undo works; commands run inside a transaction opened by the engine.
- New commands: subclass `Command` (or `SelectionCommand`) in `src/engine/commands.cpp`, register name + Italian name + alias in `CommandEngine::registerCommands`, add a test in `tests/commands_test.cpp`.
- User-facing text is Italian, prompts follow Italian AutoCAD ("Specificare ..."). Code, comments and identifiers are English.
- Each source file starts with `// SPDX-License-Identifier: GPL-3.0-or-later`.
- Walls, doors and windows: `Wall` (with its `Opening`s) is a shape; joins, plan symbols and the 3D mesh are computed in `src/core/building.cpp`. The 3D view (`src/app/view3d.cpp`) uses the software renderer in `render3d.cpp`, so it needs no OpenGL.
- Stories: every entity has a `story`; the plan shows the current story (`Document::isVisible`), the 3D view every story raised by its elevation. `Slab` and `Roof` are shapes; roofs need a convex or right-angled footprint: `roofGeometry` in `building.cpp` covers an L/T/U plan with its largest rectangles and keeps the highest roof (shared planes are made exactly equal so ties are exact). `Stair` steps come from `stairSteps`. `src/io/meshexport.cpp` writes the 3D model as OBJ/STL.
- Interface: `Visualizza > Interfaccia` switches the AutoCAD profile (ribbon, `src/app/ribbon.cpp`) and the parametric one ("Parametrico": Toolbox, Info Box, Navigator in `src/app/toolbox|infobox|navigator.cpp`); `--profilo autocad|parametrico` and `--viste` set them for screenshots. Never name ArchiCAD in the app or the docs (the user asked to drop it). Tool defaults live in `ToolSettings` (`src/engine/properties.h`), shared by the commands and the Info Box; a new architectural parameter goes in `toolProperties`/`shapeProperties` there. Colours and widget styling live only in `src/app/theme.cpp` (Fusion palette + one style sheet with `@token` colours, dark default, light via `Visualizza > Tema` or `--tema scuro|chiaro`); give a widget an object name and style it there instead of calling `setStyleSheet` or hard-coding colours. `cad4all_ui_tests` drives the panels on the offscreen platform.
- Branding: author, email and years are CMake variables in the top-level `CMakeLists.txt`. The icon's source is `src/app/resources/cad4all.svg`; the PNG sizes, `cad4all.ico` (Windows .rc) and `cad4all.icns` (macOS bundle) next to it are rendered from it and committed. README logos are in `docs/immagini/logo*.png`.
- Installers: `.github/workflows/build.yml` makes them on every run (Inno Setup from `packaging/windows/cad4all.iss.in`, configured by CMake on Windows; `.dmg` with `packaging/macos/Info.plist.in`; AppImage with linuxdeploy from the `cmake --install` tree, which adds the `packaging/linux` desktop and MIME files). A release published on GitHub (or a pushed `v*` tag) gets them attached, and an empty release of the current version is filled at the next push to main; the session's git proxy cannot push tags, so the user creates the release on the website.
