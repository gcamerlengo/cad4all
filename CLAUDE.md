# CAD4All

AutoCAD-like 2D CAD with ArchiCAD-style parametric 3D planned. C++20, Qt 6 (Widgets), CMake. Licence GPL-3.0-or-later: every dependency must be GPL-3 compatible (LibreDWG for DWG, OpenCascade for 3D later; no proprietary SDKs such as ODA).

## Build and test

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Screenshot without a display: `QT_QPA_PLATFORM=offscreen ./build/src/app/CAD4All --script docs/esempi/pianta.scr --screenshot out.png`.

## Layout and rules

- `src/core` is pure C++ (no Qt). `src/engine` and `src/io` may use QtCore only, so they are testable headless. Widgets live in `src/app`.
- All document changes go through `Document::add/remove/replace` so undo works; commands run inside a transaction opened by the engine.
- New commands: subclass `Command` (or `SelectionCommand`) in `src/engine/commands.cpp`, register name + Italian name + alias in `CommandEngine::registerCommands`, add a test in `tests/commands_test.cpp`.
- User-facing text is Italian, prompts follow Italian AutoCAD ("Specificare ..."). Code, comments and identifiers are English.
- Each source file starts with `// SPDX-License-Identifier: GPL-3.0-or-later`.
