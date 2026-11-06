#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <utility>
#include <memory>
#include <SDL3/SDL.h>
#include "idManager.h"
#include "Bus.h"
#include <nlohmann/json.hpp>
using json = nlohmann::json;

struct Note;

enum ElementType{
    region = 0,
    audioClip = 1,
    automationCurve = 2
};

class Project;
class ArrangerNode;

/** FNV-1a mixing used by element thumbnail signatures. */
constexpr uint64_t kElementHashSeed = 1469598103934665603ull;
inline uint64_t elementHashMix(uint64_t h, uint64_t v) {
    h ^= v;
    h *= 1099511628211ull;
    return h;
}

/** A text label texture cached until the string or renderer changes (same idea as
 *  NodeRender's cached port labels). */
struct CachedText {
    SDL_Texture* texture = nullptr;
    std::string text;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* get(SDL_Renderer*, const std::string&, SDL_Color);
    void clear();
};

class GridElement {
    public:

        Project* project;
        ArrangerNode* parentNode;

        GridElement(Project*, ArrangerNode*);
        virtual ~GridElement();

        struct Position {
            std::vector<std::pair<int, int>> rhythmVector;
            std::vector<std::pair<int, int>> rhythmEndVector;
            std::vector<std::pair<int, int>> startOffsetPairs;
            uint16_t trackID;
            int id;
            GridElement* element = nullptr;
            int rhythmEdoSubdivisionSteps = 1;
            std::vector<std::pair<int, int>> rhythmEdoLowerVector;
            std::vector<std::pair<int, int>> rhythmEdoUpperVector;
            std::vector<ActiveNote> dispatched;
        };

        ElementType type;

        /** User-visible name, shared by every element type. */
        std::string name;

        void createPos(std::vector<std::pair<int, int>> startPairs, std::vector<std::pair<int, int>> endPairs, uint16_t,
                       int rhythmEdoSteps, std::vector<std::pair<int, int>> rhythmEdoLower, std::vector<std::pair<int, int>> rhythmEdoUpper,
                       std::vector<std::pair<int, int>> startOffsetPairs = {});

        static json positionToJson(const Position& pos);
        static void applyPositionFromJson(Position* pos, const json& j);
        /** Returns false if id not found. Optionally returns list index before removal (for undo). */
        bool removePositionById(int positionId, size_t* removedIndex = nullptr);
        void insertPositionAt(size_t index, const json& posJson);

        std::vector<Position*> positions;

        SDL_Texture* texture = nullptr;

        virtual void draw(SDL_Renderer*, float, int) = 0;
        float pixelsPerSecond = 0;
        int h = 0;

        // Element-list preview. Each type renders itself the same way it does on the
        // timeline (notes / waveform / curve), just into a small target.
        virtual uint64_t thumbnailSignature() const { return 0; }
        virtual void drawThumbnail(SDL_Renderer*, int, int) {}

        CachedText nameLabel;
        CachedText metaLabel;
        CachedText idLabel;
        SDL_Texture* thumbTexture = nullptr;
        int thumbW = 0;
        int thumbH = 0;
        uint64_t thumbSignature = 0;
        SDL_Renderer* thumbRenderer = nullptr;
        void clearCachedTextures();

        virtual json toJSON();
        virtual void fromJSON(json);

        idManager* pos_id_pool = nullptr;

        uint16_t id;
};
