#include "UndoManager.h"
#include "JsonBytes.h"
#include "AutomationCurve.h"
#include "nodes/paramnode/paramnode.h"
#include "nodes/sampler/sampler.h"
#include "nodes/vst/vstnode.h"
#include "nodes/instrumentrack/instrumentrack.h"
#include "nodes/arranger/arranger.h"
#include "SongRoll.h"
#include "PianoRollWindow.h"
#include "GridElement.h"
#include <cmath>
#include "SDL_Events.h"
#include "styles.h"
#include <functional>
#include "Project.h"
#include "NodeProcessor.h"
#include "NodeEditor.h"
#include "nodes/nodetypes.h"
#include "NodeManager.h"
#include "InputNode.h"
#include "OutputNode.h"
#include "Note.h"
#include "PianoRoll.h"
#include <algorithm>
#include <cctype>
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include "UndoInternal.h"

// Action name registry / schema / scripted invocation (split from UndoManager.cpp).

namespace {

/// Merges caller-supplied fields into a live JSON snapshot, so a scripted edit
/// supplies intent ("pitch/rhythm/tuning change to X") rather than having to
/// echo back state it read a moment earlier.
void applyFields(json& obj, const json& intent, const char* const* fields, size_t count) {
    for (size_t i = 0; i < count; ++i)
        if (intent.contains(fields[i])) obj[fields[i]] = intent[fields[i]];
}

void applyNoteIntent(json& note, const json& intent) {
    static const char* const kFields[] = {
        "channel", "tuningMode", "tuningAnchorHarmonic", "tuningEdoSubdivisionSteps",
        "tuningEdoLowerVector", "tuningEdoUpperVector", "pitchVector",
        "rhythmVector", "rhythmEndVector", "gridSpans", "velocity",
        "tuningAnchorHarmonic", "tuningMode",
    };
    applyFields(note, intent, kFields, sizeof(kFields) / sizeof(kFields[0]));
}

void applyNoteTuningIntent(json& note, const json& intent) {
    static const char* const kFields[] = {
        "tuningMode", "tuningAnchorHarmonic", "tuningEdoSubdivisionSteps",
        "tuningEdoLowerVector", "tuningEdoUpperVector",
    };
    applyFields(note, intent, kFields, sizeof(kFields) / sizeof(kFields[0]));
}

void applyRegionTuningIntent(json& region, const json& intent) {
    static const char* const kFields[] = {
        "tuningMode", "tuningAnchorHarmonic", "tuningEdoSubdivisionSteps",
        "tuningEdoLowerVector", "tuningEdoUpperVector", "tuningHarmonicAnchorVector",
        "rhythmEdoSubdivisionSteps", "rhythmEdoLowerVector", "rhythmEdoUpperVector",
        "transportLines", "gridSpans",
    };
    applyFields(region, intent, kFields, sizeof(kFields) / sizeof(kFields[0]));
}

/// Reads a note's live snapshot, plus the index it occupies in its region.
json liveNoteJson(Project* proj, const std::vector<int>& managerPath, int nodeID, int regionID,
                  int noteID, size_t* outIndex) {
    Region& region = *undoResolveArrangerRegion(proj, managerPath, nodeID, regionID);
    auto it = region.id_to_index.find(noteID);
    if (it == region.id_to_index.end())
        throw std::runtime_error("note " + std::to_string(noteID) + " not found in region");
    const size_t idx = static_cast<size_t>(it->second);
    if (idx >= region.notes.size() || !region.notes[idx])
        throw std::runtime_error("note " + std::to_string(noteID) + " has no live object");
    if (outIndex) *outIndex = idx;
    return region.notes[idx]->toJSON();
}

json tempoPointsToJson(const AutomationCurve& c) {
    json pts = json::array();
    for (const auto& pt : c.points) {
        json jp;
        jp["timeVec"] = json::array();
        for (const auto& pr : pt.timeVec)
            jp["timeVec"].push_back(json::array({pr.first, pr.second}));
        jp["v"] = pt.v;
        jp["shape"] = static_cast<int>(pt.shape.type);
        jp["shapeParam"] = pt.shape.param;
        pts.push_back(std::move(jp));
    }
    return pts;
}

std::vector<std::pair<int, int>> readPairs(const json& j) {
    std::vector<std::pair<int, int>> out;
    if (!j.is_array()) return out;
    for (const auto& el : j)
        if (el.is_array() && el.size() >= 2)
            out.push_back({el[0].get<int>(), el[1].get<int>()});
    return out;
}

long long gcdLL(long long a, long long b) {
    a = a < 0 ? -a : a;
    b = b < 0 ? -b : b;
    while (b) { long long t = a % b; a = b; b = t; }
    return a ? a : 1;
}

/// Prime-exponent vector (positional over 2,3,5,7,...) for a rational ratio.
std::vector<std::pair<int, int>> vectorFromRatio(long long num, long long den) {
    if (num == 0)
        throw std::runtime_error("ratio must be non-zero");
    if (den < 0) { num = -num; den = -den; }
    std::map<int, int> exps;
    auto factor = [&](long long v, int sign) {
        for (long long p = 2; p * p <= v; ++p)
            while (v % p == 0) { exps[static_cast<int>(p)] += sign; v /= p; }
        if (v > 1) {
            if (v > 1000000)
                throw std::runtime_error("prime factor " + std::to_string(v) + " is too large");
            exps[static_cast<int>(v)] += sign;
        }
    };
    factor(num, 1);
    factor(den, -1);
    int maxPrime = 2;
    for (const auto& kv : exps) maxPrime = std::max(maxPrime, kv.first);
    std::vector<std::pair<int, int>> out;
    for (int p = 2; p <= maxPrime; ++p) {
        bool isPrime = true;
        for (int d = 2; d * d <= p; ++d)
            if (p % d == 0) { isPrime = false; break; }
        if (!isPrime) continue;
        auto it = exps.find(p);
        out.push_back({it == exps.end() ? 0 : it->second, 1});
    }
    while (!out.empty() && out.back().first == 0) out.pop_back();
    return out;
}

std::pair<long long, long long> parseFraction(const std::string& s) {
    const size_t slash = s.find('/');
    try {
        if (slash == std::string::npos)
            return {std::stoll(s), 1};
        return {std::stoll(s.substr(0, slash)), std::stoll(s.substr(slash + 1))};
    } catch (const std::exception&) {
        throw std::runtime_error("bad ratio '" + s + "'");
    }
}

/// Accepts an explicit int[][] vector or a token:
///   m69 / midi:69           MIDI note -> 2^((n-69)/12)
///   e7/12 / edo:7/12        EDO steps -> 2^(7/12)
///   3/2 / ji:3/2            just ratio, prime-factorised
///   h9 / harmonic:9         harmonic 9, octave-reduced (9/8)
/// A bare integer is a MIDI note; a bare fraction is a JI ratio.
std::vector<std::pair<int, int>> parsePitch(const json& j) {
    if (!j.is_string()) return readPairs(j);

    std::string t = j.get<std::string>();
    const size_t b = t.find_first_not_of(" \t\r\n");
    const size_t e = t.find_last_not_of(" \t\r\n");
    if (b == std::string::npos) throw std::runtime_error("empty pitch token");
    t = t.substr(b, e - b + 1);

    auto startsFrom = [&](const char* prefix) { return t.rfind(prefix, 0) == 0; };
    std::string body = t;
    enum { Midi, Edo, Ratio, Harmonic } kind;
    if (startsFrom("midi:"))          { kind = Midi;     body = t.substr(5); }
    else if (startsFrom("edo:"))      { kind = Edo;      body = t.substr(4); }
    else if (startsFrom("harmonic:")) { kind = Harmonic; body = t.substr(9); }
    else if (startsFrom("ratio:"))    { kind = Ratio;    body = t.substr(6); }
    else if (startsFrom("ji:"))       { kind = Ratio;    body = t.substr(3); }
    else if (t.size() > 1 && (t[0] == 'm') && (std::isdigit(static_cast<unsigned char>(t[1])) || t[1] == '-' || t[1] == '.')) { kind = Midi; body = t.substr(1); }
    else if (t.size() > 1 && t[0] == 'e' && std::isdigit(static_cast<unsigned char>(t[1]))) { kind = Edo; body = t.substr(1); }
    else if (t.size() > 1 && t[0] == 'h' && std::isdigit(static_cast<unsigned char>(t[1]))) { kind = Harmonic; body = t.substr(1); }
    else if (t.size() > 1 && t[0] == 'j' && std::isdigit(static_cast<unsigned char>(t[1]))) { kind = Ratio; body = t.substr(1); }
    else if (t.find('/') != std::string::npos) { kind = Ratio; }
    else { kind = Midi; }

    if (kind == Harmonic) {
        long long h;
        try { h = std::stoll(body); } catch (const std::exception&) {
            throw std::runtime_error("bad harmonic token '" + t + "'");
        }
        if (h <= 0 || h > 1000000)
            throw std::runtime_error("harmonic must be in 1..1000000: '" + t + "'");
        long long k = 0;
        while ((1LL << (k + 1)) <= h) ++k;
        return vectorFromRatio(h, 1LL << k);
    }

    auto [num, den] = parseFraction(body);
    if (den <= 0)
        throw std::runtime_error("bad pitch token '" + t + "' (denominator must be positive)");
    if (kind == Ratio)
        return vectorFromRatio(num, den);

    // MIDI and EDO are exponent pairs on prime 2: (n-69)/12 and steps/den.
    long long en = (kind == Midi) ? (num - 69 * den) : num;
    long long ed = (kind == Midi) ? (12 * den) : den;
    if (en == 0) return {};
    const long long g = gcdLL(en, ed);
    return {{static_cast<int>(en / g), static_cast<int>(ed / g)}};
}

/// Copies the surrounding scale onto a freshly created note, mirroring what
/// PianoRoll::stampNoteTuning does on the mouse path, and persists the result on
/// the action so redo re-applies it. Per-note tuning fields in `params` override
/// the region's.
void stampNoteTuning(Project* proj, Region* region, int noteID, const json& intent, json& outSnapshot) {
    if (!region) return;
    auto it = region->id_to_index.find(noteID);
    if (it == region->id_to_index.end()) return;
    const size_t idx = static_cast<size_t>(it->second);
    if (idx >= region->notes.size() || !region->notes[idx]) return;

    Note& n = *region->notes[idx];
    if (proj) {
        const PianoRollSettings& pr = proj->pianoRoll;
        n.tuningMode = pr.tuningMode;
        n.tuningAnchorHarmonic = pr.tuningAnchorHarmonic;
        n.tuningEdoSubdivisionSteps = pr.tuningEdoSubdivisionSteps;
        n.tuningEdoLowerVector = pr.tuningEdoLowerVector;
        n.tuningEdoUpperVector = pr.tuningEdoUpperVector;
        n.gridSpans = pr.gridSpans;
    }

    if (intent.contains("tuningMode")) n.tuningMode = intent["tuningMode"].get<int>();
    if (intent.contains("tuningAnchorHarmonic")) n.tuningAnchorHarmonic = intent["tuningAnchorHarmonic"].get<int>();
    if (intent.contains("tuningEdoSubdivisionSteps")) n.tuningEdoSubdivisionSteps = intent["tuningEdoSubdivisionSteps"].get<int>();
    if (intent.contains("tuningEdoLowerVector")) n.tuningEdoLowerVector = readPairs(intent["tuningEdoLowerVector"]);
    if (intent.contains("tuningEdoUpperVector")) n.tuningEdoUpperVector = readPairs(intent["tuningEdoUpperVector"]);
    if (intent.contains("velocity"))
        n.velocity = std::clamp(intent["velocity"].get<float>(), 0.0f, 1.0f);

    n.syncNumFromPitchVector();
    outSnapshot = n.toJSON();
}

/// Vital serializes its state as JSON inside the VST3 component-state chunk: a
/// fixed binary preamble ending in a big-endian u32 JSON length, then the JSON.
/// Rewrite one modulation-matrix slot there and rebuild the chunk lengths.
std::vector<uint8_t> vitalEditModRoute(const std::vector<uint8_t>& blob, int slot,
                                       const std::string& source,
                                       const std::string& destination,
                                       float amount, bool bipolar, bool power) {
    static const char kJsonStart[] = "{\"author\"";
    const auto it = std::search(blob.begin(), blob.end(), kJsonStart, kJsonStart + sizeof(kJsonStart) - 1);
    if (it == blob.end())
        throw std::runtime_error("vital_mod_route: not a Vital JSON state blob");
    const size_t jsonStart = static_cast<size_t>(it - blob.begin());
    if (jsonStart < 4 || jsonStart + 4 > blob.size())
        throw std::runtime_error("vital_mod_route: state preamble too short");
    const uint32_t jsonLen = (uint32_t(blob[jsonStart - 4]) << 24) | (uint32_t(blob[jsonStart - 3]) << 16) |
                             (uint32_t(blob[jsonStart - 2]) << 8) | uint32_t(blob[jsonStart - 1]);
    if (jsonLen == 0 || jsonStart + jsonLen > blob.size())
        throw std::runtime_error("vital_mod_route: bad state JSON length");

    json j = json::parse(blob.begin() + static_cast<std::ptrdiff_t>(jsonStart),
                         blob.begin() + static_cast<std::ptrdiff_t>(jsonStart + jsonLen));
    if (!j.contains("settings") || !j["settings"].contains("modulations") ||
        !j["settings"]["modulations"].is_array())
        throw std::runtime_error("vital_mod_route: state has no modulation matrix");
    json& settings = j["settings"];
    json& mods = settings["modulations"];
    if (slot < 1 || slot > static_cast<int>(mods.size()))
        throw std::runtime_error("vital_mod_route: slot out of range (1.." +
                                 std::to_string(mods.size()) + ")");

    mods[slot - 1]["source"] = source;
    mods[slot - 1]["destination"] = destination;
    const std::string prefix = "modulation_" + std::to_string(slot) + "_";
    settings[prefix + "amount"] = amount;
    settings[prefix + "bipolar"] = bipolar ? 1.0 : 0.0;
    settings[prefix + "power"] = power ? 1.0 : 0.0;

    const std::string text = j.dump();
    std::vector<uint8_t> out(blob.begin(), blob.begin() + static_cast<std::ptrdiff_t>(jsonStart));
    const uint32_t newLen = static_cast<uint32_t>(text.size());
    out[jsonStart - 4] = static_cast<uint8_t>(newLen >> 24);
    out[jsonStart - 3] = static_cast<uint8_t>(newLen >> 16);
    out[jsonStart - 2] = static_cast<uint8_t>(newLen >> 8);
    out[jsonStart - 1] = static_cast<uint8_t>(newLen);
    out.insert(out.end(), text.begin(), text.end());
    // "CcnK" container length at offset 20 counts everything after offset 24.
    if (out.size() < 24)
        throw std::runtime_error("vital_mod_route: state chunk too short");
    const uint32_t container = static_cast<uint32_t>(out.size() - 24);
    out[20] = static_cast<uint8_t>(container >> 24);
    out[21] = static_cast<uint8_t>(container >> 16);
    out[22] = static_cast<uint8_t>(container >> 8);
    out[23] = static_cast<uint8_t>(container);
    return out;
}

} // namespace

const std::unordered_map<std::string, ActionType>& UndoManager::actionRegistry() {
    static const std::unordered_map<std::string, ActionType> reg = {
        {"create_note", CreateNote},
        {"create_notes", CreateNotes},
        {"add_arranger_track", AddArrangerTrack},
        {"set_arranger_track_control", ArrangerTrackControl},
        {"add_node", AddNode},
        {"remove_node", RemoveNode},
        {"make_node_connection", MakeNodeConnection},
        {"sever_node_connection", SeverNodeConnection},
        {"reassign_node_connection", ReassignNodeConnection},
        {"remove_arranger_track", RemoveArrangerTrack},
        {"create_region", CreateRegion},
        {"delete_region", DeleteRegion},
        {"create_position", CreatePosition},
        {"delete_position", DeletePosition},
        {"move_element_position", MoveElementPosition},
        {"move_embedded_window", MoveEmbeddedWindow},
        {"resize_embedded_window", ResizeEmbeddedWindow},
        {"vst_param_change", VstParameterChange},
        {"vst_load_plugin", VstLoadPlugin},
        {"vst_state_change", VstStateChange},
        {"vital_mod_route", VitalModRoute},
        {"mixer_add_track", MixerAddTrack},
        {"toggle_piano_roll_window", TogglePianoRollWindow},
        {"create_automation_curve", CreateAutomationCurve},
        {"modify_curve_points", ModifyCurvePoints},
        {"create_audio_clip", CreateAudioClip},
        {"map_parameter", MapParameter},
        {"unmap_parameter", UnmapParameter},
        {"paramnode_add_mod_row", ParamNodeAddModRow},
        {"paramnode_remove_mod_row", ParamNodeRemoveModRow},
        {"paramnode_toggle_centered", ParamNodeToggleCentered},
        {"tempo_curve_edit", TempoCurveEdit},
        {"set_param_value", SetParamValue},
        {"move_node", MoveNode},
        {"delete_note", DeleteNote},
        {"move_note", MoveNote},
        {"move_notes", MoveMultipleNotes},
        {"pan_nodes", PanNodes},
        {"zoom_nodes", ZoomNodes},
        {"io_port_channel", IoPortChannel},
        {"songroll_rhythm_edo", SongRollRhythmEdo},
        {"piano_roll_region_tuning", PianoRollRegionTuning},
        {"assign_note_harmonic", AssignNoteHarmonic},
        {"sampler_add_sample", SamplerAddSample},
        {"sampler_remove_sample", SamplerRemoveSample},
        {"sampler_reorder_samples", SamplerReorderSamples},
        {"sampler_load_sample_file", SamplerLoadSampleFile},
        {"sampler_toggle_sample_solo", SamplerToggleSampleSolo},
        {"sampler_toggle_sample_mute", SamplerToggleSampleMute},
        {"instrument_rack_add_node", InstrumentRackInsertNode},
        {"rename_element", RenameElement},
        {"reorder_elements", ReorderElements},
        {"delete_element", DeleteElement},
    };
    return reg;
}

std::string UndoManager::actionSchema(const std::string& actionName) {
    if (actionName == "add_node") {
        return R"({"managerPath":[int,...],"nodeType":int,"x":float,"y":float})";
    }
    if (actionName == "remove_node") {
        return R"({"managerPath":[int,...],"nodeID":int})";
    }
    if (actionName == "make_node_connection" || actionName == "sever_node_connection") {
        return R"({"managerPath":[int,...],"srcNodeID":int,"srcConID":int,"dstNodeID":int,"dstConID":int})";
    }
    if (actionName == "move_node") {
        return R"({"managerPath":[int,...],"nodeID":int,"toX":float,"toY":float})";
    }
    if (actionName == "move_embedded_window") {
        return R"({"managerPath":[int,...],"ewID":int,"toX":float,"toY":float})";
    }
    if (actionName == "resize_embedded_window") {
        return R"({"managerPath":[int,...],"ewID":int,"toX":float,"toY":float,"toW":float,"toH":float})";
    }
    if (actionName == "add_arranger_track") {
        return R"({"managerPath":[int,...],"nodeID":int,"trackType":int})";
    }
    if (actionName == "set_arranger_track_control") {
        return R"({"managerPath":[int,...],"nodeID":int,"trackID":int,"control":int,"value":bool?})";
    }
    if (actionName == "create_note") {
        return R"({"managerPath":[int,...],"nodeID":int,"regionID":int,"pitchVector":[[int,int],...]|"token","rhythmVector":[[int,int],...],"rhythmEndVector":[[int,int],...]})";
    }
    if (actionName == "create_notes") {
        return R"({"managerPath":[int,...],"nodeID":int,"regionID":int,"notes":[{"pitchVector":[[int,int],...]|"token","rhythmVector":[[int,int],...],"rhythmEndVector":[[int,int],...],"intent":{...}},...]})";
    }
    if (actionName == "create_region") {
        return R"({"managerPath":[int,...],"nodeID":int})";
    }
    if (actionName == "delete_region") {
        return R"({"managerPath":[int,...],"nodeID":int,"regionID":int})";
    }
    if (actionName == "create_position") {
        return R"({"managerPath":[int,...],"nodeID":int,"elementID":int,"startPairs":[[int,int],...],"endPairs":[[int,int],...],"trackID":int})";
    }
    if (actionName == "delete_position") {
        return R"({"managerPath":[int,...],"nodeID":int,"elementID":int,"positionID":int})";
    }
    if (actionName == "move_element_position") {
        return R"({"managerPath":[int,...],"nodeID":int,"elementID":int,"positionID":int,"before":object,"after":object})";
    }
    if (actionName == "vst_param_change") {
        return R"({"managerPath":[int,...],"nodeID":int,"paramID":int,"oldValue":float,"newValue":float})";
    }
    if (actionName == "vst_load_plugin") {
        return R"({"managerPath":[int,...],"nodeID":int,"oldState":object,"newState":object})";
    }
    if (actionName == "vst_state_change") {
        return R"({"managerPath":[int,...],"nodeID":int,"oldState":object,"newState":object})";
    }
    // VSTs whose state is JSON (Vital): wire one modulation-matrix slot without the UI.
    // source/destination use the plugin's own names, e.g. env_2 / filter_1_cutoff.
    if (actionName == "vital_mod_route") {
        return R"({"managerPath":[int,...],"nodeID":int,"slot":int,"source":string,"destination":string,"amount":float?,"bipolar":bool?,"power":bool?})";
    }
    if (actionName == "toggle_piano_roll_window") {
        return R"({"managerPath":[int,...],"arrangerNodeID":int,"regionID":int,"ewID":int,"x":float,"y":float,"w":float,"h":float,"open":bool})";
    }
    if (actionName == "set_param_value")
        return R"({"managerPath":[int,...],"nodeID":int,"paramIndex":int,"value":float})";
    if (actionName == "delete_note")
        return R"({"managerPath":[int,...],"nodeID":int,"regionID":int,"noteID":int})";
    // Note edits take intent: any subset of pitchVector/rhythmVector/rhythmEndVector/channel/
    // tuning*/rhythmEdo* — the "before" snapshot is read live from the note.
    if (actionName == "move_note")
        return R"({"managerPath":[int,...],"nodeID":int,"regionID":int,"noteID":int,"intent":{...note fields...}})";
    if (actionName == "move_notes")
        return R"({"managerPath":[int,...],"nodeID":int,"regionID":int,"notes":[{"noteID":int,"intent":{...}},...]})";
    if (actionName == "pan_nodes")
        return R"({"managerPath":[int,...],"dx":float,"dy":float})";
    if (actionName == "zoom_nodes")
        return R"({"managerPath":[int,...],"amount":float,"mx":float?,"my":float?})";
    if (actionName == "io_port_channel")
        return R"({"managerPath":[int,...],"op":int,"connectionId":int?,"connectionIndex":int?})";
    if (actionName == "create_automation_curve")
        return R"({"managerPath":[int,...],"nodeID":int})";
    // `before` is read live from the curve; supply the new point list.
    if (actionName == "modify_curve_points")
        return R"({"managerPath":[int,...],"nodeID":int,"curveID":int,"points":[object,...]})";
    if (actionName == "create_audio_clip")
        return R"({"managerPath":[int,...],"nodeID":int,"filepath":string})";
    if (actionName == "tempo_curve_edit")
        return R"({"before":[object,...],"after":[object,...]})";
    // `before` is read live from the arranger node; supply the new value as `after`.
    if (actionName == "songroll_rhythm_edo")
        return R"({"managerPath":[int,...],"nodeID":int,"after":{"steps":int,"lower":[[int,int],...],"upper":[[int,int],...]}}})";
    if (actionName == "map_parameter" || actionName == "unmap_parameter")
        return R"({"managerPath":[int,...],"nodeID":int,"paramIndex":int,"vstParamID":int?})";
    if (actionName == "paramnode_add_mod_row")
        return R"({"managerPath":[int,...],"nodeID":int})";
    if (actionName == "paramnode_remove_mod_row")
        return R"({"managerPath":[int,...],"nodeID":int,"modIndex":int})";
    if (actionName == "paramnode_toggle_centered")
        return R"({"managerPath":[int,...],"nodeID":int,"modIndex":int,"newCentered":bool?,"newDepth":float?})";
    if (actionName == "sampler_add_sample")
        return R"({"managerPath":[int,...],"nodeID":int,"filepath":string?})";
    if (actionName == "sampler_remove_sample")
        return R"({"managerPath":[int,...],"nodeID":int,"sampleIndex":int})";
    if (actionName == "sampler_reorder_samples")
        return R"({"managerPath":[int,...],"nodeID":int,"newOrder":[int,...]})";
    if (actionName == "sampler_load_sample_file")
        return R"({"managerPath":[int,...],"nodeID":int,"sampleIndex":int,"filepath":string})";
    if (actionName == "sampler_toggle_sample_solo" || actionName == "sampler_toggle_sample_mute")
        return R"({"managerPath":[int,...],"nodeID":int,"sampleIndex":int})";
    // Tuning edits take intent (a subset of the region/note tuning fields); the
    // "before" snapshots are read live.
    if (actionName == "piano_roll_region_tuning")
        return R"({"managerPath":[int,...],"nodeID":int,"regionID":int,"intent":{...tuning fields...},"name":string?})";
    if (actionName == "assign_note_harmonic")
        return R"({"managerPath":[int,...],"nodeID":int,"regionID":int,"noteID":int,"intent":{...tuning fields...}})";
    if (actionName == "vst_param_change")
        return R"({"managerPath":[int,...],"nodeID":int,"paramID":int,"value":float})";
    // oldState is read live from the plugin; supply the new blobs.
    if (actionName == "vst_state_change")
        return R"({"managerPath":[int,...],"nodeID":int,"compState":bytes?,"ctrlState":bytes?})";
    if (actionName == "vst_load_plugin")
        return R"({"managerPath":[int,...],"nodeID":int,"path":string,"classIndex":int?})";
    if (actionName == "mixer_chain_add_node")
        return R"({"managerPath":[int,...],"nodeID":int,"chainIndex":int,"nodeType":int})";
    if (actionName == "instrument_rack_add_node")
        return R"({"managerPath":[int,...],"nodeID":int,"trackIndex":int,"nodeType":int})";
    if (actionName == "rename_element")
        return R"({"managerPath":[int,...],"nodeID":int,"elementID":int,"name":string})";
    if (actionName == "reorder_elements")
        return R"({"managerPath":[int,...],"nodeID":int,"afterOrder":[int,...]})";
    if (actionName == "delete_element")
        return R"({"managerPath":[int,...],"nodeID":int,"elementID":int})";
    if (actionName == "toggle_piano_roll_window")
        return R"({"managerPath":[int,...],"nodeID":int,"regionID":int,"open":bool?})";
    // Replaces whatever is on the two ports; the replaced cables are read live.
    if (actionName == "reassign_node_connection")
        return R"({"managerPath":[int,...],"srcNodeID":int,"srcConID":int,"dstNodeID":int,"dstConID":int})";
    if (actionName == "remove_arranger_track")
        return R"({"managerPath":[int,...],"nodeID":int,"trackType":int,"trackID":int,"connectionID":int})";
    return ""; // unknown action name: no schema (callers treat "" as "not described")
}

ProjectAction* UndoManager::buildRegisteredAction(const std::string& actionName, const json& params) {
    if (!head || !head->p)
        throw std::runtime_error("runRegisteredAction: undo manager not initialized");
    if (!params.is_object())
        throw std::runtime_error("runRegisteredAction: params must be a json object");
    auto it = actionRegistry().find(actionName);
    if (it == actionRegistry().end())
        throw std::runtime_error("runRegisteredAction: unknown action");
    ProjectAction* pa = nullptr;
    switch (it->second) {
        case AddNode:
            pa = new AddNodeAction(head->p, params.at("managerPath").get<std::vector<int>>(), params.at("nodeType").get<int>(),
                params.at("x").get<float>(), params.at("y").get<float>());
            break;
        case RemoveNode:
            pa = new RemoveNodeAction(head->p, params.at("managerPath").get<std::vector<int>>(), params.at("nodeID").get<int>());
            break;
        case MakeNodeConnection:
            pa = new MakeNodeConnectionAction(head->p, params.at("managerPath").get<std::vector<int>>(), params.at("srcNodeID").get<int>(),
                params.at("srcConID").get<int>(), params.at("dstNodeID").get<int>(), params.at("dstConID").get<int>());
            break;
        case SeverNodeConnection:
            pa = new SeverNodeConnectionAction(head->p, params.at("managerPath").get<std::vector<int>>(), params.at("srcNodeID").get<int>(),
                params.at("srcConID").get<int>(), params.at("dstNodeID").get<int>(), params.at("dstConID").get<int>());
            break;
        case ReassignNodeConnection: {
            // Intent: "connect src→dst, replacing whatever is on those ports". The
            // cables being replaced are read live from the two ports (at most one
            // each, since a port holds a single connection).
            auto mp = params.at("managerPath").get<std::vector<int>>();
            NodeManager& nm = requireManager(head->p, mp);
            const int sN = params.at("srcNodeID").get<int>();
            const int sC = params.at("srcConID").get<int>();
            const int dN = params.at("dstNodeID").get<int>();
            const int dC = params.at("dstConID").get<int>();
            Node* srcNode = nm.getNode(static_cast<uint16_t>(sN));
            Node* dstNode = nm.getNode(static_cast<uint16_t>(dN));
            if (!srcNode || !dstNode)
                throw std::runtime_error("reassign_node_connection: src/dst node not found");
            Connection* srcCon = srcNode->outputs.getConnection(static_cast<uint16_t>(sC));
            Connection* dstCon = dstNode->inputs.getConnection(static_cast<uint16_t>(dC));
            if (!srcCon || !dstCon)
                throw std::runtime_error("reassign_node_connection: src/dst port not found");

            ConnIDs olds[2];
            int oldCount = 0;
            if (dstCon->is_connected) {
                olds[oldCount].srcNodeID = dstCon->input_node;
                olds[oldCount].srcConID = dstCon->input_connection;
                olds[oldCount].dstNodeID = dstNode->id;
                olds[oldCount].dstConID = dstCon->id;
                olds[oldCount].existed = true;
                ++oldCount;
            }
            if (srcCon->is_connected) {
                olds[oldCount].srcNodeID = srcNode->id;
                olds[oldCount].srcConID = srcCon->id;
                olds[oldCount].dstNodeID = srcCon->output_node;
                olds[oldCount].dstConID = srcCon->output_connection;
                olds[oldCount].existed = true;
                ++oldCount;
            }
            if (oldCount == 0)
                throw std::runtime_error(
                    "reassign_node_connection: neither port is connected — use make_node_connection");
            pa = new ReassignNodeConnectionAction(head->p, std::move(mp), olds, oldCount, sN, sC, dN, dC);
            break;
        }
        case MoveEmbeddedWindow: {
            auto managerPath = params.at("managerPath").get<std::vector<int>>();
            NodeManager& nm = requireManager(head->p, managerPath);
            EmbeddedWindow* ew = nullptr;
            if (params.contains("ewID") && nm.ne)
                ew = nm.ne->getEmbeddedWindowById(params.at("ewID").get<int>());
            if (!ew)
                throw std::runtime_error("runRegisteredAction move_embedded_window: window not found");
            pa = new MoveEmbeddedWindowAction(head->p, managerPath, params.at("ewID").get<int>(),
                ew->x, ew->y, params.at("toX").get<float>(), params.at("toY").get<float>());
            break;
        }
        case ResizeEmbeddedWindow: {
            auto managerPath = params.at("managerPath").get<std::vector<int>>();
            NodeManager& nm = requireManager(head->p, managerPath);
            EmbeddedWindow* ew = nullptr;
            if (params.contains("ewID") && nm.ne)
                ew = nm.ne->getEmbeddedWindowById(params.at("ewID").get<int>());
            if (!ew)
                throw std::runtime_error("runRegisteredAction resize_embedded_window: window not found");
            pa = new ResizeEmbeddedWindowAction(head->p, managerPath, params.at("ewID").get<int>(),
                ew->x, ew->y, ew->w, ew->h,
                params.at("toX").get<float>(), params.at("toY").get<float>(),
                params.at("toW").get<float>(), params.at("toH").get<float>());
            break;
        }
        case AddArrangerTrack:
            pa = new AddArrangerTrackAction(head->p, params.at("managerPath").get<std::vector<int>>(), params.at("nodeID").get<int>(), params.at("trackType").get<int>());
            break;
        case ArrangerTrackControl: {
            auto* ac = new ArrangerTrackControlAction(head->p, params.at("managerPath").get<std::vector<int>>(),
                params.at("nodeID").get<int>(), static_cast<uint16_t>(params.at("trackID").get<int>()),
                params.at("control").get<int>());
            // Intent: omit "value" to toggle, or set an explicit state.
            if (params.contains("value") && params["value"].is_boolean())
                ac->after = params["value"].get<bool>();
            pa = ac;
            break;
        }
        case RemoveArrangerTrack:
            pa = new RemoveArrangerTrackAction(head->p, params.at("managerPath").get<std::vector<int>>(), params.at("nodeID").get<int>(), params.at("trackType").get<int>(), params.at("trackID").get<int>(), params.at("connectionID").get<int>());
            break;
        case CreateNote: {
            auto managerPath = params.at("managerPath").get<std::vector<int>>();
            requireManager(head->p, managerPath);
            std::vector<std::pair<int, int>> pairs = parsePitch(params.at("pitchVector"));
            std::vector<std::pair<int,int>> rPairs;
            if (params.contains("rhythmVector") && params["rhythmVector"].is_array()) {
                for (const auto& el : params["rhythmVector"])
                    if (el.is_array() && el.size() >= 2)
                        rPairs.push_back({el[0].get<int>(), el[1].get<int>()});
            }
            std::vector<std::pair<int,int>> rEndPairs;
            if (params.contains("rhythmEndVector") && params["rhythmEndVector"].is_array()) {
                for (const auto& el : params["rhythmEndVector"])
                    if (el.is_array() && el.size() >= 2)
                        rEndPairs.push_back({el[0].get<int>(), el[1].get<int>()});
            }
            if (rEndPairs.empty()) rEndPairs = rPairs;
            pa = new CreateNoteAction(head->p, managerPath, params.at("nodeID").get<int>(), params.at("regionID").get<int>(),
                std::move(rPairs), std::move(rEndPairs),
                std::move(pairs));
            break;
        }
        case CreateNotes: {
            auto managerPath = params.at("managerPath").get<std::vector<int>>();
            requireManager(head->p, managerPath);
            if (!params.contains("notes") || !params["notes"].is_array())
                throw std::runtime_error("create_notes: 'notes' must be an array");
            std::vector<CreateNoteSpec> specs;
            for (const auto& n : params.at("notes")) {
                CreateNoteSpec spec;
                spec.pitchVector = parsePitch(n.at("pitchVector"));
                spec.rhythmVector = readPairs(n.value("rhythmVector", json::array()));
                spec.rhythmEndVector = readPairs(n.value("rhythmEndVector", json::array()));
                if (spec.rhythmEndVector.empty()) spec.rhythmEndVector = spec.rhythmVector;
                if (n.contains("intent")) spec.intent = n["intent"];
                specs.push_back(std::move(spec));
            }
            pa = new CreateNotesAction(head->p, managerPath, params.at("nodeID").get<int>(),
                params.at("regionID").get<int>(), std::move(specs));
            break;
        }
        case CreateRegion:
            pa = new CreateRegionAction(head->p, params.at("managerPath").get<std::vector<int>>(), params.at("nodeID").get<int>());
            break;
        case DeleteRegion:
            pa = new DeleteRegionAction(head->p, params.at("managerPath").get<std::vector<int>>(), params.at("nodeID").get<int>(),
                params.at("regionID").get<int>());
            break;
        case RenameElement: {
            const auto mp = params.at("managerPath").get<std::vector<int>>();
            pa = new RenameElementAction(head->p, mp, params.at("nodeID").get<int>(),
                params.at("elementID").get<int>(), params.at("name").get<std::string>());
            break;
        }
        case ReorderElements: {
            const auto mp = params.at("managerPath").get<std::vector<int>>();
            const int nodeID = params.at("nodeID").get<int>();
            ElementManager* em = undoResolveArrangerElementManager(head->p, mp, nodeID);
            if (!em) throw std::runtime_error("reorder_elements: element manager missing");
            std::vector<uint16_t> before, after;
            for (auto* el : em->elements) before.push_back(el->id);
            for (const auto& v : params.at("afterOrder")) after.push_back(static_cast<uint16_t>(v.get<int>()));
            pa = new ElementReorderAction(head->p, mp, nodeID, std::move(before), std::move(after));
            break;
        }
        case DeleteElement: {
            const auto mp = params.at("managerPath").get<std::vector<int>>();
            pa = new DeleteElementAction(head->p, mp, params.at("nodeID").get<int>(),
                params.at("elementID").get<int>());
            break;
        }
        case CreatePosition: {
            auto readPairs = [](const json& j) {
                std::vector<std::pair<int,int>> out;
                for (const auto& el : j)
                    if (el.is_array() && el.size() >= 2)
                        out.push_back({el[0].get<int>(), el[1].get<int>()});
                return out;
            };
            pa = new CreatePositionAction(head->p, params.at("managerPath").get<std::vector<int>>(), params.at("nodeID").get<int>(),
                params.at("elementID").get<int>(), readPairs(params.at("startPairs")),
                readPairs(params.value("endPairs", json::array())),
                static_cast<uint16_t>(params.at("trackID").get<int>()));
            break;
        }
        case DeletePosition:
            pa = new DeletePositionAction(head->p, params.at("managerPath").get<std::vector<int>>(), params.at("nodeID").get<int>(),
                params.at("elementID").get<int>(), params.at("positionID").get<int>());
            break;
        case MoveElementPosition:
            pa = new MoveElementPositionAction(head->p, params.at("managerPath").get<std::vector<int>>(), params.at("nodeID").get<int>(),
                params.at("elementID").get<int>(), params.at("positionID").get<int>(), params.at("before"), params.at("after"));
            break;
        case SetParamValue: {
            auto mp = params.at("managerPath").get<std::vector<int>>();
            NodeManager& nm = requireManager(head->p, mp);
            const int nodeID = params.at("nodeID").get<int>();
            Node* target = (nodeID == 0) ? static_cast<Node*>(nm.outNode)
                         : (nodeID == 1) ? static_cast<Node*>(nm.inNode)
                                         : nm.getNode(static_cast<uint16_t>(nodeID));
            if (!target) throw std::runtime_error("set_param_value: node not found");
            const size_t idx = static_cast<size_t>(params.value("paramIndex", 0));
            if (idx >= target->params.size())
                throw std::runtime_error("set_param_value: paramIndex out of range");
            // Old value is read live — the caller supplies intent, not state.
            pa = new SetParamValueUndoAction(head->p, std::move(mp), nodeID, { idx },
                target->params[idx]->value, params.at("value").get<float>());
            break;
        }
        case MoveNode: {
            auto mp = params.at("managerPath").get<std::vector<int>>();
            NodeManager& nm = requireManager(head->p, mp);
            const int nodeID = params.at("nodeID").get<int>();
            Node* n = nm.getNode(static_cast<uint16_t>(nodeID));
            if (!n) throw std::runtime_error("move_node: node not found");
            pa = new MoveNodeAction(head->p, std::move(mp), nodeID, n->dstRect.x, n->dstRect.y,
                                    params.at("toX").get<float>(), params.at("toY").get<float>());
            break;
        }
        case PanNodes:
            pa = new PanNodesAction(head->p, params.at("managerPath").get<std::vector<int>>(),
                                    params.at("dx").get<float>(), params.at("dy").get<float>());
            break;
        case ZoomNodes:
            pa = new ZoomNodesAction(head->p, params.at("managerPath").get<std::vector<int>>(),
                                     params.at("amount").get<float>(),
                                     params.value("mx", 0.0f), params.value("my", 0.0f));
            break;
        case IoPortChannel:
            pa = new IoPortChannelAction(head->p, params.at("op").get<int>(),
                params.at("managerPath").get<std::vector<int>>(),
                static_cast<uint16_t>(params.value("connectionId", 0)),
                static_cast<size_t>(params.value("connectionIndex", 0)));
            break;
        case DeleteNote:
            pa = new DeleteNoteAction(head->p, params.at("managerPath").get<std::vector<int>>(),
                params.at("nodeID").get<int>(), params.at("regionID").get<int>(), params.at("noteID").get<int>());
            break;
        case MoveNote: {
            auto mp = params.at("managerPath").get<std::vector<int>>();
            const int nodeID = params.at("nodeID").get<int>();
            const int regionID = params.at("regionID").get<int>();
            const int noteID = params.at("noteID").get<int>();
            // before is read from the live note; the caller only supplies the change.
            json before = liveNoteJson(head->p, mp, nodeID, regionID, noteID, nullptr);
            json after = before;
            applyNoteIntent(after, params.contains("intent") ? params["intent"] : params);
            if (before == after) throw std::runtime_error("move_note: nothing to change");
            pa = new MoveNoteAction(head->p, std::move(mp), nodeID, regionID, noteID,
                std::move(before), std::move(after));
            break;
        }
        case MoveMultipleNotes: {
            auto mp = params.at("managerPath").get<std::vector<int>>();
            const int nodeID = params.at("nodeID").get<int>();
            const int regionID = params.at("regionID").get<int>();
            std::vector<int> ids;
            std::vector<json> befores, afters;
            for (const auto& e : params.at("notes")) {
                const int noteID = e.at("noteID").get<int>();
                json before = liveNoteJson(head->p, mp, nodeID, regionID, noteID, nullptr);
                json after = before;
                applyNoteIntent(after, e.contains("intent") ? e["intent"] : e);
                ids.push_back(noteID);
                befores.push_back(std::move(before));
                afters.push_back(std::move(after));
            }
            if (ids.empty()) throw std::runtime_error("move_notes: no notes given");
            pa = new MoveMultipleNotesAction(head->p, std::move(mp), nodeID, regionID,
                std::move(ids), std::move(befores), std::move(afters));
            break;
        }
        case PianoRollRegionTuning: {
            auto mp = params.at("managerPath").get<std::vector<int>>();
            const int nodeID = params.at("nodeID").get<int>();
            const int regionID = params.at("regionID").get<int>();
            Region& r = *undoResolveArrangerRegion(head->p, mp, nodeID, regionID);
            json before = r.toJSON();
            json after = before;
            applyRegionTuningIntent(after, params.contains("intent") ? params["intent"] : params);
            pa = new PianoRollRegionTuningUndoAction(head->p, std::move(mp), nodeID, regionID,
                std::move(before), std::move(after),
                params.value("name", std::string("Region Tuning")));
            break;
        }
        case AssignNoteHarmonic: {
            auto mp = params.at("managerPath").get<std::vector<int>>();
            const int nodeID = params.at("nodeID").get<int>();
            const int regionID = params.at("regionID").get<int>();
            const int noteID = params.at("noteID").get<int>();
            Region& r = *undoResolveArrangerRegion(head->p, mp, nodeID, regionID);
            const json intent = params.contains("intent") ? params["intent"] : params;
            json beforeRegion = r.toJSON();
            json afterRegion = beforeRegion;
            applyRegionTuningIntent(afterRegion, intent);
            json beforeNote = liveNoteJson(head->p, mp, nodeID, regionID, noteID, nullptr);
            json afterNote = beforeNote;
            applyNoteTuningIntent(afterNote, intent);
            pa = new AssignNoteHarmonicUndoAction(head->p, std::move(mp), nodeID, regionID, noteID,
                std::move(beforeRegion), std::move(afterRegion),
                std::move(beforeNote), std::move(afterNote));
            break;
        }
        case VstParameterChange: {
            auto mp = params.at("managerPath").get<std::vector<int>>();
            NodeManager& nm = requireManager(head->p, mp);
            const int nodeID = params.at("nodeID").get<int>();
            auto* vst = dynamic_cast<VstNode*>(nm.getNode(static_cast<uint16_t>(nodeID)));
            if (!vst || !vst->plugin)
                throw std::runtime_error("vst_param_change: node has no loaded plugin");
            const int paramID = params.at("paramID").get<int>();
            // Old value read live from the plugin, exactly like the IComponentHandler path.
            const float oldValue = vst->plugin->getParameterValue(paramID);
            pa = new VstParameterChangeAction(head->p, std::move(mp), nodeID,
                static_cast<uint32_t>(paramID), oldValue, params.at("value").get<float>());
            break;
        }
        case VstStateChange: {
            auto mp = params.at("managerPath").get<std::vector<int>>();
            NodeManager& nm = requireManager(head->p, mp);
            const int nodeID = params.at("nodeID").get<int>();
            auto* vst = dynamic_cast<VstNode*>(nm.getNode(static_cast<uint16_t>(nodeID)));
            if (!vst || !vst->plugin)
                throw std::runtime_error("vst_state_change: node has no loaded plugin");
            // Old state is the plugin's state right now; the caller supplies the new blobs.
            json oldState = json::object();
            oldState["compState"] = jsonBytesEncode(vst->plugin->getComponentState());
            oldState["ctrlState"] = jsonBytesEncode(vst->plugin->getControllerState());
            json newState = json::object();
            newState["compState"] = params.value("compState", oldState["compState"]);
            newState["ctrlState"] = params.value("ctrlState", oldState["ctrlState"]);
            pa = new VstStateChangeAction(head->p, std::move(mp), nodeID,
                std::move(oldState), std::move(newState));
            break;
        }
        case VitalModRoute: {
            auto mp = params.at("managerPath").get<std::vector<int>>();
            NodeManager& nm = requireManager(head->p, mp);
            const int nodeID = params.at("nodeID").get<int>();
            auto* vst = dynamic_cast<VstNode*>(nm.getNode(static_cast<uint16_t>(nodeID)));
            if (!vst || !vst->plugin)
                throw std::runtime_error("vital_mod_route: node has no loaded plugin");
            auto oldComp = vst->plugin->getComponentState();
            auto newComp = vitalEditModRoute(
                oldComp,
                params.at("slot").get<int>(),
                params.at("source").get<std::string>(),
                params.at("destination").get<std::string>(),
                params.value("amount", 0.5f),
                params.value("bipolar", false),
                params.value("power", true));
            json oldState = json::object();
            json newState = json::object();
            oldState["compState"] = jsonBytesEncode(oldComp);
            oldState["ctrlState"] = jsonBytesEncode(vst->plugin->getControllerState());
            newState["compState"] = jsonBytesEncode(newComp);
            newState["ctrlState"] = oldState["ctrlState"];
            pa = new VstStateChangeAction(head->p, std::move(mp), nodeID,
                std::move(oldState), std::move(newState));
            break;
        }
        case CreateAutomationCurve:
            pa = new CreateAutomationCurveAction(head->p, params.at("managerPath").get<std::vector<int>>(),
                params.at("nodeID").get<int>(),
                params.contains("points") ? params.at("points") : json::array(),
                params.value("valueRangeMin", 0.f), params.value("valueRangeMax", 1.f),
                params.value("name", std::string()));
            break;
        case ModifyCurvePoints: {
            auto mp = params.at("managerPath").get<std::vector<int>>();
            const int nodeID = params.at("nodeID").get<int>();
            const int curveID = params.at("curveID").get<int>();
            NodeManager& nm = requireManager(head->p, mp);
            auto* arr = dynamic_cast<ArrangerNode*>(nm.getNode(static_cast<uint16_t>(nodeID)));
            if (!arr || !arr->sl || !arr->elements)
                throw std::runtime_error("modify_curve_points: arranger song roll unavailable");
            auto* curve = dynamic_cast<AutomationCurve*>(
                arr->elements->getElement(static_cast<uint16_t>(curveID)));
            if (!curve) throw std::runtime_error("modify_curve_points: curve not found");
            json before = arr->sl->snapshotCurvePoints(curve);
            json after;
            if (params.contains("sourceCurveID")) {
                // Copy another curve's points, optionally shifted in time.
                auto* src = dynamic_cast<AutomationCurve*>(arr->elements->getElement(
                    static_cast<uint16_t>(params.at("sourceCurveID").get<int>())));
                if (!src) throw std::runtime_error("modify_curve_points: source curve not found");
                const auto offset = params.contains("offsetPairs")
                    ? readPairs(params.at("offsetPairs")) : std::vector<std::pair<int,int>>{};
                after = json::array();
                for (const auto& pt : src->points) {
                    json jpt;
                    jpt["timeVec"] = json::array();
                    const auto tv = addVec(pt.timeVec, offset);
                    for (const auto& pr : tv)
                        jpt["timeVec"].push_back(json::array({pr.first, pr.second}));
                    jpt["v"] = pt.v;
                    jpt["shape"] = static_cast<int>(pt.shape.type);
                    jpt["shapeParam"] = pt.shape.param;
                    after.push_back(std::move(jpt));
                }
            } else {
                after = params.contains("points") ? params.at("points") : params.value("after", before);
            }
            pa = new ModifyCurvePointsAction(head->p, std::move(mp), nodeID, curveID,
                std::move(before), std::move(after));
            break;
        }
        case CreateAudioClip:
            pa = new CreateAudioClipAction(head->p, params.at("managerPath").get<std::vector<int>>(),
                params.at("nodeID").get<int>(), params.at("filepath").get<std::string>());
            break;
        case TempoCurveEdit: {
            // Prior curve read live; the caller supplies only the new points.
            json before = tempoPointsToJson(head->p->tempoCurve);
            json after = params.contains("after") ? params.at("after") : before;
            pa = new TempoCurveEditAction(head->p, std::move(before), std::move(after));
            break;
        }
        case SongRollRhythmEdo: {
            auto mp = params.at("managerPath").get<std::vector<int>>();
            const int nodeID = params.at("nodeID").get<int>();
            NodeManager& nm = requireManager(head->p, mp);
            auto* arr = dynamic_cast<ArrangerNode*>(nm.getNode(static_cast<uint16_t>(nodeID)));
            if (!arr) throw std::runtime_error("songroll_rhythm_edo: node is not an Arranger");
            auto pairs = [](const std::vector<std::pair<int, int>>& v) {
                json a = json::array();
                for (const auto& pr : v) a.push_back(json::array({pr.first, pr.second}));
                return a;
            };
            json before;
            before["steps"] = arr->rhythmEdoSubdivisionSteps;
            before["lower"] = pairs(arr->rhythmEdoLowerVector);
            before["upper"] = pairs(arr->rhythmEdoUpperVector);
            json after = params.contains("after") ? params.at("after") : before;
            pa = new SongRollRhythmEdoAction(head->p, std::move(mp), nodeID,
                std::move(before), std::move(after));
            break;
        }
        case MapParameter:
            pa = new MapParameterUndoAction(head->p, params.at("managerPath").get<std::vector<int>>(),
                params.at("nodeID").get<int>(), static_cast<size_t>(params.value("paramIndex", 0)),
                params.value("vstParamID", -1));
            break;
        case UnmapParameter:
            pa = new UnmapParameterUndoAction(head->p, params.at("managerPath").get<std::vector<int>>(),
                params.at("nodeID").get<int>(), static_cast<size_t>(params.value("paramIndex", 0)),
                params.value("vstParamID", -1));
            break;
        case ParamNodeAddModRow:
            pa = new ParamNodeAddModRowUndoAction(head->p, params.at("managerPath").get<std::vector<int>>(),
                params.at("nodeID").get<int>());
            break;
        case ParamNodeRemoveModRow:
            pa = new ParamNodeRemoveModRowUndoAction(head->p, params.at("managerPath").get<std::vector<int>>(),
                params.at("nodeID").get<int>(), static_cast<size_t>(params.at("modIndex").get<int>()));
            break;
        case ParamNodeToggleCentered: {
            auto mp = params.at("managerPath").get<std::vector<int>>();
            NodeManager& nm = requireManager(head->p, mp);
            const int nodeID = params.at("nodeID").get<int>();
            auto* pn = dynamic_cast<ParamNode*>(nm.getNode(static_cast<uint16_t>(nodeID)));
            if (!pn) throw std::runtime_error("paramnode_toggle_centered: node is not a Param node");
            const size_t mi = static_cast<size_t>(params.at("modIndex").get<int>());
            if (mi >= pn->modulators.size())
                throw std::runtime_error("paramnode_toggle_centered: modIndex out of range");
            Modulator* mod = pn->modulators[mi];
            if (!mod) throw std::runtime_error("paramnode_toggle_centered: modulator is null");
            const bool oldCentered = mod->centered;
            const float oldDepth = mod->depth.value;
            const bool newCentered = params.contains("newCentered") ? params["newCentered"].get<bool>() : !oldCentered;
            const float newDepth = params.value("newDepth", oldDepth);
            pa = new ParamNodeToggleCenteredUndoAction(head->p, std::move(mp), nodeID, mi,
                oldCentered, newCentered, oldDepth, newDepth);
            break;
        }
        case SamplerAddSample:
            pa = new SamplerAddSampleUndoAction(head->p, params.at("managerPath").get<std::vector<int>>(),
                params.at("nodeID").get<int>(), params.value("filepath", std::string()));
            break;
        case SamplerRemoveSample:
            pa = new SamplerRemoveSampleUndoAction(head->p, params.at("managerPath").get<std::vector<int>>(),
                params.at("nodeID").get<int>(), static_cast<size_t>(params.at("sampleIndex").get<int>()));
            break;
        case SamplerReorderSamples: {
            std::vector<size_t> order;
            for (const auto& v : params.at("newOrder")) order.push_back(static_cast<size_t>(v.get<int>()));
            pa = new SamplerReorderSamplesUndoAction(head->p, params.at("managerPath").get<std::vector<int>>(),
                params.at("nodeID").get<int>(), std::move(order));
            break;
        }
        case SamplerLoadSampleFile: {
            auto mp = params.at("managerPath").get<std::vector<int>>();
            NodeManager& nm = requireManager(head->p, mp);
            const int nodeID = params.at("nodeID").get<int>();
            auto* sampler = dynamic_cast<SamplerNode*>(nm.getNode(static_cast<uint16_t>(nodeID)));
            if (!sampler) throw std::runtime_error("sampler_load_sample_file: node is not a Sampler");
            const size_t si = static_cast<size_t>(params.at("sampleIndex").get<int>());
            if (si >= sampler->samples.size())
                throw std::runtime_error("sampler_load_sample_file: sampleIndex out of range");
            const auto& slab = *sampler->samples[si];
            pa = new SamplerLoadSampleFileUndoAction(head->p, std::move(mp), nodeID, si,
                slab.filepath, params.at("filepath").get<std::string>(),
                slab.sampleRate, slab.numChannels, slab.sampleRate, slab.numChannels);
            break;
        }
        case SamplerToggleSampleSolo:
            pa = new SamplerToggleSampleSoloUndoAction(head->p, params.at("managerPath").get<std::vector<int>>(),
                params.at("nodeID").get<int>(), static_cast<size_t>(params.at("sampleIndex").get<int>()));
            break;
        case SamplerToggleSampleMute:
            pa = new SamplerToggleSampleMuteUndoAction(head->p, params.at("managerPath").get<std::vector<int>>(),
                params.at("nodeID").get<int>(), static_cast<size_t>(params.at("sampleIndex").get<int>()));
            break;
        default:
            throw std::runtime_error("runRegisteredAction: unsupported action: " + actionName);
    }
    if (!pa)
        throw std::runtime_error("runRegisteredAction: action not constructed: " + actionName);
    return pa;
}

/// The ids an action assigned to the entities it created, so a scripted caller
/// learns what it just made without guessing or diffing state.
json createdIdsFor(ProjectAction* pa) {
    json r = json::object();
    switch (pa->type) {
        case AddNode:               { auto* a = static_cast<AddNodeAction*>(pa);               r["nodeID"] = a->nodeID; break; }
        case CreateRegion:          { auto* a = static_cast<CreateRegionAction*>(pa);          r["regionID"] = a->regionID; break; }
        case CreateAutomationCurve: { auto* a = static_cast<CreateAutomationCurveAction*>(pa); r["curveID"] = a->curveID; break; }
        case CreateAudioClip:       { auto* a = static_cast<CreateAudioClipAction*>(pa);       r["clipID"] = a->clipID; break; }
        case CreateNote:            { auto* a = static_cast<CreateNoteAction*>(pa);            r["noteID"] = a->noteID; break; }
        case CreateNotes:           { auto* a = static_cast<CreateNotesAction*>(pa);
                                      r["noteIDs"] = json::array();
                                      for (const auto& spec : a->notes) r["noteIDs"].push_back(spec.noteID);
                                      break; }
        case CreatePosition:        { auto* a = static_cast<CreatePositionAction*>(pa);        r["positionID"] = a->positionID; break; }
        case AddArrangerTrack:      { auto* a = static_cast<AddArrangerTrackAction*>(pa);      r["trackID"] = a->trackID; r["connectionID"] = a->connectionID; break; }
        case SamplerAddSample:     { auto* a = static_cast<SamplerAddSampleUndoAction*>(pa);  r["sampleIndex"] = a->insertedIndex; break; }
        case IoPortChannel:         { auto* a = static_cast<IoPortChannelAction*>(pa);         r["connectionId"] = a->connectionId; r["connectionIndex"] = a->connectionIndex; break; }
        default: break;
    }
    return r;
}

bool UndoManager::runRegisteredAction(const std::string& actionName, const json& params, std::string& error,
                                      json* result) {
    error.clear();
    try {
        // A few operations are only reachable through the node/song-roll method the
        // UI itself calls, because they build and run their own action internally.
        if (actionName == "vst_load_plugin" || actionName == "toggle_piano_roll_window" ||
            actionName == "mixer_chain_add_node" || actionName == "instrument_rack_add_node" ||
            actionName == "mixer_add_track") {
            return runDelegatedAction(actionName, params, error, result);
        }

        ProjectAction* pa = buildRegisteredAction(actionName, params);
        // The UI applies these mutations itself before recording the action, so they
        // ship with skipInitialDo set. A scripted caller has applied nothing, so run
        // doAction and let the action perform the change.
        pa->skipInitialDo = false;
        newAction(pa);
        // newAction may have coalesced the action away (deleting it); only read the
        // ids back when it actually became the current node.
        if (result)
            *result = (current == pa) ? createdIdsFor(pa) : json::object();

        // A note only means something inside the scale it was placed in. The piano
        // roll stamps the surrounding tuning onto each note it creates; a scripted
        // create must do the same, or the note carries bare ratios with no scale.
        // `current == pa` guards the coalesce path, which deletes the action.
        if (current == pa) {
            if (pa->type == CreateNote) {
                auto* cn = static_cast<CreateNoteAction*>(pa);
                const json& intent = params.contains("intent") ? params["intent"] : params;
                stampNoteTuning(head->p, undoResolveArrangerRegion(head->p, cn->managerPath, cn->nodeID, cn->regionID),
                                cn->noteID, intent, cn->noteStampedSnapshot);
            } else if (pa->type == CreateNotes) {
                auto* cn = static_cast<CreateNotesAction*>(pa);
                Region* region = undoResolveArrangerRegion(head->p, cn->managerPath, cn->nodeID, cn->regionID);
                for (auto& spec : cn->notes)
                    stampNoteTuning(head->p, region, spec.noteID, spec.intent, spec.noteStampedSnapshot);
            }
        }
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    } catch (...) {
        error = "unknown error";
        return false;
    }
}

bool UndoManager::runDelegatedAction(const std::string& actionName, const json& params, std::string& error,
                                     json* result) {
    try {
        const auto managerPath = params.at("managerPath").get<std::vector<int>>();
        NodeManager& nm = requireManager(head->p, managerPath);
        const int nodeID = params.at("nodeID").get<int>();
        Node* node = nm.getNode(static_cast<uint16_t>(nodeID));
        if (!node) { error = actionName + ": node not found"; return false; }

        if (actionName == "vst_load_plugin") {
            auto* vst = dynamic_cast<VstNode*>(node);
            if (!vst) { error = "vst_load_plugin: node is not a VST node"; return false; }
            const size_t cls = params.value("classIndex", static_cast<size_t>(-1));
            vst->loadPlugin(params.at("path").get<std::string>(), /*createUndo=*/true, cls);
            return true;
        }

        if (actionName == "mixer_add_track") {
            auto* mixer = dynamic_cast<MixerNode*>(node);
            if (!mixer) { error = "mixer_add_track: node is not a Mixer node"; return false; }
            // addChain records its own "Add Mixer Chain" undo action.
            mixer->addChain(0);
            if (result) *result = json{{"added", true}};
            return true;
        }

        if (actionName == "mixer_chain_add_node") {
            auto* mixer = dynamic_cast<MixerNode*>(node);
            if (!mixer) { error = "mixer_chain_add_node: node is not a Mixer node"; return false; }
            const int chainIndex = params.at("chainIndex").get<int>();
            const int typeInt = params.at("nodeType").get<int>();
            if (typeInt < 0 || typeInt >= static_cast<int>(NodeType::Count)) {
                error = "mixer_chain_add_node: bad nodeType";
                return false;
            }
            const int addedID = mixer->addNodeToChain(chainIndex, static_cast<NodeType>(typeInt));
            if (addedID < 0) { error = "mixer_chain_add_node: could not add (bad chain index or node type)"; return false; }
            if (result) *result = json{{"nodeID", addedID}, {"chainIndex", chainIndex}};
            return true;
        }

        if (actionName == "instrument_rack_add_node") {
            auto* rack = dynamic_cast<InstrumentRackNode*>(node);
            if (!rack) { error = "instrument_rack_add_node: node is not an Instrument Rack"; return false; }
            const int trackIndex = params.at("trackIndex").get<int>();
            const int typeInt = params.at("nodeType").get<int>();
            if (typeInt < 0 || typeInt >= static_cast<int>(NodeType::Count)) {
                error = "instrument_rack_add_node: bad nodeType";
                return false;
            }
            const int addedID = rack->addNodeToTrack(trackIndex, static_cast<NodeType>(typeInt));
            if (addedID < 0) { error = "instrument_rack_add_node: could not add (bad track index or node type)"; return false; }
            if (result) *result = json{{"nodeID", addedID}, {"trackIndex", trackIndex}};
            return true;
        }

        // toggle_piano_roll_window
        auto* arr = dynamic_cast<ArrangerNode*>(node);
        if (!arr) { error = "toggle_piano_roll_window: node is not an Arranger node"; return false; }
        arr->ensureSongRoll();
        if (!arr->sl || !arr->elements) { error = "toggle_piano_roll_window: arranger has no song roll"; return false; }
        const int regionID = params.at("regionID").get<int>();
        if (!params.value("open", true)) {
            arr->sl->clearPianoRoll(regionID, /*createUndo=*/true);
            return true;
        }
        auto* region = dynamic_cast<Region*>(arr->elements->getElement(static_cast<uint16_t>(regionID)));
        if (!region) { error = "toggle_piano_roll_window: region not found"; return false; }
        arr->sl->createPianoRoll(region, /*createUndo=*/true);
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

