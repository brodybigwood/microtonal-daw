#include "UndoManager.h"

ProjectAction* ProjectAction::deSerialize(json j) {
    ProjectAction* pa;
    switch (j["type"].get<int>()) {
        case CreateNote:
            pa = new CreateNoteAction(
                j["regionID"], fract::fromJSON(j["start"]), fract::fromJSON(j["length"]),
                j["pitch"], ScaleManager::instance()->byID(j["scaleID"])
            );
            break;
        default:
            pa = new ProjectAction(NullAction); // head
            break;
    }

    for (auto jc : j["children"]) {
        auto c = ProjectAction::deSerialize(jc);
        pa->newAction(c);
    }
    return pa;
}

json ProjectAction::serialize(ProjectAction* pa) {
    json j;
    j["type"] = pa->type;
    switch (pa->type) {
        case CreateNote: {
            auto cn = static_cast<CreateNoteAction*>(pa);
            j["regionID"] = cn->regionID;
            j["start"] = cn->start.toJSON();
            j["length"] = cn->length.toJSON();
            j["pitch"] = cn->pitch;
            j["scaleID"] = cn->scaleID;
            break;
        }
        default:
            break;
}

    json children = json::array();
    for (auto c : pa->children) {
        json jc = ProjectAction::serialize(c);
        children.push_back(jc);
    }

    j["children"] = children;
    return j;
}
