#include "Project.h"
#include "Region.h"
#include <memory>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <thread>
#include <chrono>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <algorithm>
#include "JsonBytes.h"
#include <stdexcept>
#include <limits>
#include "AudioManager.h"
#include "TrackManager.h"
#include "ElementManager.h"
#include "NodeProcessor.h"
#include "Settings.h"
#include "NodeManager.h"
#include "nodes/arranger/arranger.h"
#include "WindowHandler.h"
#include "WindowManager.h"
#include "ContextMenu.h"
#include <nlohmann/json.hpp>
#ifndef __EMSCRIPTEN__
#include <sndfile.h>
#endif
#include <atomic>
using json = nlohmann::json;

// Worker stack size: the undo tree serializes as nested JSON whose depth is the
// history length; nlohmann's copy/dump recurse once per level.
static constexpr size_t kWorkerStackBytes = 64u * 1024u * 1024u;

BigStackThread::~BigStackThread() {
    join();
}

void BigStackThread::join() {
    if (!active) return;
#ifdef __EMSCRIPTEN__
    if (thread.joinable()) thread.join();
#else
    pthread_join(handle, nullptr);
#endif
    active = false;
}

void BigStackThread::start(std::function<void()> fn) {
    join();
#ifdef __EMSCRIPTEN__
    thread = std::thread(std::move(fn));
    active = true;
#else
    struct Ctx { std::function<void()> fn; };
    auto* ctx = new Ctx{std::move(fn)};
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, kWorkerStackBytes);
    const int rc = pthread_create(&handle, &attr, [](void* p) -> void* {
        std::unique_ptr<Ctx> c(static_cast<Ctx*>(p));
        c->fn();
        return nullptr;
    }, ctx);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        delete ctx;
        std::cerr << "[thread] failed to start worker (pthread_create " << rc << ")" << std::endl;
        return;
    }
    active = true;
#endif
}

void Project::process(float* input, float* output, int& bufferSize, int& numChannelsIn, int& numChannelsOut, int& sampleRate) {
    if (processor) processor->process(input, output, numChannelsIn, numChannelsOut, bufferSize, sampleRate);
};

void Project::render() {
    drainRecordedTakes();
    if (processor) processor->render();
}

std::string Project::projectFolder() const {
    if (filepath.empty()) return "";
    std::filesystem::path folder(filepath);
    if (folder.is_relative())
        folder = std::filesystem::path(startupCWD) / folder;
    return folder.string();
}

std::string Project::vstStateFolder() const {
    const std::string projectF = projectFolder();
    if (!projectF.empty()) return projectF;
    // Unsaved project: keep state blobs in a persistent default folder.
    const char* home = std::getenv("HOME");
    std::filesystem::path base = home ? std::filesystem::path(home)
                                      : std::filesystem::path(startupCWD);
    return (base / ".local/share/microtonal-daw/vst-state").string();
}

json Project::externalizeVstState(json state) {
    if (!state.is_object()) return state;
    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::path(vstStateFolder()) / "vst" / "state";
    std::filesystem::create_directories(dir, ec);
    if (ec) return state;
    static std::atomic<uint64_t> seq{0};
    for (const char* key : {"compState", "ctrlState"}) {
        auto it = state.find(key);
        if (it == state.end() || !it->is_string()) continue;
        const auto bytes = jsonBytesDecode(*it);
        const uint64_t stamp = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        const std::string name = std::to_string(stamp) + "_" +
                                 std::to_string(seq.fetch_add(1)) + "_" + key + ".bin";
        const std::filesystem::path path = dir / name;
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f.is_open()) continue;
        f.write(reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
        f.close();
        if (f.fail()) continue;
        state[std::string(key) + "File"] = path.string();
        state.erase(it);
    }
    return state;
}

bool Project::readVstStateBlob(const json& state, const char* inlineKey, const char* fileKey,
                               std::vector<uint8_t>& out) const {
    out.clear();
    if (state.contains(fileKey) && state[fileKey].is_string()) {
        std::filesystem::path p(state[fileKey].get<std::string>());
        if (p.is_relative()) p = std::filesystem::path(vstStateFolder()) / p;
        std::ifstream f(p, std::ios::binary);
        if (f.is_open()) {
            out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
            return true;
        }
    }
    if (state.contains(inlineKey))
        out = jsonBytesDecode(state[inlineKey]);
    return !out.empty();
}

json Project::pianoRollToJson() const {
    json j;
    j["tuningMode"] = pianoRoll.tuningMode;
    j["tuningAnchorHarmonic"] = pianoRoll.tuningAnchorHarmonic;
    j["tuningHarmonicAnchorVector"] = pairsToJSON(pianoRoll.tuningHarmonicAnchorVector);
    j["tuningEdoSubdivisionSteps"] = pianoRoll.tuningEdoSubdivisionSteps;
    j["tuningEdoLowerVector"] = pairsToJSON(pianoRoll.tuningEdoLowerVector);
    j["tuningEdoUpperVector"] = pairsToJSON(pianoRoll.tuningEdoUpperVector);
    j["rhythmEdoSubdivisionSteps"] = pianoRoll.rhythmEdoSubdivisionSteps;
    j["rhythmEdoLowerVector"] = pairsToJSON(pianoRoll.rhythmEdoLowerVector);
    j["rhythmEdoUpperVector"] = pairsToJSON(pianoRoll.rhythmEdoUpperVector);
    j["transportLines"] = latticeSpansToJSON(pianoRoll.transportLines);
    j["gridSpans"] = latticeSpansToJSON(pianoRoll.gridSpans);
    return j;
}

void Project::pianoRollFromJson(const json& j) {
    pianoRoll = PianoRollSettings{};
    if (!j.is_object()) {
        pianoRoll.gridSpans.push_back(RhythmLatticeSpan{});
        return;
    }
    pianoRoll.tuningMode = j.value("tuningMode", 0);
    pianoRoll.tuningAnchorHarmonic = j.value("tuningAnchorHarmonic", 1);
    pianoRoll.tuningHarmonicAnchorVector = pairsFromJSON(j.value("tuningHarmonicAnchorVector", json::array()));
    pianoRoll.tuningEdoSubdivisionSteps = std::max(1, j.value("tuningEdoSubdivisionSteps", 12));
    pianoRoll.tuningEdoLowerVector = pairsFromJSON(j.value("tuningEdoLowerVector", json::array()));
    pianoRoll.tuningEdoUpperVector = pairsFromJSON(j.value("tuningEdoUpperVector", json::array()));
    pianoRoll.rhythmEdoSubdivisionSteps = std::max(1, j.value("rhythmEdoSubdivisionSteps", 1));
    pianoRoll.rhythmEdoLowerVector = pairsFromJSON(j.value("rhythmEdoLowerVector", json::array()));
    pianoRoll.rhythmEdoUpperVector = pairsFromJSON(j.value("rhythmEdoUpperVector", json::array()));
    pianoRoll.transportLines = latticeSpansFromJSON(j, "transportLines");
    pianoRoll.gridSpans = latticeSpansFromJSON(j, "gridSpans");
    if (pianoRoll.gridSpans.empty()) pianoRoll.gridSpans.push_back(RhythmLatticeSpan{});
}

void Project::pushRecordedTake(RecordedTake&& take) {
    std::lock_guard<std::mutex> lock(takeQueueMutex);
    pendingTakes.push_back(std::move(take));
}

#ifndef __EMSCRIPTEN__
static std::string writeTakeWavFile(const RecordedTake& take, const std::string& folder) {
    if (take.channels <= 0 || take.frames == 0 || take.sampleRate <= 0) return "";
    static std::atomic<unsigned> seq{0};
    std::filesystem::path dir = std::filesystem::path(folder) / "recordings";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) return "";

    // Timestamp keeps take names unique across process restarts; seq orders takes within one.
    std::string name = "rec_" + std::to_string(take.nodeID) + "_" + std::to_string(take.trackID) +
                       "_" + std::to_string(static_cast<long long>(std::time(nullptr))) + "_" +
                       std::to_string(seq.fetch_add(1)) + ".wav";
    std::filesystem::path finalPath = dir / name;
    std::filesystem::path tmpPath = dir / (name + ".tmp");

    std::vector<float> interleaved(take.frames * static_cast<size_t>(take.channels));
    for (size_t i = 0; i < take.frames; ++i)
        for (int ch = 0; ch < take.channels; ++ch)
            interleaved[i * static_cast<size_t>(take.channels) + static_cast<size_t>(ch)] =
                take.audio[static_cast<size_t>(ch) * take.frames + i];

    SF_INFO info{};
    info.samplerate = take.sampleRate;
    info.channels = take.channels;
    info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
    SNDFILE* sf = sf_open(tmpPath.c_str(), SFM_WRITE, &info);
    if (!sf) {
        std::cerr << "[record] cannot write " << tmpPath << ": " << sf_strerror(nullptr) << std::endl;
        return "";
    }
    sf_writef_float(sf, interleaved.data(), static_cast<sf_count_t>(take.frames));
    sf_close(sf);

    std::filesystem::rename(tmpPath, finalPath, ec);
    if (ec) {
        std::filesystem::remove(tmpPath, ec);
        std::cerr << "[record] rename failed for " << finalPath << std::endl;
        return "";
    }
    return finalPath.string();
}
#else
static std::string writeTakeWavFile(const RecordedTake&, const std::string&) { return ""; }
#endif

void Project::drainRecordedTakes() {
    std::vector<RecordedTake> local;
    {
        std::lock_guard<std::mutex> lock(takeQueueMutex);
        local.swap(pendingTakes);
    }
    for (auto& take : local) {
        if (take.isAudio) {
            const std::string folder = projectFolder();
            if (folder.empty()) {
                std::cerr << "[record] dropping audio take: project is not saved yet" << std::endl;
                continue;
            }
            take.filepath = writeTakeWavFile(take, folder);
            if (take.filepath.empty()) {
                std::cerr << "[record] dropping audio take: WAV write failed" << std::endl;
                continue;
            }
        }
        NodeManager* gui = processor ? processor->guiGraph : nullptr;
        ArrangerNode* arr = gui
            ? dynamic_cast<ArrangerNode*>(gui->getNode(static_cast<uint16_t>(take.nodeID)))
            : nullptr;
        if (!arr || !arr->tracks || !arr->tracks->getTrack(take.trackID)) {
            std::cerr << "[record] dropping take: arranger track no longer exists" << std::endl;
            continue;
        }

        std::cout << "[record] take node=" << take.nodeID << " track=" << take.trackID
                  << (take.isAudio ? " audio" : " notes")
                  << " " << take.startBeat << ".." << take.endBeat << std::endl;
        um->newAction(new RecordTakeAction(this, take.managerPath, take.nodeID, take.trackID, take));
    }
}

void Project::renderPresent() {
    if (processor) processor->renderPresent();
}

Project::Project() : tempoCurve(this, nullptr), dspTempoCurve(this, nullptr) {
    pianoRoll = PianoRollSettings{};
    pianoRoll.gridSpans.push_back(RhythmLatticeSpan{});
    tempoCurve.valueRangeMin = 0.001f;
    tempoCurve.valueRangeMax = 999.f;
    tempoCurve.addPoint({}, 120.f, CurveShape::Single);
    dspTempoCurve.valueRangeMin = 0.001f;
    dspTempoCurve.valueRangeMax = 999.f;
    dspTempoCurve.addPoint({}, 120.f, CurveShape::Single);

    startupCWD = std::filesystem::current_path().string();
    processor = new NodeProcessor(this);
    window = processor ? processor->getHostWindow() : nullptr;
    renderer = processor ? processor->getHostRenderer() : nullptr;

    um = new UndoManager(this);
}

AutomationCurve& Project::activeTempoCurve() {
    if (processor && NodeProcessor::getActiveRoot() == processor->dspGraph)
        return dspTempoCurve;
    return tempoCurve;
}

double& Project::activeBeatPosition() {
    return (processor && NodeProcessor::getActiveRoot() == processor->dspGraph)
        ? dspBeatPosition : beatPosition;
}

double& Project::activeTimeSeconds() {
    return (processor && NodeProcessor::getActiveRoot() == processor->dspGraph)
        ? dspTimeSeconds : timeSeconds;
}

void Project::handleWindowInput(SDL_Event& e) {
    if (processor) processor->handleWindowInput(e);
}

Project::~Project() {
    waitForSave();
    waitForLoadThread();
    if (exportThread.joinable()) exportThread.join();
    delete um;
    delete processor;
}

void Project::startAsyncLoad(const std::string& path) {
    if (loadInProgress.load()) return;

    // Expanded windows own textures created on this renderer; close them on
    // the GUI thread so the load worker never touches renderer state.
    if (processor) {
        if (auto* wm = processor->getWindowManager()) {
            while (!wm->windows().empty())
                wm->removeWindow(wm->windows().back().get());
        }
    }

    waitForLoadThread();
    loadFinished.store(false);
    loadProgressValue.store(0.f);
    loadStatusText.store("Opening project");
    loadStartMs.store(SDL_GetTicks());
    loadInProgress.store(true);
    loadThread.start([this, path] {
        // Undo deserialization resolves graphs through the thread-local manager.
        if (processor) NodeProcessor::setThreadActiveRoot(processor->guiGraph);
        load(path);
        loadFinished.store(true);
    });
}

void Project::newProject() {
    waitForLoadThread();

    // Stop the audio graph before its copy is replaced.
    if (um) {
        auto stopped = std::make_shared<std::atomic<bool>>(false);
        um->enqueueAudioSync([this, stopped]() {
            dspLoading = true;
            dspIsPlaying = false;
            stopped->store(true);
        });
        for (int i = 0; i < 250 && !stopped->load(); ++i) SDL_Delay(2);
    }

    if (processor) {
        if (auto* wm = processor->getWindowManager()) {
            while (!wm->windows().empty())
                wm->removeWindow(wm->windows().back().get());
        }
        processor->reset();
    }

    delete um;
    um = new UndoManager(this);

    filepath.clear();
    tempoCurve.valueRangeMin = 0.001f;
    tempoCurve.valueRangeMax = 999.f;
    tempoCurve.points.clear();
    tempoCurve.addPoint({}, 120.f, CurveShape::Single);
    beatPosition = 0.0;
    timeSeconds = 0.0;
    effectiveBeatPosition = 0.0;
    effectiveTime = 0.0;
    playHeadBeat = 0.0;
    playHeadStart = 0.0;
    setRecording(false);
    loading = false;
    dirty.store(false);
    loadProgressValue.store(1.0f);
    loadStatusText.store("Ready");

    const auto points = tempoCurve.points;
    um->enqueueAudioSync([this, points]() {
        dspTempoCurve.valueRangeMin = 0.001f;
        dspTempoCurve.valueRangeMax = 999.f;
        dspTempoCurve.points = points;
        dspLoading = false;
        dspIsPlaying = false;
    });
}

void Project::waitForLoadThread() {
    std::lock_guard<std::mutex> lock(loadThreadMutex);
    loadThread.join();
}

void Project::completeAsyncLoad() {
    waitForLoadThread();
    loadInProgress.store(false);
    // The worker replaced guiGraph; the GUI thread's thread-local manager still
    // points at the deleted old graph.
    if (processor) NodeProcessor::setThreadActiveRoot(processor->guiGraph);
}

void Project::waitForSave() {
    std::lock_guard<std::mutex> lock(saveThreadMutex);
    saveThread.join();
}

void Project::load(std::string path) {
    // Never carry a record arming state across projects.
    setRecording(false);

    std::filesystem::path folder(path);
    if (folder.is_relative())
        folder = std::filesystem::path(startupCWD) / folder;
    std::filesystem::path file = folder / "save.json";

    std::ifstream inFile(file);
    if (!inFile.is_open()) {
        std::cout<<"file didnt open"<<std::endl;
        return;
    }

    loading = true;
    auto stage = [this](float p, const char* text) {
        loadProgressValue.store(p, std::memory_order_relaxed);
        loadStatusText.store(text, std::memory_order_relaxed);
    };
    stage(0.02f, "Opening project");
    if (um) {
        um->enqueueAudioSync([this]() {
            dspLoading = true;
            dspIsPlaying = false;
        });
    }

    filepath = path;

    auto loadMs = [](auto t0) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
    };
    auto lt = std::chrono::steady_clock::now();
    json j;
    inFile >> j;
    std::cout << "[load] parse: " << loadMs(lt) << " ms" << std::endl;
    stage(0.10f, "Parsing project");

    // Central piano-roll lines (missing in older saves: default grid).
    pianoRollFromJson(j.contains("pianoRoll") ? j["pianoRoll"] : json::object());

    // Restore tempo curve from JSON (or fall back to legacy float, or default)
    if (j.contains("tempoCurve") && j["tempoCurve"].is_object()) {
        auto& tcj = j["tempoCurve"];
        tempoCurve.valueRangeMin = tcj.value("valueRangeMin", 0.f);
        tempoCurve.valueRangeMax = tcj.value("valueRangeMax", 999.f);
        tempoCurve.points.clear();
        if (tcj.contains("points") && tcj["points"].is_array()) {
            for (const auto& ptj : tcj["points"]) {
                CurvePoint pt;
                pt.v = ptj.value("v", 120.f);
                pt.shape.type = static_cast<CurveShape::Type>(ptj.value("shape", static_cast<int>(CurveShape::Single)));
                pt.shape.param = ptj.value("shapeParam", 0.f);
                if (ptj.contains("timeVec") && ptj["timeVec"].is_array()) {
                    for (const auto& pr : ptj["timeVec"])
                        if (pr.is_array() && pr.size() >= 2)
                            pt.timeVec.push_back({pr[0].get<int>(), pr[1].get<int>()});
                }
                tempoCurve.points.push_back(std::move(pt));
            }
        }
        if (tempoCurve.points.empty())
            tempoCurve.addPoint({}, 120.f, CurveShape::Single);
    } else {
        // Legacy: flat tempo from float field
        float legacyTempo = j.value("tempo", 120.0f);
        tempoCurve.points.clear();
        tempoCurve.addPoint({}, legacyTempo, CurveShape::Single);
    }

    // Expanded windows hold pointers to nodes that are about to be replaced by the graph
    // being loaded; close them first so their input/render paths never touch stale nodes.
    if (processor) {
        if (auto* wm = processor->getWindowManager()) {
            while (!wm->windows().empty())
                wm->removeWindow(wm->windows().back().get());
        }
    }

    // Restore undo tree first (walks to version)
    lt = std::chrono::steady_clock::now();
    um->deSerialize(j["undoManager"], this);
    std::cout << "[load] undo deserialize: " << loadMs(lt) << " ms" << std::endl;
    stage(0.24f, "Restoring history");

    // Restore NM state from current action's snapshot (falls back to legacy top-level)
    lt = std::chrono::steady_clock::now();
    stage(0.28f, "Loading instruments and effects");
    if (processor) {
        if (!um->current->savedMainManager.is_null())
            processor->deSerialize(um->current->savedMainManager);
        else if (j.contains("nodeManager"))
            processor->deSerialize(j["nodeManager"]);
    }
    std::cout << "[load] graph deserialize: " << loadMs(lt) << " ms" << std::endl;
    stage(0.93f, "Preparing audio engine");

    loading = false;
    stage(1.0f, "Ready");
    const float tempoMin = tempoCurve.valueRangeMin;
    const float tempoMax = tempoCurve.valueRangeMax;
    const auto tempoPoints = tempoCurve.points;
    if (um) {
        um->enqueueAudioSync([this, tempoMin, tempoMax, tempoPoints]() {
            dspTempoCurve.valueRangeMin = tempoMin;
            dspTempoCurve.valueRangeMax = tempoMax;
            dspTempoCurve.points = tempoPoints;
            dspLoading = false;
        });
    }
    Settings::instance().addRecentProject(folder.string());
    dirty.store(false);
}

void Project::save(uint32_t triggerWindowID, SDL_Renderer* triggerRenderer) {

    auto save_l = [this] {
        std::filesystem::path folder(this->filepath);
        if (folder.is_relative())
            folder = std::filesystem::path(this->startupCWD) / folder;
        std::error_code ec;
        std::filesystem::create_directories(folder, ec);
        if (ec) {
            std::cerr << "[save] create_directories failed: " << ec.message() << " path=" << folder << std::endl;
            return;
        }
        std::filesystem::path file = folder / "save.json";

        json j;
        // Serialize tempo curve
        json tcj;
        tcj["valueRangeMin"] = tempoCurve.valueRangeMin;
        tcj["valueRangeMax"] = tempoCurve.valueRangeMax;
        tcj["points"] = json::array();
        for (const auto& pt : tempoCurve.points) {
            json jpt;
            jpt["timeVec"] = json::array();
            for (const auto& pr : pt.timeVec)
                jpt["timeVec"].push_back(json::array({pr.first, pr.second}));
            jpt["v"] = pt.v;
            jpt["shape"] = static_cast<int>(pt.shape.type);
            jpt["shapeParam"] = pt.shape.param;
            tcj["points"].push_back(jpt);
        }
        j["tempoCurve"] = std::move(tcj);
        j["pianoRoll"] = pianoRollToJson();

        auto msSince = [](auto t0) {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0).count();
        };
        // Snapshot NM state onto current action before serializing the tree
        auto t0 = std::chrono::steady_clock::now();
        if (this->processor && this->um && this->um->current)
            this->um->current->savedMainManager = this->processor->serialize();
        std::cout << "[save] graph serialize: " << msSince(t0) << " ms" << std::endl;

        t0 = std::chrono::steady_clock::now();
        j["undoManager"] = this->um->serialize();
        std::cout << "[save] undo tree serialize: " << msSince(t0) << " ms" << std::endl;

        // VST states live in sidecar files, not inside save.json, and the file writing runs
        // on a worker thread so the GUI keeps pumping events (Space still pauses) while a
        // large project is written. The thread is joinable, not detached: waitForSave()
        // joins it at shutdown, otherwise exit() kills it mid-write.
        std::lock_guard<std::mutex> threadLock(saveThreadMutex);
        saveThread.join();
        saveThread.start([j = std::move(j), folder]() mutable {
            // Self-contained snapshot (VST states still inline) kept as a version file.
            // save.json references shared sidecars that later saves rewrite, so it is not
            // a durable copy; this one is.
            json archive = j;
            std::error_code wec;
            std::filesystem::path vstDir = folder / "vst";
            std::filesystem::create_directories(vstDir, wec);
            // Move inline VST blobs into <base>/vst/N.bin and leave compStateFile/ctrlStateFile
            // refs behind. Each target folder gets its own numbered set of sidecars.
            std::function<void(json&, const std::filesystem::path&, int&)> externalizeInto =
                [&](json& o, const std::filesystem::path& base, int& counter) {
                if (o.is_object()) {
                    for (const char* key : {"compState", "ctrlState"}) {
                        const std::string fileKey = std::string(key) + "File";
                        std::vector<uint8_t> bytes;
                        bool have = false;
                        auto inlineIt = o.find(key);
                        if (inlineIt != o.end() && inlineIt->is_string()) {
                            bytes = jsonBytesDecode(*inlineIt);
                            have = true;
                        } else {
                            // State is already out-of-line; copy the referenced file
                            // so version archives stay self-contained.
                            auto fileIt = o.find(fileKey);
                            if (fileIt != o.end() && fileIt->is_string()) {
                                std::filesystem::path p(fileIt->get<std::string>());
                                if (p.is_relative()) p = folder / p;
                                // Already archived inside this save's folder: keep the
                                // reference and skip the copy. Re-copying every snapshot
                                // on every save duplicated the whole undo tree's state.
                                std::error_code cec;
                                auto canonP = std::filesystem::weakly_canonical(p, cec);
                                auto canonBase = std::filesystem::weakly_canonical(base, cec);
                                if (!cec && canonP.string().rfind(canonBase.string(), 0) == 0)
                                    continue;
                                std::ifstream in(p, std::ios::binary);
                                if (in.is_open()) {
                                    bytes.assign(std::istreambuf_iterator<char>(in),
                                                 std::istreambuf_iterator<char>());
                                    have = true;
                                }
                            }
                        }
                        if (!have) continue;
                        const std::string rel = "vst/" + std::to_string(counter++) + ".bin";
                        // Write to a temp file and rename, so an interrupted save can
                        // never leave a truncated sidecar behind.
                        std::filesystem::path relPath = rel;
                        std::filesystem::path tmpPath = base / relPath;
                        tmpPath += ".tmp";
                        std::ofstream f(tmpPath, std::ios::binary | std::ios::trunc);
                        if (f.is_open()) {
                            f.write(reinterpret_cast<const char*>(bytes.data()),
                                    static_cast<std::streamsize>(bytes.size()));
                            f.close();
                            std::error_code rec;
                            std::filesystem::rename(tmpPath, base / relPath, rec);
                        }
                        o[fileKey] = rel;
                        if (inlineIt != o.end()) o.erase(inlineIt);
                    }
                    for (auto& el : o.items())
                        externalizeInto(el.value(), base, counter);
                } else if (o.is_array()) {
                    for (auto& el : o)
                        externalizeInto(el, base, counter);
                }
            };
            auto tw = std::chrono::steady_clock::now();
            int vstFileCounter = 0;
            externalizeInto(j, folder, vstFileCounter);
            std::cout << "[save] externalize: " << std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - tw).count() << " ms" << std::endl;

            tw = std::chrono::steady_clock::now();
            // Atomic replace: dump to save.json.tmp, then rename over save.json. Opening
            // save.json directly truncates it, so a save killed mid-write destroys the
            // previous good file.
            const std::filesystem::path finalPath = folder / "save.json";
            const std::filesystem::path tmpPath = folder / "save.json.tmp";
            std::ofstream outFile(tmpPath, std::ios::binary | std::ios::trunc);
            bool mainWrote = false;
            if (outFile.is_open()) {
                outFile << j.dump();
                outFile.flush();
                const bool ok = !outFile.fail();
                outFile.close();
                std::error_code rec;
                if (ok) std::filesystem::rename(tmpPath, finalPath, rec);
                mainWrote = ok && !rec;
                if (mainWrote) {
                    std::cout << "[save] wrote " << finalPath << " (dump+write "
                              << std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - tw).count() << " ms)" << std::endl;
                } else {
                    std::cerr << "[save] WRITE FAILED for " << finalPath
                              << (rec ? (": " + rec.message()) : std::string()) << std::endl;
                }
            } else {
                std::cerr << "[save] failed to open " << tmpPath << std::endl;
            }

            // Sidecar numbering restarts at 0 each save, so files above this save's count
            // are orphans from an earlier, larger project. Version folders hold their own
            // copies, so the live folder only needs the ones save.json references.
            if (mainWrote) {
                for (const auto& entry : std::filesystem::directory_iterator(vstDir, wec)) {
                    if (!entry.is_regular_file()) continue;
                    const std::string fn = entry.path().filename().string();
                    if (fn.size() >= 4 && fn.compare(fn.size() - 4, 4, ".tmp") == 0) {
                        std::filesystem::remove(entry.path(), wec);
                        continue;
                    }
                    if (fn.size() <= 4 || fn.compare(fn.size() - 4, 4, ".bin") != 0) continue;
                    const std::string num = fn.substr(0, fn.size() - 4);
                    if (num.empty() ||
                        !std::all_of(num.begin(), num.end(), [](unsigned char c) { return std::isdigit(c); }))
                        continue;
                    if (std::atoi(num.c_str()) >= vstFileCounter)
                        std::filesystem::remove(entry.path(), wec);
                }
            }

            // Never-overwritten version copy: versions/save_v<seq>_<UTC stamp>/, a complete
            // project folder with its own save.json and stamped vst/*.bin sidecars.
            std::filesystem::path versionsDir = folder / "versions";
            std::filesystem::create_directories(versionsDir, wec);
            int nextVersion = 1;
            for (const auto& entry : std::filesystem::directory_iterator(versionsDir, wec)) {
                const std::string fn = entry.path().filename().string();
                if (fn.rfind("save_v", 0) == 0) {
                    const int v = std::atoi(fn.c_str() + 6);
                    if (v >= nextVersion) nextVersion = v + 1;
                }
            }
            std::time_t nowT = std::time(nullptr);
            std::tm tmUtc{};
            gmtime_r(&nowT, &tmUtc);
            char stamp[32] = {};
            std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tmUtc);
            char verName[128] = {};
            std::snprintf(verName, sizeof(verName), "save_v%04d_%s", nextVersion, stamp);
            const std::filesystem::path verDir = versionsDir / verName;
            std::filesystem::create_directories(verDir / "vst", wec);
            int verCounter = 0;
            externalizeInto(archive, verDir, verCounter);

            // Copy the rest of the project folder (recorded WAVs etc.) into the version so it
            // is self-contained, then point the archived clip paths at the copies.
            {
                std::error_code cec;
                for (std::filesystem::recursive_directory_iterator it(
                         folder, std::filesystem::directory_options::skip_permission_denied, cec), end;
                     !cec && it != end; it.increment(cec)) {
                    const std::filesystem::path rel = it->path().lexically_relative(folder);
                    if (rel.empty() || rel == ".") continue;
                    auto first = rel.begin();
                    if (first != rel.end() && (*first == "versions" || *first == "vst")) {
                        if (it->is_directory()) it.disable_recursion_pending();
                        continue;
                    }
                    if (!it->is_regular_file()) continue;
                    const std::string fn = it->path().filename().string();
                    if (fn == "save.json" || fn == "save.json.tmp") continue;
                    if (fn.size() >= 4 && fn.compare(fn.size() - 4, 4, ".tmp") == 0) continue;
                    const std::filesystem::path dest = verDir / rel;
                    std::error_code dec;
                    std::filesystem::create_directories(dest.parent_path(), dec);
                    std::filesystem::copy_file(it->path(), dest,
                        std::filesystem::copy_options::overwrite_existing, dec);
                }

                std::error_code canonEc;
                const std::filesystem::path folderCanon =
                    std::filesystem::weakly_canonical(folder, canonEc);
                if (!canonEc) {
                    std::function<void(json&)> rewritePaths = [&](json& node) {
                        if (node.is_object()) {
                            for (auto itj = node.begin(); itj != node.end(); ++itj) {
                                if (itj.key() == "filepath" && itj.value().is_string()) {
                                    const std::string s = itj.value().get<std::string>();
                                    if (s.empty()) continue;
                                    std::filesystem::path p(s);
                                    if (p.is_relative()) p = folder / p;
                                    std::error_code pec;
                                    auto canon = std::filesystem::weakly_canonical(p, pec);
                                    if (pec) continue;
                                    auto rel = canon.lexically_relative(folderCanon);
                                    if (rel.empty() || rel == ".") continue;
                                    bool escaped = false;
                                    for (const auto& part : rel)
                                        if (part == "..") { escaped = true; break; }
                                    if (escaped) continue;
                                    auto first = rel.begin();
                                    if (first != rel.end() && *first == "versions") continue;
                                    const std::filesystem::path copied = verDir / rel;
                                    std::error_code xec;
                                    if (!std::filesystem::exists(copied, xec)) continue;
                                    itj.value() = std::filesystem::absolute(copied).string();
                                } else {
                                    rewritePaths(itj.value());
                                }
                            }
                        } else if (node.is_array()) {
                            for (auto& el : node) rewritePaths(el);
                        }
                    };
                    rewritePaths(archive);
                }
            }

            const std::filesystem::path verFinal = verDir / "save.json";
            std::filesystem::path verTmp = verFinal;
            verTmp += ".tmp";
            std::ofstream verFile(verTmp, std::ios::binary | std::ios::trunc);
            if (verFile.is_open()) {
                verFile << archive.dump();
                verFile.flush();
                const bool vok = !verFile.fail();
                verFile.close();
                std::error_code vrec;
                if (vok) std::filesystem::rename(verTmp, verFinal, vrec);
                if (vok && !vrec)
                    std::cout << "[save] version " << verName << std::endl;
                else
                    std::cerr << "[save] version write failed: " << verFinal << std::endl;
            } else {
                std::cerr << "[save] failed to open " << verTmp << std::endl;
            }
        });
        dirty.store(false);
    };

    const bool uiSaveRequested = (triggerWindowID != 0) || (triggerRenderer != nullptr);
    if (filepath.empty() && !uiSaveRequested) {
        std::cout << "[save] filepath is empty; use Ctrl+S in a node window to choose a save path." << std::endl;
        return;
    }

    if (filepath.empty()) {
        auto ctxMenu = ContextMenu::get();
        if (this->window) {
            SDL_RaiseWindow(this->window);
            SDL_SetWindowKeyboardGrab(this->window, true);
        }
        ctxMenu->activate(SDL_GetRenderer(this->window), SDL_GetWindowID(this->window));
        if (this->window) SDL_StartTextInput(this->window);
        int w = 0, h = 0;
        if (this->window) SDL_GetWindowSize(this->window, &w, &h);
        ctxMenu->locX = w * 0.5f;
        ctxMenu->locY = h * 0.5f;


        ctxMenu->dynamicTick = getTextInputTicker(
            [this, save_l] (std::string text) {
                std::filesystem::path p(text);
                if (p.is_relative())
                    p = std::filesystem::path(this->startupCWD) / p;
                this->filepath = p.string();
                save_l();
            },
            [this] () {
                if (this->window) SDL_SetWindowKeyboardGrab(this->window, false);
            }
        );
    
    } else save_l();
}

void Project::seekBeat(double beat) {
    beatPosition = beat;
    effectiveBeatPosition = beat;
    const double sec = static_cast<double>(tempoCurve.secondsForBeats(0.f, static_cast<float>(beat)));
    effectiveTime = sec;
    timeSeconds = sec;
    playHeadStart = sec;
    playHeadBeat = beat;
    syncTransportToAudio(isPlaying, beat, sec);
}

bool Project::exportRange(double startBeat, double endBeat, const std::string& path) {
    AudioManager* am = AudioManager::instance();
    if (!am || !processor || endBeat <= startBeat) return false;
    if (exportInProgress.exchange(true)) return false; // already exporting
    if (exportThread.joinable()) exportThread.join();
    exportProgressValue.store(0.f);

    exportThread = std::thread([this, am, startBeat, endBeat, path]() {
    // The audio thread stops looping; this worker calls the audio callback
    // itself instead, streaming each block to the file.
    am->stop();
    seekBeat(startBeat);
    if (!isPlaying) togglePlaying();

    // Same block size the graph was set up for: VST buffers are sized from the
    // device buffer, so a larger manual block overruns them (garbage output).
    const unsigned int bs = am->bufferSize > 0 ? am->bufferSize : 256;
    const int outCh = static_cast<int>(am->outputChannels);
    const int wavCh = 2;
    std::vector<float> out(static_cast<size_t>(bs) * static_cast<size_t>(outCh), 0.f);
    std::vector<float> inter(static_cast<size_t>(bs) * wavCh, 0.f);
    std::vector<float> pcm; // interleaved; normalized to -1 dBFS before writing

    const double span = endBeat - startBeat;
    double lastBeat = dspBeatPosition;
    bool stalled = false;
    sf_count_t blocksWritten = 0;
    while (dspBeatPosition < endBeat - 1e-9) {
        std::fill(out.begin(), out.end(), 0.f);
        AudioManager::callback(out.data(), nullptr, bs, 0.0, 0, am);
        for (unsigned int i = 0; i < bs; ++i) {
            inter[static_cast<size_t>(i) * 2] = out[static_cast<size_t>(i)];
            inter[static_cast<size_t>(i) * 2 + 1] = out[static_cast<size_t>(bs) + i];
        }
        pcm.insert(pcm.end(), inter.begin(), inter.end());
        ++blocksWritten;
        exportProgressValue.store(static_cast<float>(
            std::clamp((dspBeatPosition - startBeat) / span, 0.0, 1.0)));
        if (dspBeatPosition <= lastBeat) { stalled = true; break; }
        lastBeat = dspBeatPosition;
    }

    stop();
    am->start();

    // Auto-normalize: peak to -1 dBFS.
    float peak = 0.f;
    for (float v : pcm) peak = std::max(peak, std::fabs(v));
    if (peak > 0.f) {
        const float gain = 0.8912509f / peak;
        for (float& v : pcm) v *= gain;
    }

    SF_INFO info{};
    info.samplerate = static_cast<int>(am->sampleRate);
    info.channels = wavCh;
    info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
    SNDFILE* sf = sf_open(path.c_str(), SFM_WRITE, &info);
    if (sf) {
        sf_writef_float(sf, pcm.data(), static_cast<sf_count_t>(pcm.size() / wavCh));
        sf_close(sf);
    }

    if (!sf) {
        std::cerr << "[export] failed to open " << path << std::endl;
    } else if (stalled) {
        std::cerr << "[export] transport did not advance (beat " << dspBeatPosition
                  << "); aborted after " << blocksWritten << " block(s): " << path << std::endl;
    }
    exportProgressValue.store(1.f);
    exportInProgress.store(false);
    });
    return true;
}

void Project::createNote(int nodeID, std::vector<std::pair<int,int>> startPairs, std::vector<std::pair<int,int>> endPairs, int regionID, std::vector<int> managerPath,
                         std::vector<std::pair<int, int>> pitchVector) {
    auto pa = new CreateNoteAction(this, std::move(managerPath), nodeID, regionID, std::move(startPairs), std::move(endPairs),
                                   std::move(pitchVector));
    um->newAction(pa);
}

void Project::deleteNote(int nodeID, int regionID, int noteID, std::vector<int> managerPath) {
    auto pa = new DeleteNoteAction(this, std::move(managerPath), nodeID, regionID, noteID);
    um->newAction(pa);
}

void Project::advanceBeatPosition(double dtSec) {
    if (dtSec <= 0.0) {
        deltaBeats = 0.0;
        return;
    }

    double b = activeBeatPosition();
    auto& tc = activeTempoCurve();

    // Binary search db such that secondsForBeats(b, b+db) == dtSec
    double lo = 0.0, hi = dtSec * 999.0 / 60.0;
    for (int iter = 0; iter < 20; iter++) {
        double mid = (lo + hi) * 0.5;
        float secs = tc.secondsForBeats(
            static_cast<float>(b), static_cast<float>(b + mid));
        if (static_cast<double>(secs) < dtSec) lo = mid;
        else hi = mid;
    }
    deltaBeats = hi;
}

void Project::setRecording(bool on) {
    isRecording = on;
    if (!um) return;
    // Deliberately separate from syncTransportToAudio: toggling record must not bump
    // dspTransportGeneration (that would flush every active voice).
    um->enqueueAudioSync([this, on]() { dspRecording = on; });
}

void Project::syncTransportToAudio(bool playing, double beat, double seconds) {
    if (!um) return;
    um->enqueueAudioSync([this, playing, beat, seconds]() {
        dspIsPlaying = playing;
        dspBeatPosition = beat;
        dspTimeSeconds = seconds;
        ++dspTransportGeneration;
    });
}

int Project::allocateDspVoiceId() {
    if (dspNextVoiceId <= 0) dspNextVoiceId = 1;
    const int id = dspNextVoiceId;
    dspNextVoiceId = (dspNextVoiceId == std::numeric_limits<int>::max())
        ? 1 : dspNextVoiceId + 1;
    return id;
}

void Project::togglePlaying() {
    if (!isPlaying) {
        // Start exactly at the playhead. The DSP dispatches a block at its start
        // position before advancing (and chases notes already sounding), so notes
        // on the start beat do not need the transport pulled back.
        const double sec = static_cast<double>(tempoCurve.secondsForBeats(0.f, static_cast<float>(playHeadBeat)));
        beatPosition = playHeadBeat;
        timeSeconds = sec;
        isPlaying = true;
        syncTransportToAudio(true, playHeadBeat, sec);
    } else {
        isPlaying = false;
        beatPosition = playHeadBeat;
        double sec = static_cast<double>(tempoCurve.secondsForBeats(0.f, static_cast<float>(playHeadBeat)));
        timeSeconds = sec;
        syncTransportToAudio(false, playHeadBeat, sec);
    }
}


void Project::stop() {
    if(isPlaying) {
        isPlaying = false;
        beatPosition = playHeadBeat;
        double sec = static_cast<double>(tempoCurve.secondsForBeats(0.f, static_cast<float>(playHeadBeat)));
        timeSeconds = sec;
        syncTransportToAudio(false, playHeadBeat, sec);
    }
}

void Project::tick() {

}

void Project::setup() {
}
