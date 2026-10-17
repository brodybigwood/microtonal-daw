#include "ControlDocs.h"

using json = nlohmann::json;

namespace {

json P(const char* type, const char* meaning, bool required = true) {
    return json{{"type", type}, {"meaning", meaning}, {"required", required}};
}

json A(const char* summary, const json& params, const char* example) {
    return json{{"summary", summary}, {"params", params}, {"example", example}};
}

const json kManagerPath = P("int[]", "graph address; [] is the root graph (see the managerPath concept)", false);

} // namespace

const json& controlConcepts() {
    static const json docs = json{
        {"request", "One JSON object per line over the control socket. Shape: {\"id\":<any scalar>,\"op\":\"<verb>\",...}. The id is echoed in the reply. Verbs: action, query, undo, redo, goto, save, load, transport, help, list_actions, describe_action, list_queries, describe_query, ping, shutdown."},
        {"response", "{\"id\":..,\"ok\":true,\"result\":..} on success, or {\"id\":..,\"ok\":false,\"error\":{\"code\":..,\"message\":..}}. Codes: bad_json, bad_request, unknown_op, unknown_action, unknown_query, not_found, execution_failed, no_project."},
        {"managerPath", "Addresses which graph to act on. [] is the root graph. [p] descends into the Patcher node with id p. [m,q] descends into patcher q of the Multiplexer node m. Paths nest further. Enumerate valid ones with the `managers` query."},
        {"nodeID", "0 is the Output node, 1 is the Input node. Anything else is a node you created with add_node. List them with the `nodes` query."},
        {"portID", "Nodes own input/output ports, each with its own id. The `node` query lists them under inputs[]/outputs[] with dataType (0 = Events/MIDI, 1 = Waveform/audio) and connection state. Connections are (nodeID, portID) pairs."},
        {"undo", "Every `action` is recorded in an undo tree, applied to the GUI graph and replayed on the audio graph. `undo`, `redo` and `goto` walk that tree; `goto` takes a child-index path from the root."},
        {"note pitch", "pitchVector is a positional list of [num,den] exponent pairs over primes [2,3,5,7,11,13,...]: pitch = 69 + 12*log2(product(prime_i ^ (num_i/den_i))). [] is 1/1 = MIDI 69. The 5-limit ratio 5/4 is [[-2,1],[0,1],[1,1]]."},
        {"note rhythm", "rhythmVector / rhythmEndVector are the note's start and end in BEATS, as the same prime-exponent pairs: value = sum((num_i/den_i) * log2(prime_i)). [] is beat 0, [[1,1]] is beat 1, [[1,2]] is beat 0.5."},
        {"tuning", "A region carries the scale. tuningMode 0 = harmonic lattice, 1 = EDO. In harmonic mode, tuningAnchorHarmonic is the anchor harmonic A and tuningHarmonicAnchorVector is that harmonic's pitch; lattice line h sits at anchorVec - dense(A) + dense(h), where dense(h) is h's prime factorisation. A note's tuningAnchorHarmonic is WHICH line it sits on (its scale degree) — not the region's anchor. A note's tuningMode is inherited from the region when the note is created."},
        {"degrees", "Harmonic scale degrees are named by harmonic number: I = 1/2/4/8 (any power of two), II = 9, III = 5 or 10, IV = 4, V = 3 or 6, VI = 5 or 10, VII = 15."},
        {"transport position", "beatPosition/timeSeconds are the transport ANCHOR (the seek target, set when playback starts). The live playhead is effectiveBeatPosition/effectiveTime, published from the audio thread. A playhead that looks stuck at the anchor is normal while playing — read the effective pair."},
        {"threading", "Requests run on the GUI thread, so mutations always land on the authoritative graph copies and are mirrored to the audio graph. No request blocks audio."},
        {"read-live rule", "Actions take INTENT, not state. You pass what you want; the DAW reads the current value itself (old parameter values, existing cables, the note's prior snapshot). Never send a value you read back earlier — it may be stale and will corrupt the undo entry."},
        {"arranger elements", "An Arranger node holds elements and tracks. An ELEMENT is the content — a region (notes), an audio clip, or an automation curve; type 0/1/2 respectively. Regions, clips and curves SHARE one id space per arranger, so an element id is unambiguous. That same id is what you pass as elementID / regionID / curveID depending on the action."},
        {"positions", "A POSITION is a placement of an element on a track at a time — separate from the element itself, and one element can have many. Keys: startOffsetPairs (extra start offset in beats, [] = 0), rhythmVector (start beat), rhythmEndVector (end beat), trackID, id (the positionID used by delete_position / move_element_position), plus rhythmEdo* snapshots. Get them from the `elements` or `region` query."},
        {"tracks", "An Arranger track is {type, id, connectionID}. type 0 = Audio, 1 = Notes. connectionID is the track's output port on the Arranger node (Waveform for Audio, Events for Notes) — adding a Notes track is what gives the arranger its event output. Get them from the `tracks` query."},
        {"automation curve points", "A curve point is {timeVec, v, shape, shapeParam}. timeVec is the X position in BEATS, encoded exactly like note rhythm (value = sum((num_i/den_i)*log2(prime_i)); [] = beat 0, [[1,1]] = beat 1, [[9,1]] = beat 9). v is the value — BPM for the tempo curve, otherwise the target parameter's range. shape: 0 = Hold, 1 = Single (smooth bow), 2 = Double (smoother); shapeParam is -1..1, a bow amount for Single and a balance for Double."},
        {"EQ bands", "ParametricEQ band values are NORMALISED 0..1, not Hz/dB: type 0..1 where the filter is round(type*6) → 0 Peaking, 1 LowShelf, 2 HighShelf, 3 LowPass, 4 HighPass, 5 BandPass, 6 Notch; freq 0..1 → 20 Hz..20 kHz (log); gain 0..1 → -18..+18 dB; q 0..1 → 0.1..10. Defaults: type 0, freq 0.5 (~632 Hz), gain 0.5 (0 dB), q 0.1."},
        {"embedded windows", "Nodes are floating windows inside the canvas and have their own id space, SEPARATE from node ids: ewID comes from the editor's pool (0 and 1 are reserved, so on a fresh graph the Output node gets ewID 2 and the Input node ewID 3). Never compute it — read it from the `embedded_windows` query, and pass it to move_embedded_window / resize_embedded_window."},
        {"vst state blobs", "compState / ctrlState in vst_state_change are base64 strings (standard alphabet, = padding) of the plugin's raw component/controller state. Get the current ones from the plugin, or pass neither to re-apply the current state."},
        {"undo tree", "The `undo` query returns {head, version}. head is the root action; each node has type (an ActionType int), its action-specific fields, children[], name, last_index and optionally savedMainManager (the graph snapshot). version is the child-index path from head to the current action — [] is the root. `goto` consumes exactly that array."},
        {"self-description", "`help` returns these concepts plus indexes. `describe_action` / `describe_query` / `describe_op` return one entry in full, including parameters and a worked example. Prefer those over guessing."},
    };
    return docs;
}

const json& actionDocs() {
    static const json docs = [] {
        json d = json::object();
        d["add_node"] = A("Create a node in a graph.",
            {{"managerPath", kManagerPath}, {"nodeType", P("int", "NodeType index — see the node_types query")},
             {"x", P("float", "canvas x")}, {"y", P("float", "canvas y")}},
            R"({"op":"action","action":"add_node","args":{"managerPath":[],"nodeType":1,"x":300,"y":200}})");
        d["remove_node"] = A("Delete a node and sever its cables.",
            {{"managerPath", kManagerPath}, {"nodeID", P("int", "node to remove")}},
            R"({"op":"action","action":"remove_node","args":{"managerPath":[],"nodeID":2}})");
        d["make_node_connection"] = A("Connect an output port to an input port.",
            {{"managerPath", kManagerPath}, {"srcNodeID", P("int", "source node")}, {"srcConID", P("int", "source output port id")},
             {"dstNodeID", P("int", "destination node")}, {"dstConID", P("int", "destination input port id")}},
            R"({"op":"action","action":"make_node_connection","args":{"managerPath":[],"srcNodeID":2,"srcConID":0,"dstNodeID":3,"dstConID":0}})");
        d["sever_node_connection"] = A("Disconnect a cable.",
            {{"managerPath", kManagerPath}, {"srcNodeID", P("int", "")}, {"srcConID", P("int", "")},
             {"dstNodeID", P("int", "")}, {"dstConID", P("int", "")}},
            R"({"op":"action","action":"sever_node_connection","args":{"managerPath":[],"srcNodeID":2,"srcConID":0,"dstNodeID":3,"dstConID":0}})");
        d["reassign_node_connection"] = A("Move a cable onto ports, replacing whatever is already on them (one undo step). The replaced cables are read live.",
            {{"managerPath", kManagerPath}, {"srcNodeID", P("int", "new source node")}, {"srcConID", P("int", "new source port")},
             {"dstNodeID", P("int", "new destination node")}, {"dstConID", P("int", "new destination port")}},
            R"({"op":"action","action":"reassign_node_connection","args":{"managerPath":[],"srcNodeID":2,"srcConID":0,"dstNodeID":4,"dstConID":0}})");
        d["move_node"] = A("Move a node to an absolute canvas position.",
            {{"managerPath", kManagerPath}, {"nodeID", P("int", "")}, {"toX", P("float", "")}, {"toY", P("float", "")}},
            R"({"op":"action","action":"move_node","args":{"managerPath":[],"nodeID":2,"toX":400,"toY":250}})");
        d["toggle_node_visible"] = A("Show/hide a node.",
            {{"managerPath", kManagerPath}, {"nodeID", P("int", "")}},
            R"({"op":"action","action":"toggle_node_visible","args":{"managerPath":[],"nodeID":2}})");
        d["pan_nodes"] = A("Pan the canvas by a delta.",
            {{"managerPath", kManagerPath}, {"dx", P("float", "")}, {"dy", P("float", "")}},
            R"({"op":"action","action":"pan_nodes","args":{"managerPath":[],"dx":100,"dy":0}})");
        d["zoom_nodes"] = A("Retained for history compatibility; zoom was removed, so this is a no-op.",
            {{"managerPath", kManagerPath}, {"amount", P("float", "")}, {"mx", P("float", "", false)}, {"my", P("float", "", false)}},
            R"({"op":"action","action":"zoom_nodes","args":{"managerPath":[],"amount":1.1}})");
        d["set_param_value"] = A("Set a node parameter. The previous value is read live and stored for undo.",
            {{"managerPath", kManagerPath}, {"nodeID", P("int", "")}, {"paramIndex", P("int", "index into the node's params[], from the node or params query")},
             {"value", P("float", "new value")}},
            R"({"op":"action","action":"set_param_value","args":{"managerPath":[],"nodeID":2,"paramIndex":0,"value":0.75}})");

        d["create_region"] = A("Create an empty region on an Arranger node.",
            {{"managerPath", kManagerPath}, {"nodeID", P("int", "Arranger node")}},
            R"({"op":"action","action":"create_region","args":{"managerPath":[],"nodeID":2}})");
        d["delete_region"] = A("Delete a region.",
            {{"managerPath", kManagerPath}, {"nodeID", P("int", "Arranger node")}, {"regionID", P("int", "element id, see the elements query")}},
            R"({"op":"action","action":"delete_region","args":{"managerPath":[],"nodeID":2,"regionID":0}})");
        d["create_automation_curve"] = A("Create an automation curve on an Arranger node.",
            {{"managerPath", kManagerPath}, {"nodeID", P("int", "Arranger node")}},
            R"({"op":"action","action":"create_automation_curve","args":{"managerPath":[],"nodeID":2}})");
        d["modify_curve_points"] = A("Replace an automation curve's points. The prior point list is read live.",
            {{"managerPath", kManagerPath}, {"nodeID", P("int", "Arranger node")}, {"curveID", P("int", "element id of the curve")},
             {"points", P("object[]", "new points, each {timeVec, v, shape, shapeParam} — see the automation curve points concept")}},
            R"({"op":"action","action":"modify_curve_points","args":{"managerPath":[],"nodeID":2,"curveID":0,"points":[{"timeVec":[],"v":120,"shape":1,"shapeParam":0}]}})");
        d["create_audio_clip"] = A("Create an audio clip element from a file.",
            {{"managerPath", kManagerPath}, {"nodeID", P("int", "Arranger node")}, {"filepath", P("string", "absolute path to a wav/flac")}},
            R"({"op":"action","action":"create_audio_clip","args":{"managerPath":[],"nodeID":2,"filepath":"/home/me/kick.wav"}})");
        d["create_position"] = A("Place an element (region / clip / curve) on an arranger track at a time.",
            {{"managerPath", kManagerPath}, {"nodeID", P("int", "Arranger node")}, {"elementID", P("int", "element id from the elements query")},
             {"startPairs", P("int[][]", "start beat as prime-exponent pairs; [] = beat 0")},
             {"endPairs", P("int[][]", "end beat; [[3,1]] = beat 3", false)},
             {"trackID", P("int", "track id from the tracks query")}},
            R"({"op":"action","action":"create_position","args":{"managerPath":[],"nodeID":2,"elementID":0,"startPairs":[],"endPairs":[[3,1]],"trackID":0}})");
        d["delete_position"] = A("Remove a placement from the timeline.",
            {{"managerPath", kManagerPath}, {"nodeID", P("int", "")}, {"elementID", P("int", "")}, {"positionID", P("int", "")}},
            R"({"op":"action","action":"delete_position","args":{"managerPath":[],"nodeID":2,"elementID":0,"positionID":0}})");
        d["move_element_position"] = A("Move a placement. before/after must both be FULL position objects; the action reads rhythmVector, rhythmEndVector, startOffsetPairs and trackID from each (the position keeps its existing id).",
            {{"managerPath", kManagerPath}, {"nodeID", P("int", "Arranger node")}, {"elementID", P("int", "")}, {"positionID", P("int", "")},
             {"before", P("object", "position snapshot to move away from, from the `elements` query")},
             {"after", P("object", "the same object with rhythmVector/rhythmEndVector/trackID changed")}},
            R"({"op":"action","action":"move_element_position","args":{"managerPath":[],"nodeID":2,"elementID":0,"positionID":0,"before":{"startOffsetPairs":[],"rhythmVector":[],"rhythmEndVector":[[3,1]],"trackID":0},"after":{"startOffsetPairs":[],"rhythmVector":[[1,1]],"rhythmEndVector":[[4,1]],"trackID":0}}})");
        d["add_arranger_track"] = A("Add a track to an Arranger node. A Notes track also creates the node's event output port.",
            {{"managerPath", kManagerPath}, {"nodeID", P("int", "Arranger node")},
             {"trackType", P("int", "0 = Audio, 1 = Notes")}},
            R"({"op":"action","action":"add_arranger_track","args":{"managerPath":[],"nodeID":2,"trackType":1}})");
        d["remove_arranger_track"] = A("Remove an arranger track.",
            {{"managerPath", kManagerPath}, {"nodeID", P("int", "")}, {"trackType", P("int", "0 = Audio, 1 = Notes")},
             {"trackID", P("int", "")}, {"connectionID", P("int", "the track's connection id")}},
            R"({"op":"action","action":"remove_arranger_track","args":{"managerPath":[],"nodeID":2,"trackType":1,"trackID":0,"connectionID":0}})");

        d["create_note"] = A("Create a note in a region. The note is stamped with the region's scale, and its lattice degree is derived from its pitch (or taken from tuningAnchorHarmonic).",
            {{"managerPath", kManagerPath}, {"nodeID", P("int", "Arranger node")}, {"regionID", P("int", "region element id")},
             {"pitchVector", P("int[][]", "pitch as prime-exponent pairs; [] = 1/1 = MIDI 69")},
             {"rhythmVector", P("int[][]", "start beat; [] = beat 0", false)},
             {"rhythmEndVector", P("int[][]", "end beat; defaults to rhythmVector", false)},
             {"tuningAnchorHarmonic", P("int", "force the scale degree instead of deriving it", false)},
             {"intent", P("object", "override the anchors stamped from the region: tuningMode, tuningAnchorHarmonic, tuningEdo*, rhythmEdoSubdivisionSteps, rhythmEdoLowerVector, rhythmEdoUpperVector", false)}},
            R"({"op":"action","action":"create_note","args":{"managerPath":[],"nodeID":2,"regionID":0,"pitchVector":[[-2,1],[0,1],[1,1]],"rhythmVector":[],"rhythmEndVector":[[1,1]]}})");
        d["delete_note"] = A("Delete a note.",
            {{"managerPath", kManagerPath}, {"nodeID", P("int", "")}, {"regionID", P("int", "")}, {"noteID", P("int", "from the region query")}},
            R"({"op":"action","action":"delete_note","args":{"managerPath":[],"nodeID":2,"regionID":0,"noteID":0}})");
        d["move_note"] = A("Change a note. The prior snapshot is read live; you supply only the fields that change (intent).",
            {{"managerPath", kManagerPath}, {"nodeID", P("int", "")}, {"regionID", P("int", "")}, {"noteID", P("int", "")},
             {"intent", P("object", "any subset of: pitchVector, rhythmVector, rhythmEndVector, channel, tuningMode, tuningAnchorHarmonic, tuningEdo*, rhythmEdo*")}},
            R"({"op":"action","action":"move_note","args":{"managerPath":[],"nodeID":2,"regionID":0,"noteID":0,"intent":{"pitchVector":[[-1,1],[1,1]]}}})");
        d["move_notes"] = A("Change several notes at once (one undo step).",
            {{"managerPath", kManagerPath}, {"nodeID", P("int", "")}, {"regionID", P("int", "")},
             {"notes", P("object[]", "each {noteID, intent}")}},
            R"({"op":"action","action":"move_notes","args":{"managerPath":[],"nodeID":2,"regionID":0,"notes":[{"noteID":0,"intent":{"rhythmVector":[[1,1]]}}]}})");
        d["piano_roll_region_tuning"] = A("Set a region's scale AND its rhythm grid. Prior tuning is read live; supply the fields that change. Set this BEFORE creating notes so they are stamped with it (this is also what Ctrl+Shift+Right-click on a note does, using that note's rhythm anchors).",
            {{"managerPath", kManagerPath}, {"nodeID", P("int", "")}, {"regionID", P("int", "")},
             {"intent", P("object", "subset of: tuningMode, tuningAnchorHarmonic, tuningHarmonicAnchorVector, tuningEdoSubdivisionSteps, tuningEdoLowerVector, tuningEdoUpperVector, rhythmEdoSubdivisionSteps, rhythmEdoLowerVector, rhythmEdoUpperVector. The rhythm grid draws [rhythmEdoLowerVector, rhythmEdoUpperVector] split into rhythmEdoSubdivisionSteps ([] = beat 0, default upper [[1,1]] = beat 1).")},
             {"name", P("string", "undo entry label", false)}},
            R"({"op":"action","action":"piano_roll_region_tuning","args":{"managerPath":[],"nodeID":2,"regionID":0,"intent":{"tuningMode":0,"tuningAnchorHarmonic":8}}})");
        d["assign_note_harmonic"] = A("Assign a note's tuning/harmonic (region tuning and note tuning change together).",
            {{"managerPath", kManagerPath}, {"nodeID", P("int", "")}, {"regionID", P("int", "")}, {"noteID", P("int", "")},
             {"intent", P("object", "tuning fields to apply")}},
            R"({"op":"action","action":"assign_note_harmonic","args":{"managerPath":[],"nodeID":2,"regionID":0,"noteID":0,"intent":{"tuningAnchorHarmonic":9}}})");
        d["songroll_rhythm_edo"] = A("Change an arranger's rhythm-EDO subdivision: the beat grid it draws is [lower, upper] split into `steps` equal parts (steps that land on a whole beat are marked as beats). Prior value read live.",
            {{"managerPath", kManagerPath}, {"nodeID", P("int", "Arranger node")},
             {"after", P("object", "{steps: int (number of equal subdivisions), lower: [[num,den]...] lower time bound in beats ([] = 0), upper: [[num,den]...] upper time bound (default [[1,1]] = beat 1)}")}},
            R"({"op":"action","action":"songroll_rhythm_edo","args":{"managerPath":[],"nodeID":2,"after":{"steps":4,"lower":[],"upper":[[2,1]]}}})");
        d["toggle_piano_roll_window"] = A("Open or close a region's piano roll window.",
            {{"managerPath", kManagerPath}, {"nodeID", P("int", "Arranger node")}, {"regionID", P("int", "")},
             {"open", P("bool", "true to open, false to close", false)}},
            R"({"op":"action","action":"toggle_piano_roll_window","args":{"managerPath":[],"nodeID":2,"regionID":0,"open":true}})");

        d["move_embedded_window"] = A("Move a floating window inside the canvas (nodes are embedded windows).",
            {{"managerPath", kManagerPath}, {"ewID", P("int", "embedded-window id from the embedded_windows query — NOT the node id")},
             {"toX", P("float", "")}, {"toY", P("float", "")}},
            R"({"op":"action","action":"move_embedded_window","args":{"managerPath":[],"ewID":5,"toX":300,"toY":200}})");
        d["resize_embedded_window"] = A("Resize a floating window.",
            {{"managerPath", kManagerPath}, {"ewID", P("int", "")}, {"toX", P("float", "")}, {"toY", P("float", "")},
             {"toW", P("float", "")}, {"toH", P("float", "")}},
            R"({"op":"action","action":"resize_embedded_window","args":{"managerPath":[],"ewID":5,"toX":300,"toY":200,"toW":500,"toH":400}})");

        d["add_eq_band"] = A("Add a band to a ParametricEQ node. All values are NORMALISED 0..1 (see the EQ bands concept).",
            {{"managerPath", kManagerPath}, {"nodeID", P("int", "ParametricEQ node")},
             {"bandIndex", P("int", "insert position; omit or -1 to append", false)},
             {"type", P("float", "0..1; filter = round(type*6): 0 Peaking, 1 LowShelf, 2 HighShelf, 3 LowPass, 4 HighPass, 5 BandPass, 6 Notch", false)},
             {"freq", P("float", "0..1 -> 20 Hz..20 kHz (log). 0.5 is ~632 Hz", false)},
             {"gain", P("float", "0..1 -> -18..+18 dB. 0.5 is 0 dB", false)},
             {"q", P("float", "0..1 -> 0.1..10. Default 0.1", false)}},
            R"({"op":"action","action":"add_eq_band","args":{"managerPath":[],"nodeID":4,"type":0,"freq":0.65,"gain":0.6,"q":0.2}})");
        d["remove_eq_band"] = A("Remove an EQ band. The band's state is snapshotted live for undo.",
            {{"managerPath", kManagerPath}, {"nodeID", P("int", "ParametricEQ node")}, {"bandIndex", P("int", "")}},
            R"({"op":"action","action":"remove_eq_band","args":{"managerPath":[],"nodeID":4,"bandIndex":0}})");

        d["map_parameter"] = A("Map a node parameter to an input connection (so it can be driven by a cable).",
            {{"managerPath", kManagerPath}, {"nodeID", P("int", "")}, {"paramIndex", P("int", "")},
             {"vstParamID", P("int", "for a VST node, the plugin parameter id", false)}},
            R"({"op":"action","action":"map_parameter","args":{"managerPath":[],"nodeID":3,"paramIndex":0}})");
        d["unmap_parameter"] = A("Remove a parameter mapping.",
            {{"managerPath", kManagerPath}, {"nodeID", P("int", "")}, {"paramIndex", P("int", "")},
             {"vstParamID", P("int", "", false)}},
            R"({"op":"action","action":"unmap_parameter","args":{"managerPath":[],"nodeID":3,"paramIndex":0}})");
        d["paramnode_add_mod_row"] = A("Add a modulator row to a Param node.",
            {{"managerPath", kManagerPath}, {"nodeID", P("int", "Param node")}},
            R"({"op":"action","action":"paramnode_add_mod_row","args":{"managerPath":[],"nodeID":5}})");
        d["paramnode_remove_mod_row"] = A("Remove a modulator row (snapshotted live).",
            {{"managerPath", kManagerPath}, {"nodeID", P("int", "Param node")}, {"modIndex", P("int", "")}},
            R"({"op":"action","action":"paramnode_remove_mod_row","args":{"managerPath":[],"nodeID":5,"modIndex":0}})");
        d["paramnode_toggle_centered"] = A("Toggle a modulator's centered flag. Old state read live.",
            {{"managerPath", kManagerPath}, {"nodeID", P("int", "Param node")}, {"modIndex", P("int", "")},
             {"newCentered", P("bool", "defaults to the negation of the current value", false)},
             {"newDepth", P("float", "defaults to the current depth", false)}},
            R"({"op":"action","action":"paramnode_toggle_centered","args":{"managerPath":[],"nodeID":5,"modIndex":0}})");

        d["vst_load_plugin"] = A("Load a VST3 into a VST node (the node gains its ports once loaded).",
            {{"managerPath", kManagerPath}, {"nodeID", P("int", "VST node")}, {"path", P("string", "absolute path to the .so")}},
            R"({"op":"action","action":"vst_load_plugin","args":{"managerPath":[],"nodeID":3,"path":"/home/me/.vst3/Foo.vst3/Contents/x86_64-linux/Foo.so"}})");
        d["vst_param_change"] = A("Set a VST parameter. paramID comes from the vst_params query; value is NORMALISED 0..1. The old value is read live from the plugin.",
            {{"managerPath", kManagerPath}, {"nodeID", P("int", "VST node")}, {"paramID", P("int", "stable plugin parameter id")},
             {"value", P("float", "normalised 0..1")}},
            R"({"op":"action","action":"vst_param_change","args":{"managerPath":[],"nodeID":3,"paramID":50832,"value":0.75}})");
        d["vst_state_change"] = A("Apply a whole-plugin state blob (component + controller). The prior state is read live. Blobs are base64 strings (see the vst state blobs concept).",
            {{"managerPath", kManagerPath}, {"nodeID", P("int", "VST node")},
             {"compState", P("string", "base64 component state; omit to keep the current one", false)},
             {"ctrlState", P("string", "base64 controller state; omit to keep the current one", false)}},
            R"({"op":"action","action":"vst_state_change","args":{"managerPath":[],"nodeID":3,"compState":"AAEC","ctrlState":"AwQF"}})");
        d["tempo_curve_edit"] = A("Set the project tempo curve. The X axis is BEATS and the value is BPM; the prior curve is read live, so you only supply `after`. Points outside the first/last point extend flat.",
            {{"after", P("object[]", "new points, each {timeVec (beat), v (BPM), shape, shapeParam} — see the automation curve points concept")}},
            R"({"op":"action","action":"tempo_curve_edit","args":{"after":[{"timeVec":[],"v":108,"shape":1,"shapeParam":0},{"timeVec":[[8,1]],"v":120,"shape":1,"shapeParam":0}]}})");
        d["io_port_channel"] = A("Add/remove waveform or event sockets on the Input/Output node. Add ops take no id. For a remove op, read the node with the `node` query first — Input ops look at the INPUT node's (id 1) outputs[], Output ops at the OUTPUT node's (id 0) inputs[] — then pick the LAST entry matching the op's dataType (1 = Waveform, 0 = Events) whose connected is false, and pass its id and its index. If that last port is connected, removal fails.",
            {{"managerPath", kManagerPath}, {"op", P("int", "0..7: InputAddWaveform=0, InputRemoveWaveform=1, InputAddEvent=2, InputRemoveEvent=3, OutputAddWaveform=4, OutputRemoveWaveform=5, OutputAddEvent=6, OutputRemoveEvent=7")},
             {"nodeID", P("int", "0 = Output node, 1 = Input node")},
             {"connectionId", P("int", "remove ops only: the port's id", false)},
             {"connectionIndex", P("int", "remove ops only: its 0-based index in that node's inputs[]/outputs[]", false)}},
            R"({"op":"action","action":"io_port_channel","args":{"managerPath":[],"op":4,"nodeID":0}})");

        d["sampler_add_sample"] = A("Add a sample slot to a Sampler node (optionally loading a file).",
            {{"managerPath", kManagerPath}, {"nodeID", P("int", "Sampler node")}, {"filepath", P("string", "wav/flac to load", false)}},
            R"({"op":"action","action":"sampler_add_sample","args":{"managerPath":[],"nodeID":6,"filepath":"/home/me/hit.wav"}})");
        d["sampler_remove_sample"] = A("Remove a sample slot (snapshotted live).",
            {{"managerPath", kManagerPath}, {"nodeID", P("int", "")}, {"sampleIndex", P("int", "")}},
            R"({"op":"action","action":"sampler_remove_sample","args":{"managerPath":[],"nodeID":6,"sampleIndex":0}})");
        d["sampler_reorder_samples"] = A("Reorder sample slots.",
            {{"managerPath", kManagerPath}, {"nodeID", P("int", "")}, {"newOrder", P("int[]", "permutation of slot indices")}},
            R"({"op":"action","action":"sampler_reorder_samples","args":{"managerPath":[],"nodeID":6,"newOrder":[1,0]}})");
        d["sampler_load_sample_file"] = A("Load a file into an existing sample slot. Old file info is read live.",
            {{"managerPath", kManagerPath}, {"nodeID", P("int", "")}, {"sampleIndex", P("int", "")}, {"filepath", P("string", "")}},
            R"({"op":"action","action":"sampler_load_sample_file","args":{"managerPath":[],"nodeID":6,"sampleIndex":0,"filepath":"/home/me/other.wav"}})");
        d["sampler_toggle_sample_solo"] = A("Toggle solo on a sample slot.",
            {{"managerPath", kManagerPath}, {"nodeID", P("int", "")}, {"sampleIndex", P("int", "")}},
            R"({"op":"action","action":"sampler_toggle_sample_solo","args":{"managerPath":[],"nodeID":6,"sampleIndex":0}})");
        d["sampler_toggle_sample_mute"] = A("Toggle mute on a sample slot.",
            {{"managerPath", kManagerPath}, {"nodeID", P("int", "")}, {"sampleIndex", P("int", "")}},
            R"({"op":"action","action":"sampler_toggle_sample_mute","args":{"managerPath":[],"nodeID":6,"sampleIndex":0}})");
        return d;
    }();
    return docs;
}

const json& queryDocs() {
    static const json docs = [] {
        json d = json::object();
        d["ping"] = A("Liveness/version check.", json::object(), R"({"op":"query","query":"ping"})");
        d["node_types"] = A("Every NodeType index you can pass to add_node.", json::object(),
            R"({"op":"query","query":"node_types"})");
        d["managers"] = A("Every graph in the project and its managerPath. Use these paths for the managerPath argument everywhere else.",
            json::object(), R"({"op":"query","query":"managers"})");
        d["graph"] = A("Full serialized graph for one manager: nodes, connections, IO nodes.",
            {{"managerPath", kManagerPath}}, R"({"op":"query","query":"graph","args":{"managerPath":[]}})");
        d["nodes"] = A("Node list for one manager: id, name, type, position, param count. Includes the Output node (id 0) and Input node (id 1).",
            {{"managerPath", kManagerPath}}, R"({"op":"query","query":"nodes","args":{"managerPath":[]}})");
        d["node"] = A("One node in full: geometry, parameters, and its input/output ports with their ids and connection state.",
            {{"managerPath", kManagerPath}, {"nodeID", P("int", "")}},
            R"({"op":"query","query":"node","args":{"managerPath":[],"nodeID":3}})");
        d["params"] = A("Just the parameter list of one node.",
            {{"managerPath", kManagerPath}, {"nodeID", P("int", "")}},
            R"({"op":"query","query":"params","args":{"managerPath":[],"nodeID":3}})");
        d["connections"] = A("Every cable in one manager as (srcNodeID, srcConID, dstNodeID, dstConID).",
            {{"managerPath", kManagerPath}}, R"({"op":"query","query":"connections","args":{"managerPath":[]}})");
        d["embedded_windows"] = A("Floating windows on a canvas (nodes are embedded windows) with geometry and visibility.",
            {{"managerPath", kManagerPath}}, R"({"op":"query","query":"embedded_windows","args":{"managerPath":[]}})");
        d["piano_rolls"] = A("Piano-roll windows currently open in a manager.",
            {{"managerPath", kManagerPath}}, R"({"op":"query","query":"piano_rolls","args":{"managerPath":[]}})");
        d["arranger"] = A("An Arranger node's tracks and elements together.",
            {{"managerPath", kManagerPath}, {"nodeID", P("int", "Arranger node")}},
            R"({"op":"query","query":"arranger","args":{"managerPath":[],"nodeID":2}})");
        d["tracks"] = A("Arranger tracks: {tracks:[{type, id, connectionID}], soloTracks, muteTracks, id_pool, ids}. type 0 = Audio, 1 = Notes.",
            {{"managerPath", kManagerPath}, {"nodeID", P("int", "Arranger node")}},
            R"({"op":"query","query":"tracks","args":{"managerPath":[],"nodeID":2}})");
        d["elements"] = A("Arranger elements: {id_pool, position_id_pool, elements:[{id, type, name, positions[], ...}]}. type 0 = region, 1 = audio clip, 2 = automation curve; all three share one id space. Each element lists its positions (placements on the timeline).",
            {{"managerPath", kManagerPath}, {"nodeID", P("int", "Arranger node")}},
            R"({"op":"query","query":"elements","args":{"managerPath":[],"nodeID":2}})");
        d["region"] = A("One element in full. A region also lists every note as {id, channel, pitchVector, rhythmVector, rhythmEndVector, tuning*}; any element lists its positions. Use the element's id as regionID / elementID / curveID elsewhere.",
            {{"managerPath", kManagerPath}, {"nodeID", P("int", "Arranger node")}, {"regionID", P("int", "element id")}},
            R"({"op":"query","query":"region","args":{"managerPath":[],"nodeID":2,"regionID":0}})");
        d["vst_params"] = A("Every parameter of a loaded VST: index, stable id, name, normalised value. Use the id with vst_param_change.",
            {{"managerPath", kManagerPath}, {"nodeID", P("int", "VST node with a plugin loaded")}},
            R"({"op":"query","query":"vst_params","args":{"managerPath":[],"nodeID":3}})");
        d["transport"] = A("Transport + tempo curve. beatPosition/timeSeconds are the anchor; effectiveBeatPosition/effectiveTime are the live playhead.",
            json::object(), R"({"op":"query","query":"transport"})");
        d["audio"] = A("Audio device state: sample rate, buffer size, channel counts, latency.",
            json::object(), R"({"op":"query","query":"audio"})");
        d["undo"] = A("The whole undo tree: {head, version}. Each tree node has type (ActionType int), its action-specific fields, children[], name, last_index and optionally savedMainManager (a graph snapshot). version is the child-index path from head to the current action — the exact array `goto` takes.",
            json::object(), R"({"op":"query","query":"undo"})");
        d["settings"] = A("The DAW's settings.json contents.", json::object(), R"({"op":"query","query":"settings"})");
        return d;
    }();
    return docs;
}

const json& opDocs() {
    static const json docs = [] {
        json d = json::object();
        d["ping"] = A("Liveness check.", json::object(), R"({"op":"ping"})");
        d["action"] = A("Run a registered action (undoable). See list_actions / describe_action.",
            {{"action", P("string", "action name")}, {"args", P("object", "that action's parameters")}},
            R"({"op":"action","action":"add_node","args":{"managerPath":[],"nodeType":1,"x":300,"y":200}})");
        d["query"] = A("Read DAW state. See list_queries / describe_query.",
            {{"query", P("string", "query name")}, {"args", P("object", "that query's arguments", false)}},
            R"({"op":"query","query":"nodes","args":{"managerPath":[]}})");
        d["undo"] = A("Undo one step.", json::object(), R"({"op":"undo"})");
        d["redo"] = A("Redo one step, or a specific branch.",
            {{"index", P("int", "child index to redo; omit for the default branch", false)}}, R"({"op":"redo"})");
        d["goto"] = A("Jump to a position in the undo tree.",
            {{"version", P("int[]", "child-index path from the root, [] for the root")}},
            R"({"op":"goto","version":[0,1]})");
        d["save"] = A("Save the project.",
            {{"path", P("string", "project folder; required if the project has no path yet", false)}},
            R"({"op":"save","path":"/home/me/proj"})");
        d["load"] = A("Load a project folder.",
            {{"path", P("string", "project folder containing save.json")}},
            R"({"op":"load","path":"/home/me/proj"})");
        d["transport"] = A("Transport control. seekBeat moves the playhead to an absolute beat.",
            {{"play", P("bool", "start playback", false)}, {"stop", P("bool", "stop playback", false)},
             {"seekBeat", P("float", "absolute beat to seek to", false)}},
            R"({"op":"transport","play":true,"seekBeat":0})");
        d["help"] = A("The shared concepts plus an index of every action, query and op (name + one-line summary). Use describe_action / describe_query for a full parameter list and example.",
            {{"concepts", P("bool", "include the concepts block (default true)", false)}},
            R"({"op":"help"})");
        d["list_actions"] = A("All action names with a one-line summary.", json::object(), R"({"op":"list_actions"})");
        d["describe_action"] = A("One action in full: summary, parameters and an example.",
            {{"action", P("string", "")}}, R"({"op":"describe_action","action":"create_note"})");
        d["list_queries"] = A("All query names with a one-line summary.", json::object(), R"({"op":"list_queries"})");
        d["describe_query"] = A("One query in full: summary, arguments and an example.",
            {{"query", P("string", "")}}, R"({"op":"describe_query","query":"region"})");
        d["shutdown"] = A("Quit the DAW.", json::object(), R"({"op":"shutdown"})");
        return d;
    }();
    return docs;
}
