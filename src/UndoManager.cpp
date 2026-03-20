#include "UndoManager.h"
#include "SDL_Events.h"
#include "styles.h"
#include "Project.h"

ProjectAction* ProjectAction::deSerialize(json j, Project* p) {
    ProjectAction* pa;
    switch (j["type"].get<int>()) {
        case CreateNote: {
            auto cn = new CreateNoteAction(
                p, j["regionID"], fract::fromJSON(j["start"]), fract::fromJSON(j["length"]),
                j["pitch"], p->sm->byID(j["scaleID"])
            );
            cn->noteID = j["noteID"];
            pa = cn;
            break;
        }
        default:
            pa = new ProjectAction(p, NullAction); // head
            break;
    }

    for (auto jc : j["children"]) {
        auto c = ProjectAction::deSerialize(jc, p);
        pa->newAction(c);
    }

    pa->last_index = j["last_index"];
    pa->name = j["name"];
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
            j["noteID"] = cn->noteID;
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
    j["name"] = pa->name;
    j["last_index"] = pa->last_index;
    return j;
}

bool UndoManager::render(SDL_Renderer* renderer) {
    bool handled = renderAction(renderer, baseRect, head);
    clicked = false;
    return handled;
}

bool UndoManager::renderAction(SDL_Renderer* renderer, SDL_FRect* rect, ProjectAction* pa) {

    bool hovering = false;

    SDL_Color color;

    if (pa == current) {
        if (MouseOn(rect)) {
            color = {100, 200, 100, 255};   
        } else {
            color = {100, 255, 100, 255};
        }
    } else {
        if (MouseOn(rect)) {
            color = {255, 255, 255, 255};
        } else {
            color = {200, 200, 200, 255};
        }
    }


    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(renderer, rect);

    SDL_Texture*& texture = pa->texture;
    if (!texture) {
        SDL_Color textColor{0,0,0,255};
        SDL_Surface* surf = TTF_RenderText_Blended(fonts.mainFont, pa->name.c_str(), 0, textColor);
        texture = SDL_CreateTextureFromSurface(renderer, surf);
        SDL_DestroySurface(surf);
    }

    SDL_RenderTexture(renderer, texture, nullptr, rect);

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderRect(renderer, rect);

    if (MouseOn(rect)) hovering = true;
    if (hovering && clicked) {
        goTo(pa);
        clicked = false;
    }

    SDL_FRect subRect = *rect;
    subRect.y += subRect.h;

    for (auto c : pa->children) {
        if (renderAction(renderer, &subRect, c)) hovering = true;
        subRect.x += subRect.w;
    }

    return hovering;
}

void UndoManager::goTo(ProjectAction* target) {
    // traverse the tree to some arbitrary action node

    if (!target || !current || !head) return;

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


CreateNoteAction::CreateNoteAction(Project* p, int regionID, fract start, fract length, float pitch, TuningTable* scale) :
        ProjectAction(p, CreateNote),
        regionID(regionID),
        start(start),
        length(length),
        pitch(pitch),
        scaleID(scale->id)
        {

        doAction = [this] () {
            auto region = static_cast<Region*>(this->p->em->getElement(this->regionID));
            noteID = region->createNote(this->start, this->length, this->pitch, this->p->sm->byID(this->scaleID));
            name = "Create Note " + std::to_string(noteID) + " " + std::to_string(this->regionID);
        };

        undoAction = [this] () {
            auto region = static_cast<Region*>(this->p->em->getElement(this->regionID));
            region->deleteNote(this->noteID);
        };
}
