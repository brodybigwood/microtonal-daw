# Architecture

A microtonal DAW. Everything — instruments, effects, even the arranger/timeline —
is a node in one patchable graph. Native Linux (SDL3 + RtAudio + VST3 SDK) and
WASM/Emscripten (SDL3 audio, no VST) from the same source.

## Threading model: two graph copies

The core design decision. To keep the audio callback lock-free there are **two
complete copies of the node graph** (`NodeProcessor.h`):

- `NodeProcessor::guiGraph` — owned by the main/GUI thread. Has SDL textures,
  is rendered and edited.
- `NodeProcessor::dspGraph` — owned by the audio thread. No SDL resources.
  This is the copy that actually produces sound.

Each thread registers the copy it owns via `NodeProcessor::setThreadActiveRoot`
(a thread-local, `UndoManager.cpp`). Undo-action lambdas always mutate "the
thread-active graph", so the *same* lambda works on either copy.

Cross-thread traffic uses two lambda queues:

- **GUI → audio**: every mutation goes through `UndoManager`. `newAction`/
  `undo`/`redo` apply the action to the GUI copy, then
  `enqueueAudioSync(lambda)` queues the identical mutation; the audio callback
  drains the queue via `flushAudioSync()` (`AudioManager.cpp`) *before*
  processing, so the DSP copy replays the exact same edits.
- **Audio → GUI**: `NodeProcessor::enqueueProcessorAction` /
  `flushProcessorActions` (drained each render frame).

**Rule: never mutate graph state directly from the GUI thread without going
through an undo action** — the DSP copy will silently diverge.

## Audio engine

`AudioManager` (singleton) picks a backend at startup: JACK → ALSA (both via
RtAudio, native) → SDL3 audio (fallback, and the only WASM path). The callback:
flush audio-sync queue → `Project::process` → `NodeProcessor::process` →
`dspGraph->process()` → pull-based `outNode->processTree()` which recursively
pulls inputs (guarded by `isProcessed`/`resetProcessTree`).

Connections (`Bus.h`) carry either a `Waveform` (multi-channel float buffer) or
`Events` (MIDI-like note on/off with microtonal pitch, MPE-style per-note).

## Microtonal pitch & rhythm model

Time and pitch are exact rationals, not floats:

- `fract.h` — `{num, den}` rational; used for the playhead and region/element
  positions.
- `Note` stores pitch and rhythm as **vectors of prime-power exponent pairs**
  (`pitchIntegerPairs` etc.): value = ∏ primeᵢ^(numᵢ/denᵢ). Pitch maps to
  `midi = 69 + 12·log₂(ratio)`; rhythm maps to seconds. Notes/regions carry a
  tuning mode (Harmonic lattice vs EDO) — see `PianoRollInternal.h` for the
  shared rational-vector math.
- The PianoRoll renders **pitch lines and rhythm lines** derived from the
  region's lattice instead of a fixed semitone/bar grid.

## Undo system: a tree, not a stack

`UndoManager` keeps a branching history (`ProjectAction` with `children`);
undoing then doing something new creates a branch, and the tree view windows
(`UndoTree*Window`) navigate it. Every action carries `doAction`/`undoAction`
lambdas plus enough JSON snapshot state to replay on either graph copy.
Actions address their target by `managerPath` (patcher/multiplexer ids) +
node/region/note ids, resolved via `requireManager` (`UndoInternal.h`) against
the thread-active graph.

Implementation is split by domain:

- `UndoManager.cpp` — core: newAction/undo/redo/goTo, thread-active root,
  multiplexer sibling replication.
- `UndoActionsNote/Region/Graph/Param/Effect/View.cpp` — the ~35 action types.
- `UndoSerialization.cpp` — `ProjectAction` JSON round-trip.
- `UndoRegistry.cpp` — string-keyed action registry (scripted invocation).
- `UndoTreeView.cpp` — tree-view rendering/hit-testing.

**Save format**: a project folder's `save.json` is the serialized undo tree;
the current action holds the graph snapshot (`savedMainManager`). Loading
rebuilds the graph then replays to the saved tree position.

## UI: one canvas, two window tiers

There is one real main window (`NodeProcessorHost`, created in
`NodeProcessor.cpp`); `WindowHandler::tick()` is the event/render pump
(~60 fps) and owns global shortcuts (Space, Ctrl+Z/Shift+Z, Ctrl+S).

- **`EmbeddedWindow`** — floating windows *inside* the node-editor canvas
  (hit polygon, drag, resize, z-order). `Node` itself derives from
  `EmbeddedWindow`: a node *is* a draggable window with ports. `PianoRoll`
  also derives from it (for coordinate/size bookkeeping) but is only ever
  shown wrapped in a top-level `PianoRollWindow`.
- **`ExpandedWindow` + `WindowManager`** — real top-level SDL windows
  (`*ExpandedWindow` classes), routed by SDL window id.

`NodeEditor` is the canvas: panning, patch-cable drag (`NodeEditorWires.cpp`),
root menu bar (`NodeEditorMenuBar.cpp`), embedded-window ownership and event
routing (`NodeEditorWindows.cpp`).

Node implementations live in `src/nodes/<type>/`; each overrides `process()`
(DSP) and optionally `renderContent`/`handleCustomInput` (UI). `nodetype.h`
enumerates the types. `PatcherNode` contains a nested `NodeManager` (sub-graph);
`MultiplexerNode` holds N parallel patchers, and undo actions targeting one
sibling are replicated to the others (`UndoManager::newAction`).

The VST node (`nodes/vst/`) hosts VST3 plugins natively (X11 editor window,
yabridge-aware, microtonal notes → MPE); it compiles to a stub under WASM.
Plugin undo is two-tier: exposed parameter edits arrive via
`IComponentHandler::performEdit` (`VstParameterChangeAction`, drag-coalesced),
and everything else (mod routing, internal toggles, preset browsing) is caught
by a state-diff poller (`VstNode::pollVstStateForUndo`, driven from
`tickEditor` on the GUI thread) that snapshots component+controller state
while the editor is open and records differences as `VstStateChangeAction`
blobs. Host-initiated state writes mark `vstStateBaselineDirty` (plus a forced
next-tick poll) so the poller re-baselines instead of echoing them; polling
waits out open beginEdit/endEdit gestures and held pointer buttons (a drag
commits as one action on release), and a mapped parameter connection only
causes a silent re-baseline when its driven value actually moved since the
last snapshot. Undo-tree navigation calls
`VstPlugin::commitPendingStateEdits()` first so not-yet-polled plugin edits
become actions instead of being absorbed.

## Build

- Native: `build/` — CMake; SDL3(+ttf/image/gfx), RtAudio, sndfile, VST3 SDK
  (paths in `CMakeLists.txt` are machine-specific). AddressSanitizer is ON for
  native builds (see `CMakeLists.txt`; remove the `-fsanitize=address` lines
  for a fast/release binary).
- WASM: `build-wasm/` — Emscripten, `-sUSE_SDL=3`, output in `html/`.
- CMake globs `src/*.cpp` and `src/nodes/**/*.cpp`; new top-level source files
  are picked up automatically, new *subdirectories* under `src/` are not.
