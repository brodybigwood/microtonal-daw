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

Channel widths propagate forward: an input port reports its source's width,
`widthFollowsInput()` nodes push it to their outputs, and `syncWidthsFromInputs()`
handles per-port mappings (the arranger's per-track ports). Downstream demand can
make an output wider than its source; processors must zero the channels the
source does not carry (see `GainNode`).

### Arranger, inputs, recording

`ArrangerNode` holds tracks (`TrackManager`) and timeline elements
(`ElementManager`). Each track owns one **output** port on the arranger (Events
for Notes tracks, Waveform for Audio tracks) and one **input** port, created in
track order. Track rows carry solo/mute/record-arm/monitor controls; monitoring
sums the input into the output, mute/solo zero the output after monitoring.

Recording is DSP-side: `TrackManager::captureTakes` reads each armed track's
input every block while the transport plays and the project's global record flag
is on. Finished takes go through a mutex queue to the GUI thread, which writes a
float WAV under `<project>/recordings/` and creates one `RecordTakeAction`: it
trims/splits overlapping positions on that track and inserts a new
AudioClip/Region position. The undo action is replayed on both graphs.

### Mixer chains

`MixerNode` has one multichannel input port per chain. Each chain is a serial
insert list: **the list is processed bottom → top, so the bottom row is the first
processor and the top row is last.** The chain's input feeds the bottom node; each
node's first waveform output feeds the node above; the top output is the chain
output. Nodes are not run through the graph's connection system — the mixer calls
each node's `process()` directly with the signal buffer, and latency-aligns the
chains when summing (widest chain output sets the mixer width).

Insert nodes are `Node`s owned by the mixer's `Chain`, not members of the
`NodeManager` node list. They still share the owning manager's **id namespace**:
created via `NodeManager::allocateNodeID`, registered with
`registerExtraNode`, resolved by the normal `getNode(id)`, and unregistered on
removal. This is what lets every undo action address chain inserts with plain
`managerPath + nodeID` — do not introduce a separate per-chain id pool or a
second addressing form. Old saves with chain-local ids are renumbered on load if
they collide. Chain insert parameter edits are undoable as
`MixerChainNodeParamAction` (mixer + chain + insert id) for the mixer preview.

Nested graphs (the Delay's feedback patcher) extend the path as
`[mixerID, chainID, insertID]`; `requireManager` descends mixer → chain → insert
→ its nested manager.

`DelayNode` runs an inner feedback graph: its `Input` receives
`input + previous*feedback`; the graph's `Output` is what is written back to the
delay line; the wet tap is the delay-line read. An unconnected feedback graph has
no wet signal. 100% feedback is unity. 100% mix is purely the delayed signal.

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

Chain inserts are extra nodes (`NodeManager::extraNodes`, see Mixer above):
created with the manager's id allocator, found by `getNode`, never processed by
the graph pass. Nested graph paths are `[mixerID, chainID, insertID]`.

Actions are serialized and deserialized **iteratively** (explicit stack), not
recursively: the tree is a chain hundreds of actions deep, and recursion both
copied subtrees (quadratic save time) and overflowed the stack.

**Save format**: a project folder's `save.json` is the serialized undo tree;
the current action holds the graph snapshot (`savedMainManager`). Loading
resolves the saved version path and rebuilds both graphs from that snapshot.
Writes are atomic (`save.json.tmp` + rename) on a joinable worker thread.
Every save also writes a never-overwritten
`versions/save_vNNNN_<UTC stamp>/` containing its own `save.json`, VST state
sidecars (`vst/N.bin`), and a copy of the project folder's files (recorded
WAVs etc.); `filepath` values inside that archived JSON are rewritten to the
copies so versions are self-contained.

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
enumerates the types; `NodeTypeStr`/`byType` (`nodetypes.h`) are the registry —
add the enum value, the name, and the factory case. `PatcherNode` contains a
nested `NodeManager` (sub-graph); `MultiplexerNode` holds N parallel patchers,
and undo actions targeting one sibling are replicated to the others
(`UndoManager::newAction`).

`DuplicatorNode` (nodeType 18) copies its whole input to each of eight outputs.
`FilterNode` keeps independent filter state per channel and follows input width.
`MixerNode`'s chain window shows a node-sized live preview of the selected
insert; param textures are renderer-bound SDL objects and are cleared whenever
the window renderer changes.

Nodes cannot be hidden: there is no close box hit-test and no visibility
toggle. `EmbeddedWindow::visible` remains only for utility windows and the
multiplexer's inactive patchers. Port clicks are dispatched to cable dragging
(polygon test on the node body), never to the node's custom content handler;
`handleCustomInput` sees button presses only inside the body.

The VST node (`nodes/vst/`) hosts VST3 plugins natively (X11 editor window,
yabridge-aware, microtonal notes → MPE); it compiles to a stub under WASM.
A bundle can expose many classes: `PlugLib` enumerates every `Audio Module
Class` paired with its controller, and `VstNode` stores a `classIndex` that is
selectable from the plugin menu, serialized, and passed through load/undo. The
host context handed to `initialize()` must implement `IHostApplication` —
plugins such as LSP dereference it and crash with a bare `FUnknown`.
`VstPluginCache` is shared by both graph copies and is keyed by
node + managerPath; a class change recreates the instance.
Plugin undo follows Ardour's VST3 model. Exposed parameter edits arrive via
`IComponentHandler::performEdit` and become `VstParameterChangeAction`s.
`VstPlugin` keeps a per-parameter shadow of the last known normalized value
(Ardour's `VST3PI::_shadow_data`): it is updated by every host-side
`setParameterValue()` — including undo/redo replay — and refreshed from the
controller after any state restore, so an edit the plugin makes without a
host-issued value still has a true previous value. `performEdit` reports that
shadow value and a gesture id; `beginEdit` opens a gesture and `endEdit`
closes it. Actions carry the gesture id and only coalesce with edits of the
same gesture, so a knob drag is one undo step and separate gestures on the
same parameter are separate steps. Plugins that never call beginEdit (LSP)
are grouped by the editor pointer: one implicit gesture per parameter while a
button is held, plus a 250 ms tail after release. Edits with no held button
(wheel ticks, key nudges) group per parameter while less than 400 ms apart,
so a scroll run is one step and a pause starts a new one — including plugins
that bracket each notch in its own beginEdit/endEdit (LSP EQ), while a fresh
pointer press clears the run so drags never merge into it. State blobs are used only for save/load (applied while
the plugin is deactivated, per Ardour's `VST3PI::load_state`), never to derive
undo entries.

## Control API

`ControlServer` speaks one JSON object per line on a Unix socket (enabled from
settings.json); `ControlApi.cpp` dispatches ops/queries/actions, and
`assets/control_docs.json` is the source for `help`/`describe_*`. Actions mutate
through `UndoManager`, so control-API edits are undoable like UI edits. Keep the
docs file in sync when adding an action, query, or op.

## Build

- Native: `build/` — CMake; SDL3(+ttf/image/gfx), RtAudio, sndfile, VST3 SDK
  (paths in `CMakeLists.txt` are machine-specific). AddressSanitizer is ON for
  native builds (see `CMakeLists.txt`; remove the `-fsanitize=address` lines
  for a fast/release binary).
- WASM: `build-wasm/` — Emscripten, `-sUSE_SDL=3`, output in `html/`.
- CMake globs `src/*.cpp` and `src/nodes/**/*.cpp`; new top-level source files
  are picked up automatically, new *subdirectories* under `src/` are not.
