#include "UndoManager.h"
#include "nodes/parametriceq/parametriceq.h"
#include "nodes/vst/vstnode.h"
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
#include <stdexcept>
#include <unordered_map>
#include <vector>
#include "UndoInternal.h"

/** Each thread sets this once to the copy it owns (GUI → guiGraph, audio → dspGraph). */
static thread_local NodeManager* tls_activeManager = nullptr;

void NodeProcessor::setThreadActiveRoot(NodeManager* r) { tls_activeManager = r; }

NodeManager& requireManager(Project* p, const std::vector<int>& path) {
    if (!p || !p->processor)
        throw std::runtime_error("requireManager: project or processor missing");
    NodeManager* nm = tls_activeManager;
    for (size_t i = 0; i < path.size(); ++i) {
        int nodeId = path[i];
        auto* node = nm->getNode(static_cast<uint16_t>(nodeId));
        auto* patcher = dynamic_cast<PatcherNode*>(node);
        if (patcher && patcher->mainManager) {
            nm = patcher->mainManager;
            continue;
        }
        auto* mux = dynamic_cast<MultiplexerNode*>(node);
        if (mux) {
            ++i;
            if (i >= path.size())
                throw std::runtime_error("requireManager: multiplexer without patcher index");
            int patcherId = path[i];
            for (auto* mp : mux->patchers) {
                if (mp->id == static_cast<uint16_t>(patcherId)) {
                    nm = mp->mainManager;
                    break;
                }
            }
            continue;
        }
        throw std::runtime_error("requireManager: invalid node in managerPath");
    }
    return *nm;
}

void UndoManager::newAction(ProjectAction* pa) {
    // --- Multiplexer replication: collect all mux levels in the path,
    //     then generate the full cartesian product of sibling paths. ---
    json j = ProjectAction::serialize(pa);
    if (j.contains("managerPath")) {
        auto path = j["managerPath"].get<std::vector<int>>();

        struct MuxLevel {
            size_t pathPos;
            std::vector<int> siblingIds;
        };
        std::vector<MuxLevel> levels;

        NodeManager* nm = tls_activeManager;
        for (size_t pi = 0; pi < path.size() && nm; ++pi) {
            int nodeId = path[pi];
            auto* node = nm->getNode(static_cast<uint16_t>(nodeId));
            auto* patcher = dynamic_cast<PatcherNode*>(node);
            auto* mux2 = dynamic_cast<MultiplexerNode*>(node);
            if (mux2) {
                ++pi;
                if (pi >= path.size()) break;
                int innerPatcherId = path[pi];
                patcher = nullptr;
                for (auto* mp : mux2->patchers) {
                    if (mp->id == static_cast<uint16_t>(innerPatcherId)) {
                        patcher = mp; break;
                    }
                }
            }
            if (patcher && patcher->multiplexer) {
                MuxLevel level;
                level.pathPos = pi;
                level.siblingIds.push_back(patcher->id); // index 0 = keep original
                for (auto* sib : patcher->multiplexer->patchers)
                    if (sib != patcher) level.siblingIds.push_back(sib->id);
                levels.push_back(level);
            }
            if (patcher && patcher->mainManager)
                nm = patcher->mainManager;
            else if (!mux2)
                break;
        }

        if (!levels.empty()) {
            // Build the full cartesian product of replacement combinations.
            std::vector<std::vector<int>> siblingPaths;
            {
                std::vector<size_t> indices(levels.size(), 0);
                std::vector<size_t> maxIdx;
                for (auto& lv : levels) maxIdx.push_back(lv.siblingIds.size());
                while (true) {
                    auto sp = path;
                    for (size_t li = 0; li < levels.size(); ++li)
                        sp[levels[li].pathPos] = levels[li].siblingIds[indices[li]];
                    siblingPaths.push_back(sp);

                    // Advance indices (mixed-radix counter, skip all-zeros = source).
                    size_t li = 0;
                    while (li < levels.size()) {
                        ++indices[li];
                        if (indices[li] < maxIdx[li]) break;
                        indices[li] = 0;
                        ++li;
                    }
                    if (li >= levels.size()) break;
                }
                // Remove the all-zeros entry (it's the source path, pushed first).
                if (!siblingPaths.empty()) siblingPaths.erase(siblingPaths.begin());
            }

            Project* proj = pa->p;
            auto origDo = pa->doAction;
            auto origUndo = pa->undoAction;
            std::shared_ptr<json> postDoJson = std::make_shared<json>();

            pa->doAction = [origDo, proj, siblingPaths, postDoJson, pa]() {
                origDo();
                *postDoJson = ProjectAction::serialize(pa);
                for (auto& sp : siblingPaths) {
                    json sj = *postDoJson;
                    sj["managerPath"] = sp;
                    auto* s = ProjectAction::deSerialize(sj, proj);
                    s->doAction();
                    delete s;
                }
            };
            pa->undoAction = [origUndo, proj, siblingPaths, postDoJson]() {
                for (int i = (int)siblingPaths.size() - 1; i >= 0; --i) {
                    json sj = *postDoJson;
                    sj["managerPath"] = siblingPaths[i];
                    auto* s = ProjectAction::deSerialize(sj, proj);
                    s->undoAction();
                    delete s;
                }
                origUndo();
            };

            if (pa->type == ZoomNodes) {
                // Zoom removed — no-op propagation for backward compat.
                auto* zn = static_cast<ZoomNodesAction*>(pa);
                zn->propagateCoalesced = [](float, float, float) {};
            }

            if (pa->skipInitialDo) {
                for (auto& sp : siblingPaths) {
                    json sj = j;
                    sj["managerPath"] = sp;
                    auto* s = ProjectAction::deSerialize(sj, proj);
                    s->doAction();
                    delete s;
                }
            }
        }
    }

    // --- Normal newAction flow ---
    current->newAction(pa);
    current->last_index = pa->index;
    current = pa;
    // Apply to GUI copy (active root defaults to GUI), unless already done via direct mutation.
    if (!pa->skipInitialDo)
        pa->doAction();
    // Always enqueue for audio copy replay before next DSP pass.
    if (pa->p && pa->p->processor && pa->p->processor->dspGraph) {
        ProjectAction* cap = pa;
        enqueueAudioSync([cap]() { cap->doAction(); });
    }
}

void UndoManager::undo() {
    if (current == head) return;
    // Apply undo to GUI copy (active root is already GUI).
    current->undoAction();
    // Enqueue for audio copy (applied before next DSP callback).
    ProjectAction* cap = current;
    enqueueAudioSync([cap]() { cap->undoAction(); });
    current->parent->last_index = current->index;
    current = current->parent;
}

Region* undoResolveArrangerRegion(Project* p, const std::vector<int>& managerPath, int nodeID, int regionID) {
    NodeManager& nm = requireManager(p, managerPath);
    auto* arr = dynamic_cast<ArrangerNode*>(nm.getNode(static_cast<uint16_t>(nodeID)));
    ElementManager* em = arr ? arr->elements : nullptr;
    if (!arr || !em)
        throw std::runtime_error("undoResolveArrangerRegion: arranger or element manager missing");
    auto* r = dynamic_cast<Region*>(em->getElement(static_cast<uint16_t>(regionID)));
    if (!r)
        throw std::runtime_error("undoResolveArrangerRegion: region not found");
    return r;
}

ArrangerNode* undoResolveArrangerNode(Project* p, const std::vector<int>& managerPath, int nodeID) {
    NodeManager& nm = requireManager(p, managerPath);
    auto* arr = dynamic_cast<ArrangerNode*>(nm.getNode(static_cast<uint16_t>(nodeID)));
    if (!arr)
        throw std::runtime_error("undoResolveArrangerNode: arranger not found");
    return arr;
}

ElementManager* undoResolveArrangerElementManager(Project* p, const std::vector<int>& managerPath, int nodeID) {
    NodeManager& nm = requireManager(p, managerPath);
    auto* arr = dynamic_cast<ArrangerNode*>(nm.getNode(static_cast<uint16_t>(nodeID)));
    return arr ? arr->elements : nullptr;
}

void UndoManager::redo(int childIndex) {
    if (current->children.empty())
        return;
    int idx = childIndex;
    if (idx < 0) {
        idx = current->last_index;
        if (idx < 0 || static_cast<size_t>(idx) >= current->children.size())
            idx = static_cast<int>(current->children.size()) - 1;
        goTo(current->children[static_cast<size_t>(idx)]);
        return;
    }
    if (idx < 0 || static_cast<size_t>(idx) >= current->children.size())
        idx = static_cast<int>(current->children.size()) - 1;
    current = current->children[static_cast<size_t>(idx)];
    // Apply to GUI copy.
    current->doAction();
    // Enqueue for audio copy.
    ProjectAction* cap = current;
    enqueueAudioSync([cap]() { cap->doAction(); });
}


void UndoManager::goTo(ProjectAction* target) {
    // traverse the tree to some arbitrary action node

    if (!target || !current || !head)
        throw std::runtime_error("UndoManager::goTo: null target, current, or head");

    std::vector<int> headToCurrent;
    std::vector<int> headToTarget;

    auto tmp = current;
    while (tmp != head) {
        headToCurrent.push_back(tmp->index);
        tmp = tmp->parent;
    }
    std::reverse(headToCurrent.begin(), headToCurrent.end());

    tmp = target;
    while (tmp != head) {
        headToTarget.push_back(tmp->index);
        tmp = tmp->parent;
    }
    std::reverse(headToTarget.begin(), headToTarget.end());

    // find divergence point
    size_t d = 0;
    while (d < headToCurrent.size() && d < headToTarget.size() && headToCurrent[d] == headToTarget[d]) d++;

    // travel to divergence point

    size_t i = headToCurrent.size();
    while (i > d) {
        undo();
        i--;
    }

    // travel to target

    while (i < headToTarget.size()) {
        redo(headToTarget[i]);
        i++;
    }
}


