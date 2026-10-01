# Using anaf_io: Import / Export Guide

This guide shows how code outside `src/io/` reads and writes model files through `anaf_io`: a CLI, a new solver, a GUI panel or a test. It covers the public headers, the functions they declare, and the usual call sequences.

For the file formats themselves (which data goes where in MSH, VTK, VTU, `.pvd` and the STEP sidecar), see [FILE_HANDLING.md](FILE_HANDLING.md).

> **Document status**
> Verified against: `v0.1.3-alpha` (released 2026-10-01), content checked 2026-09-28 (after `8a947e1`).

## 1. Overview

```text
  caller                          public headers of anaf_io                     inside anaf_io
  ------                          -------------------------                     --------------
  CLI / solver thread / test ---> io/meshIo.hpp            readMesh / writeMesh ---> formats/*   (private)
                                    |                                                 detail/*    (private)
  GUI frame loop ---------------> io/service/ioService.hpp  IoService: one I/O thread,
                                    |                        IoTask polled every frame
                                    v
  solver adapter <--------------> io/model/meshModel.hpp    MeshModel: nodes, element blocks, sets,
  (e.g. trussMeshAdapter)           io/model/elementType.hpp   fields, BCs, loads, global data
                                    io/core/ioTypes.hpp        options, errors, reports, IoContext
                                    io/core/pathUtf8.hpp       UTF-8 <-> std::filesystem::path
```

- `readMesh` / `writeMesh` choose the format themselves. Callers never include `io/formats/*` or `io/detail/*`.
- `MeshModel` is the only exchange type. `anaf_io` knows no solver; every solver converts between `MeshModel` and its own data in an adapter (section 7).

## 2. Public headers

| Header | Declares | Include it when |
|---|---|---|
| `io/meshIo.hpp` | `detectFormat`, `supportedFormats`, `readMesh`, `writeMesh`; includes `ioTypes.hpp` and `meshModel.hpp` | Synchronous I/O: CLI, worker threads, tests |
| `io/service/ioService.hpp` | `IoService`, `IoTask<T>`; includes `meshIo.hpp` | Non-blocking I/O from the GUI thread |
| `io/model/meshModel.hpp` | `MeshModel` and its parts, well-known names (`FieldName`, `Attribute`, `InitialQuantity`, `GlobalName`) | Code that builds or reads a model but does not touch files (adapters) |
| `io/model/elementType.hpp` | `ElementType`, `elementInfo`, `allElementTypes`, `elementTypeFromGmsh`, `elementTypeFromVtk` | Walking element blocks |
| `io/core/ioTypes.hpp` | `FileFormat`, `ReadOptions`, `WriteOptions`, `IoError`, `WriteReport`, `IoContext`, `FormatDescriptor`, `formatName` | Already included by `meshIo.hpp` |
| `io/core/pathUtf8.hpp` | `pathToUtf8`, `pathFromUtf8` | Every path that is shown, logged, or comes from / goes to a UTF-8 string |

Build: link the target to `anaf_io` (`target_link_libraries(<target> PRIVATE anaf_io)`); `anaf_core` already links it. Include paths start at `src/`, for example `#include <io/meshIo.hpp>`.

## 3. Functions and types

### 3.1 `io/meshIo.hpp`

| Function | Returns | Notes |
|---|---|---|
| `detectFormat(path)` | `FileFormat` | Extension first, then content sniffing; `FileFormat::Auto` when unknown |
| `supportedFormats()` | `std::span<const FormatDescriptor>` | Name, extensions, `canRead` / `canWrite`, in display order (dialog filters, CLI help) |
| `readMesh(path, ReadOptions = {}, IoContext = {})` | `std::expected<MeshModel, IoError>` | Safe from any thread |
| `writeMesh(path, model, WriteOptions = {}, IoContext = {})` | `std::expected<WriteReport, IoError>` | Safe from any thread |

Formats (`FileFormat`):

| Value | Extensions | Read | Write |
|---|---|---|---|
| `Msh` | `.msh` (1.0, 2.x, 4.0, 4.1) | yes | yes (2.2 or 4.1) |
| `VtkLegacy` | `.vtk` (2.0 ... 5.1) | yes | yes (4.2 or 5.1) |
| `Vtu` | `.vtu` | yes | yes |
| `Pvd` | `.pvd` + folder of `.vtu` files | yes | yes |
| `Step` | `.step`, `.stp` | yes (meshed on import) | yes (+ `.anafFields` sidecar) |
| `Iges` | `.iges`, `.igs` | yes | no |
| `Brep` | `.brep` | yes | no |

`supportedFormats()` is the authoritative list.

### 3.2 Options

`ReadOptions`:

| Member | Default | Meaning |
|---|---|---|
| `format` | `Auto` | Force a format instead of detecting it |
| `cadMeshDimension` | `1` | CAD import: 1 = curves (trusses, frames), 2 = surfaces, 3 = volumes |
| `cadMeshSize` | `0.0` | Target element size in model units; `<= 0` = one element per curve |
| `cadElementOrder` | `1` | 1 = linear, 2 = quadratic |
| `cadKeepLowerDimensions` | `false` | Also keep boundary elements (e.g. triangles of a tet mesh) |
| `readSidecar` | `true` | Merge `<file>.anafFields` if present |

`WriteOptions`:

| Member | Default | Meaning |
|---|---|---|
| `format` | `Auto` | Force a format instead of using the extension |
| `encoding` | `Ascii` | `Ascii` or `Binary` (MSH, VTK, VTU) |
| `mshVersion` | `V4_1` | `V2_2` or `V4_1` |
| `vtkVersion` | `V5_1` | `V4_2` (every VTK / ParaView) or `V5_1` (VTK >= 9) |
| `compress` | `false` | VTU: zlib-compress binary arrays |
| `timeStep` | `-1` | Single-step formats (VTK, VTU): Time step to write, `-1` = last. `.pvd` writes all steps. Mode / Frequency / LoadCase fields are always written in full (`<name>_<Kind>_NNN` arrays). |
| `writeSidecar` | `true` | STEP: write `<file>.anafFields` with the non-geometric data |
| `writeTags` | `true` | VTK / VTU: store the original node / element tags as arrays |

### 3.3 Results and errors

- `IoError { Code code; std::string message; }`. Codes: `Cancelled`, `FileNotFound`, `UnsupportedFormat`, `ParseError`, `WriteError`, `InvalidModel`, `BackendError`.
- `WriteReport { path, warnings, extraFiles }`. `extraFiles` lists the files written next to `path`: the STEP sidecar, or the per-step `.vtu` files of a `.pvd`.
- `MeshModel::warnings` collects non-fatal issues found while reading.

### 3.4 `IoContext`

```cpp
struct IoContext {
  std::function<bool()> isCancelled;                        // may be empty
  std::function<void(float, std::string_view)> onProgress;  // fraction 0..1, stage label; may be empty
};
```

Cancellation is cooperative: the reader or writer checks `isCancelled` between stages and chunks and then returns `IoError::Code::Cancelled`.

## 4. Synchronous use (CLI, worker thread, test)

1. Convert the user's path with `pathFromUtf8` when it comes from a UTF-8 string (command line on Windows, ImGui, pfd).
2. Call `readMesh`. On failure, report `error().message`.
3. Report `model.warnings`.
4. Work on the model, or convert it with an adapter (section 7).
5. Optionally check `model.validate()`. It returns one message per problem and is empty when the model is consistent.
6. Call `writeMesh` and report `warnings` and `extraFiles`.

```cpp
#include <io/meshIo.hpp>
#include <io/core/pathUtf8.hpp>

#include <print>

using namespace anaf::IO;

int convert(const std::string& inputUtf8, const std::string& outputUtf8) {
  ReadOptions readOptions;
  readOptions.cadMeshDimension = 1;
  readOptions.cadMeshSize = 0.5;

  auto model = readMesh(pathFromUtf8(inputUtf8), readOptions);
  if (!model) {
    std::println(stderr, "read failed: {}", model.error().message);
    return 1;
  }
  for (const auto& warning : model->warnings) std::println("warning: {}", warning);

  if (const auto problems = model->validate(); !problems.empty()) {
    for (const auto& problem : problems) std::println(stderr, "invalid model: {}", problem);
    return 1;
  }

  WriteOptions writeOptions;
  writeOptions.encoding = Encoding::Binary;
  writeOptions.compress = true;
  const auto report = writeMesh(pathFromUtf8(outputUtf8), *model, writeOptions);
  if (!report) {
    std::println(stderr, "write failed: {}", report.error().message);
    return 1;
  }
  for (const auto& file : report->extraFiles) std::println("also wrote {}", file);
  return 0;
}
```

To cancel or show progress from a `std::jthread`:

```cpp
IoContext context;
context.isCancelled = [stop] { return stop.stop_requested(); };
context.onProgress = [](const float fraction, const std::string_view stage) { /* 0..1 */ };
auto model = readMesh(path, {}, context);
```

## 5. Asynchronous use (GUI)

`IoService` owns one worker thread and runs jobs in submission order. Every job returns a `std::shared_ptr<IoTask<T>>`.

| Call | Purpose |
|---|---|
| `importAsync(path, ReadOptions)` | `readMesh` on the I/O thread, returns `IoTask<MeshModel>` |
| `exportAsync(path, shared_ptr<const MeshModel>, WriteOptions)` | `writeMesh` on the I/O thread. The model is shared, not copied. |
| `runAsync<T>(description, job)` | Any job, for example "read, then convert for a solver", so that no step runs on the GUI thread |
| `queuedJobs()` | Jobs waiting behind the current one |

`IoTask<T>` members:

| Member | Blocks | Meaning |
|---|---|---|
| `tryResult()` | no | `const std::expected<T, IoError>*` once ready, `nullptr` before |
| `ready()` | no | Result available |
| `wait()` | yes | Waits for the result; never call it on the GUI thread |
| `progress()`, `stage()` | no | Last reported fraction and stage label |
| `cancel()` | no | Cooperative cancellation |
| `description()` | no | Text given at submission |

Sequence in a panel:

1. Keep one `IoService` for the lifetime of the panel. Its destructor cancels queued jobs and joins the thread, so destroy it before the GL context goes away (see [ARCHITECTURE.md](ARCHITECTURE.md) section 6).
2. Submit a job and store the task.
3. Each frame, call `tryResult()`. While it returns `nullptr`, draw `progress()` / `stage()` and offer `cancel()`.
4. When the result arrives, publish it on the GUI thread (for the truss: through the bridge, see [BRIDGE.md](BRIDGE.md)) and drop the task.

```cpp
#include <io/service/ioService.hpp>

anaf::IO::IoService io;
std::shared_ptr<anaf::IO::IoTask<anaf::IO::MeshModel>> task = io.importAsync(path);

// every frame
if (task) {
  if (const auto* result = task->tryResult()) {
    if (*result) useModel(**result);
    else showError((*result).error().message);
    task.reset();
  } else {
    ImGui::ProgressBar(task->progress());
    ImGui::TextUnformatted(task->stage().c_str());
    if (ImGui::Button("Cancel")) task->cancel();
  }
}
```

Read and convert in one job:

```cpp
auto job = io.runAsync<FEM::TRUSS::ADAPTER::ImportedTruss>(
  "import truss",
  [path, materials](const anaf::IO::IoContext& context)
    -> std::expected<FEM::TRUSS::ADAPTER::ImportedTruss, anaf::IO::IoError> {
    auto model = anaf::IO::readMesh(path, {}, context);
    if (!model) return std::unexpected(model.error());
    return FEM::TRUSS::ADAPTER::toMeshData(*model, materials);
  });
```

Exceptions thrown inside a job become `IoError::Code::BackendError`.

Export from an immutable snapshot:

```cpp
auto snapshot = std::make_shared<const anaf::IO::MeshModel>(std::move(model));
auto exportTask = io.exportAsync(path, snapshot, options);
```

## 6. The model: `MeshModel`

### 6.1 Indexing rules

| Item | Rule |
|---|---|
| Nodes | 0-based index into `nodes`. `Node::tag` keeps the id from the file. |
| Elements | Grouped by type in `blocks`, one block per type (`blockFor(type)`). The global element index runs over the blocks in order. |
| Connectivity | Node indices (not tags), `elementInfo(type).nodeCount` per element, in Gmsh local node order. |
| Element attributes | `elementAttributes[name][globalElementIndex]` |
| Field values | `steps[s][entity * components + c]` |
| Units | SI (`lengthUnit` says otherwise) |

### 6.2 Where each kind of data is stored

| Data | Member | Type / unit |
|---|---|---|
| Geometry | `nodes`, `blocks` | `Node`, `ElementBlock` |
| Named groups (physical groups, materials as `Material:<name>`) | `sets` | `EntitySet` (node or element members) |
| Per-element scalars | `elementAttributes` | `MaterialID`, `CrossSectionArea` (m²), `HeatGeneration` (W/m³), any other name |
| Supports and prescribed displacements | `constraints` | `NodeConstraint{node, fixed, allowedMotion, prescribed (m), amplitude}` |
| Forces | `loads` | `NodalLoad{node, force (N), amplitude}` |
| Prescribed temperatures | `temperatureConstraints` | `TemperatureConstraint{node, temperature (K), amplitude}` |
| Heat loads | `heatLoads` | `HeatLoad{node, power (W), amplitude}` |
| Time functions | `amplitudes` | `Amplitude{name, times, factors}`; `factorAt(t)` is piecewise linear and held constant outside the range |
| Initial state | `initialConditions` | `InitialCondition{quantity, components, values}` |
| Damping | `damping` | `std::optional<Damping>{rayleighAlpha (1/s), rayleighBeta (s), modalRatios}` |
| Results / input fields | `fields` | `Field{name, location, components, times, steps, stepKind, stepLabels}` |
| Model-level arrays | `globalData` | `GlobalArray{name, components, values}` (natural frequencies, statistics) |
| Metadata | `title`, `lengthUnit`, `warnings` | |

An empty `amplitude` name means a constant factor of 1. `validate()` rejects references to unknown amplitudes.

`Field::stepKind` sets what `times[s]` means:

| `StepKind` | `times[s]` | Typical use |
|---|---|---|
| `Time` | time (s) | Static result (one step at 0) or transient history |
| `Frequency` | excitation frequency (Hz) | Frequency response |
| `Mode` | natural frequency (Hz); mode number = `s + 1` | Mode shapes |
| `LoadCase` | load case number | Independent static load cases, names in `stepLabels` |

### 6.3 Well-known names

| Namespace | Names |
|---|---|
| `FieldName` | `Displacement` (node, 3, m), `Stress` (element, Pa, tension > 0), `AxialForce` (element, N) |
| `Attribute` | `MaterialId`, `CrossSectionArea`, `HeatGeneration` |
| `InitialQuantity` | `Displacement` (3, m), `Velocity` (3, m/s), `Temperature` (1, K) |
| `GlobalName` | `NaturalFrequency` (1 component per mode, Hz) |

Any other name also round-trips. The well-known names are the ones other solvers and viewers look for.

### 6.4 Lookups and helpers

| Call | Returns |
|---|---|
| `nodeCount()`, `elementCount()`, `maxDimension()` | Sizes |
| `blockFor(type)` | The block of that type, created on first use |
| `locateElement(global)`, `blockOffset(block)` | Global element index ↔ (block, local index) |
| `nodeIndexByTag()` | File tag → node index map (built on each call; keep it) |
| `findField(name, location)` | `Field*` or `nullptr` |
| `findSet(name, kind)` | `const EntitySet*` or `nullptr` |
| `findAmplitude(name)` | `const Amplitude*` or `nullptr` |
| `findInitialCondition(quantity)` | `const InitialCondition*` or `nullptr` |
| `findGlobal(name)` | `GlobalArray*` or `nullptr` |
| `validate()` | One message per problem, empty when consistent |
| `elementInfo(type)` | `name`, `dimension`, `nodeCount`, `gmshType`, `vtkType`, `edges` |
| `stepKindName(kind)`, `stepKindFromName(text)` | `StepKind` ↔ `"Time"`, `"Frequency"`, `"Mode"`, `"LoadCase"` |

### 6.5 Building a model

The examples assign members one by one. This stays warning-free under `-Wmissing-field-initializers` with every compiler in the matrix, whatever the struct gains later.

```cpp
using namespace anaf::IO;

MeshModel model;
model.title = "two-bar example";
model.nodes = {Node{1, {0.0, 0.0, 0.0}}, Node{2, {1.0, 0.0, 0.0}}, Node{3, {1.0, 1.0, 0.0}}};

auto& bars = model.blockFor(ElementType::Line2);
bars.tags = {1, 2};
bars.entityTags = {0, 0};
bars.connectivity = {0, 1, 1, 2};                     // node indices, 2 per bar
model.elementAttributes[Attribute::CrossSectionArea] = {1e-4, 1e-4};

NodeConstraint support;
support.node = 0;
support.fixed = {true, true, true};
model.constraints.push_back(support);

Amplitude ramp;
ramp.name = "Ramp";
ramp.times = {0.0, 1.0};
ramp.factors = {0.0, 1.0};
model.amplitudes.push_back(ramp);

NodalLoad load;
load.node = 2;
load.force = {0.0, -1000.0, 0.0};
load.amplitude = "Ramp";
model.loads.push_back(load);

Damping damping;
damping.rayleighAlpha = 0.05;
damping.rayleighBeta = 2e-4;
model.damping = damping;
```

Static result (one Time step):

```cpp
Field displacement;
displacement.name = FieldName::Displacement;
displacement.location = FieldLocation::Node;
displacement.components = 3;
displacement.times = {0.0};
displacement.steps = {std::move(values)};            // nodeCount() * 3 values
model.fields.push_back(std::move(displacement));
```

Modal result (one step per mode, plus the frequencies as global data):

```cpp
Field modes;
modes.name = "ModeShape";
modes.location = FieldLocation::Node;
modes.components = 3;
modes.stepKind = StepKind::Mode;
modes.times = {12.4, 31.9};                           // Hz
modes.steps = {std::move(mode1), std::move(mode2)};
model.fields.push_back(std::move(modes));

GlobalArray frequencies;
frequencies.name = GlobalName::NaturalFrequency;
frequencies.values = {12.4, 31.9};
model.globalData.push_back(std::move(frequencies));
```

Load cases use `StepKind::LoadCase`, `times = {1, 2, ...}` and `stepLabels = {"Dead", "Wind", ...}`.

### 6.6 Reading a model

```cpp
if (const Field* u = model.findField(FieldName::Displacement, FieldLocation::Node);
    u && u->components == 3 && !u->steps.empty()) {
  const auto& last = u->steps.back();
  const double uy = last[node * 3 + 1];
}

for (const auto& load : model.loads) {
  const Amplitude* amplitude = model.findAmplitude(load.amplitude);
  const double factor = amplitude ? amplitude->factorAt(time) : 1.0;
  // force = load.force * factor
}

std::size_t global = 0;
for (const auto& block : model.blocks) {
  const auto& info = elementInfo(block.type);
  const auto n = static_cast<std::size_t>(info.nodeCount);
  for (std::size_t e = 0; e < block.size(); ++e, ++global) {
    const std::uint32_t* nodes = &block.connectivity[e * n];
    // global = index into elementAttributes and element fields
  }
}
```

## 7. Adding a solver: the adapter pattern

`anaf_io` must stay free of solver and GUI types. Each solver gets an adapter in its own module, like the truss adapter:

| Function | Direction | Used for |
|---|---|---|
| `toMeshModel(solverData, ...)` | solver → `MeshModel` | Export, including results |
| `toMeshData(model, ...)` | `MeshModel` → solver | Import |

Rules:

1. Put the adapter next to the solver (`src/objectCalcs/<solver>/<solver>IO/`) and its sources in `ANAF_CORE_SOURCES`.
2. Write the well-known names from section 6.3, so that other solvers and ParaView find the data.
3. On import, check `components` and `steps.empty()` before using a field. Collect user-facing remarks (such as ignored element types) instead of failing.
4. Read the BC and load data the solver supports. The truss adapter reads only `fixed`, `allowedMotion` and `force` so far. Prescribed displacements, amplitudes, thermal BCs, initial conditions and damping are stored and round-trip through every format, but no solver uses them yet.
5. Add a round-trip test in `tests/` (see `anaf_truss_io_tests`).

## 8. Paths on Windows

- Never use `path.string()` or `path(std::string)` for text that is shown, logged, or passed to ImGui, Gmsh or pfd. On Windows these use the ANSI code page.
- Use `pathToUtf8(path)` and `pathFromUtf8(text)`. `WriteReport::path` and `extraFiles` are already UTF-8.
- `anaf_io`, the file panel and the native dialog follow this rule (fixed 2026-09-28, [ARCHITECTURE.md](ARCHITECTURE.md) section 8.2).

## 9. Related files

- [../src/io/meshIo.hpp](../src/io/meshIo.hpp): synchronous API
- [../src/io/service/ioService.hpp](../src/io/service/ioService.hpp): `IoService`, `IoTask`
- [../src/io/model/meshModel.hpp](../src/io/model/meshModel.hpp): `MeshModel`
- [../src/io/model/elementType.hpp](../src/io/model/elementType.hpp): element types
- [../src/io/core/ioTypes.hpp](../src/io/core/ioTypes.hpp): options, errors, `IoContext`
- [../src/io/core/pathUtf8.hpp](../src/io/core/pathUtf8.hpp): UTF-8 path helpers
- [../src/objectCalcs/truss_1D/trussIO/trussMeshAdapter.hpp](../src/objectCalcs/truss_1D/trussIO/trussMeshAdapter.hpp): example adapter
- [../src/gui/panels/fileIoPanel.cpp](../src/gui/panels/fileIoPanel.cpp): example `IoService` use
- [../tests/ioTests.cpp](../tests/ioTests.cpp): round-trip tests and a sample model with every kind of data
- [FILE_HANDLING.md](FILE_HANDLING.md): formats and on-disk encoding
