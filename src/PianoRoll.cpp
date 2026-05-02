#include "PianoRoll.h"

#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_stdinc.h>
#include <algorithm>
#include <cmath>
#include <cfloat>
#include <climits>
#include <cstdlib>
#include <numeric>
#include <sstream>
#include <iomanip>
#include <SDL3/SDL.h>
#include "styles.h"
#include "GridView.h"
#include "Region.h"
#include "Note.h"
#include "Playhead.h"
#include "Transport.h"
#include "ContextMenu.h"
#include "Node.h"
#include "NodeManager.h"
#include "nodes/nodetypes.h"
#include "UndoManager.h"

namespace {
struct RegionTuningSnapshot {
    int mode = 0;
    float harmonicAnchorMidi = 69.0f;
    int harmonicAnchorNumber = 1;
    float edoAnchorMidi = 69.0f;
    float edoStep = 1.0f;
    int edoSpanDivisions = 0;
    float edoSpanLoMidi = 0.0f;
    float edoSpanHiMidi = 0.0f;
    int spanLoHarm = 0;
    int spanHiHarm = 0;
    int spanLoEdoK = INT_MAX;
    int spanHiEdoK = INT_MAX;
    int edoStepSemiNum = 1;
    int edoStepSemiDen = 1;
};

struct NoteTuningSnapshot {
    int harmonicNumber = 0;
    int tuningMode = 0;
    float tuningAnchorMidi = 69.0f;
    int tuningAnchorHarmonic = 1;
    float tuningEdoAnchorMidi = 69.0f;
    float tuningEdoStep = 1.0f;
};

} // namespace

static constexpr Uint64 kPitchFactorsTooltipDwellMs = 500;

void PianoRoll::refreshPitchFactorsHoverTiming() {
    if (!hoveredElement) {
        hoverPitchFactorsNoteId = -1;
        return;
    }
    if (hoverPitchFactorsNoteId != hoveredElement->id) {
        hoverPitchFactorsNoteId = hoveredElement->id;
        hoverPitchFactorsStartMs = SDL_GetTicks();
    }
}

void PianoRoll::renderPitchFactorsHoverTooltip() {
    auto note = hoveredElement;
    if (!note || !fonts.mainFont)
        return;
    if (SDL_GetTicks() - hoverPitchFactorsStartMs < kPitchFactorsTooltipDwellMs)
        return;
    if (hoverPitchFactorsNoteId != note->id)
        return;

    const char* text = "the correct vector";

    SDL_Surface* surf = TTF_RenderText_Blended(fonts.mainFont, text, 0, SDL_Color{150, 165, 180, 200});
    if (!surf)
        return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    if (!tex) {
        SDL_DestroySurface(surf);
        return;
    }

    constexpr float pad = 5;
    constexpr float inset = 3;
    const float bw = surf->w + pad * 2;
    const float bh = surf->h + pad * 2;
    float bx = mouseX + 14;
    float by = mouseY - bh - 10;
    if (bx + bw > width - inset)
        bx = width - bw - inset;
    if (bx < inset)
        bx = inset;
    if (by < inset)
        by = inset;
    if (by + bh > height - bottomMargin - inset)
        by = height - bottomMargin - bh - inset;

    SDL_BlendMode prevBm;
    SDL_GetRenderDrawBlendMode(renderer, &prevBm);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    SDL_FRect bg{bx - 2, by - 2, bw + 4, bh + 4};
    SDL_SetRenderDrawColor(renderer, 26, 30, 36, 210);
    SDL_RenderFillRect(renderer, &bg);
    SDL_SetRenderDrawColor(renderer, 72, 86, 102, 120);
    SDL_RenderRect(renderer, &bg);

    SDL_FRect tr{bx + pad - 2, by + pad - 2, static_cast<float>(surf->w), static_cast<float>(surf->h)};
    SDL_RenderTexture(renderer, tex, nullptr, &tr);

    SDL_SetRenderDrawBlendMode(renderer, prevBm);
    SDL_DestroyTexture(tex);
    SDL_DestroySurface(surf);
}

float PianoRoll::harmonicToMidi(int harmonic) const {
    const int h = std::max(1, harmonic);
    const int anchorH = std::max(1, harmonicAnchorNumber);
    return harmonicAnchorMidi + 12.0f * std::log2(static_cast<float>(h) / static_cast<float>(anchorH));
}

int PianoRoll::midiToNearestHarmonic(float midi) const {
    const int anchorH = std::max(1, harmonicAnchorNumber);
    const float rel = std::pow(2.0f, (midi - harmonicAnchorMidi) / 12.0f);
    const int h = static_cast<int>(std::round(rel * static_cast<float>(anchorH)));
    return std::max(1, h);
}

void PianoRoll::applyHarmonicAnchor(float midi, int harmonic) {
    tuningMode = TuningMode::Harmonic;
    harmonicAnchorMidi = midi;
    harmonicAnchorNumber = std::max(1, harmonic);
    if (region) {
        region->tuningEdoSpanDivisions = 0;
        region->tuningEdoSpanLoMidi = 0.0f;
        region->tuningEdoSpanHiMidi = 0.0f;
        region->tuningSpanLoHarm = 0;
        region->tuningSpanHiHarm = 0;
        region->tuningSpanLoEdoK = INT_MAX;
        region->tuningSpanHiEdoK = INT_MAX;
    }
    syncTuningToRegion();
    updateLines();
}

void PianoRoll::defineEdoFromInterval(float a, float b, int steps) {
    const int n = std::max(1, steps);
    const float lo = std::min(a, b);
    const float hi = std::max(a, b);
    const float diff = std::max(1e-6f, hi - lo);
    tuningMode = TuningMode::EDO;
    edoAnchorMidi = lo;
    edoStep = diff / static_cast<float>(n);
    if (region) {
        region->tuningEdoSpanDivisions = n;
        region->tuningEdoSpanLoMidi = lo;
        region->tuningEdoSpanHiMidi = hi;
        region->tuningSpanLoHarm = 0;
        region->tuningSpanHiHarm = 0;
        region->tuningSpanLoEdoK = INT_MAX;
        region->tuningSpanHiEdoK = INT_MAX;
    }
    syncTuningToRegion();
    updateLines();
}

static RegionTuningSnapshot captureRegionTuning(const Region* region) {
    RegionTuningSnapshot s;
    if (!region) return s;
    s.mode = region->tuningMode;
    s.harmonicAnchorMidi = region->tuningAnchorMidi;
    s.harmonicAnchorNumber = region->tuningAnchorHarmonic;
    s.edoAnchorMidi = region->tuningEdoAnchorMidi;
    s.edoStep = region->tuningEdoStep;
    s.edoSpanDivisions = region->tuningEdoSpanDivisions;
    s.edoSpanLoMidi = region->tuningEdoSpanLoMidi;
    s.edoSpanHiMidi = region->tuningEdoSpanHiMidi;
    s.spanLoHarm = region->tuningSpanLoHarm;
    s.spanHiHarm = region->tuningSpanHiHarm;
    s.spanLoEdoK = region->tuningSpanLoEdoK;
    s.spanHiEdoK = region->tuningSpanHiEdoK;
    s.edoStepSemiNum = region->tuningEdoStepSemiNum;
    s.edoStepSemiDen = region->tuningEdoStepSemiDen;
    return s;
}

static void applyRegionTuning(Region* region, const RegionTuningSnapshot& s) {
    if (!region) return;
    region->tuningMode = s.mode;
    region->tuningAnchorMidi = s.harmonicAnchorMidi;
    region->tuningAnchorHarmonic = s.harmonicAnchorNumber;
    region->tuningEdoAnchorMidi = s.edoAnchorMidi;
    region->tuningEdoStep = s.edoStep;
    region->tuningEdoSpanDivisions = s.edoSpanDivisions;
    region->tuningEdoSpanLoMidi = s.edoSpanLoMidi;
    region->tuningEdoSpanHiMidi = s.edoSpanHiMidi;
    region->tuningSpanLoHarm = s.spanLoHarm;
    region->tuningSpanHiHarm = s.spanHiHarm;
    region->tuningSpanLoEdoK = s.spanLoEdoK;
    region->tuningSpanHiEdoK = s.spanHiEdoK;
    region->tuningEdoStepSemiNum = std::max(1, s.edoStepSemiNum);
    region->tuningEdoStepSemiDen = std::max(1, s.edoStepSemiDen);
}

static NoteTuningSnapshot captureNoteTuning(const std::shared_ptr<Note>& n) {
    NoteTuningSnapshot s;
    if (!n) return s;
    s.harmonicNumber = n->harmonicNumber;
    s.tuningMode = n->tuningMode;
    s.tuningAnchorMidi = n->tuningAnchorMidi;
    s.tuningAnchorHarmonic = n->tuningAnchorHarmonic;
    s.tuningEdoAnchorMidi = n->tuningEdoAnchorMidi;
    s.tuningEdoStep = n->tuningEdoStep;
    return s;
}

static void applyNoteTuningSnapshot(const std::shared_ptr<Note>& n, const NoteTuningSnapshot& s) {
    if (!n) return;
    n->harmonicNumber = s.harmonicNumber;
    n->tuningMode = s.tuningMode;
    n->tuningAnchorMidi = s.tuningAnchorMidi;
    n->tuningAnchorHarmonic = s.tuningAnchorHarmonic;
    n->tuningEdoAnchorMidi = s.tuningEdoAnchorMidi;
    n->tuningEdoStep = s.tuningEdoStep;
}

void PianoRoll::applyNoteTuning(const std::shared_ptr<Note>& note) {
    if (!note) return;
    tuningMode = (note->tuningMode == 1) ? TuningMode::EDO : TuningMode::Harmonic;
    harmonicAnchorMidi = note->tuningAnchorMidi;
    harmonicAnchorNumber = std::max(1, note->tuningAnchorHarmonic);
    edoAnchorMidi = note->tuningEdoAnchorMidi;
    edoStep = std::max(1e-5f, note->tuningEdoStep);
    syncTuningToRegion();
    updateLines();
}

void PianoRoll::stampNoteTuning(const std::shared_ptr<Note>& note) {
    if (!note) return;
    note->tuningMode = (tuningMode == TuningMode::EDO) ? 1 : 0;
    note->tuningAnchorMidi = harmonicAnchorMidi;
    note->tuningAnchorHarmonic = harmonicAnchorNumber;
    note->tuningEdoAnchorMidi = edoAnchorMidi;
    note->tuningEdoStep = edoStep;
    const size_t li = hoveredPitchLineIndex;
    if (li != SIZE_MAX && li < lines.size())
        note->num = lines[li];
    if (li != SIZE_MAX && li < lineStructural.size() && tuningMode == TuningMode::Harmonic)
        note->harmonicNumber = std::max(1, lineStructural[li]);
}

void PianoRoll::syncTuningToRegion() {
    if (!region) return;
    region->tuningMode = static_cast<int>(tuningMode);
    region->tuningAnchorMidi = harmonicAnchorMidi;
    region->tuningAnchorHarmonic = harmonicAnchorNumber;
    region->tuningEdoAnchorMidi = edoAnchorMidi;
    region->tuningEdoStep = edoStep;
}

void PianoRoll::loadTuningFromRegion() {
    if (!region) return;
    tuningMode = (region->tuningMode == 1) ? TuningMode::EDO : TuningMode::Harmonic;
    harmonicAnchorMidi = region->tuningAnchorMidi;
    harmonicAnchorNumber = std::max(1, region->tuningAnchorHarmonic);
    edoAnchorMidi = region->tuningEdoAnchorMidi;
    edoStep = std::max(1e-5f, region->tuningEdoStep);
}

void PianoRoll::newTuning() {
    // Toggle between harmonic and EDO view quickly.
    auto before = captureRegionTuning(region);
    auto after = before;
    after.mode = (before.mode == 0) ? 1 : 0;
    after.edoSpanDivisions = 0;
    after.edoSpanLoMidi = 0.0f;
    after.edoSpanHiMidi = 0.0f;
    after.spanLoHarm = 0;
    after.spanHiHarm = 0;
    after.spanLoEdoK = INT_MAX;
    after.spanHiEdoK = INT_MAX;
    auto* pa = new ProjectAction(project, NullAction);
    pa->name = "Toggle Tuning Mode";
    pa->doAction = [this, after]() {
        applyRegionTuning(region, after);
        loadTuningFromRegion();
        updateLines();
    };
    pa->undoAction = [this, before]() {
        applyRegionTuning(region, before);
        loadTuningFromRegion();
        updateLines();
    };
    project->um->newAction(pa);
}

void PianoRoll::updateLines() {
    lines.clear();
    lineLabels.clear();
    lineStructural.clear();

    if (tuningMode == TuningMode::Harmonic) {
        for (int h = 1; h <= 512; ++h) {
            const float midi = harmonicToMidi(h);
            if (midi < -24.0f || midi > 152.0f) continue;
            lines.push_back(midi);
            lineLabels.push_back(std::to_string(h));
            lineStructural.push_back(h);
        }
    } else {
        const float step = std::max(1e-5f, edoStep);
        for (int k = -1024; k <= 1024; ++k) {
            const float midi = edoAnchorMidi + static_cast<float>(k) * step;
            if (midi < -24.0f || midi > 152.0f) continue;
            lines.push_back(midi);
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(2) << midi;
            lineLabels.push_back(ss.str());
            lineStructural.push_back(k);
        }
    }
    Scroll();
}

size_t PianoRoll::closestLineIndexForMidi(float midiPitch) const {
    if (lines.empty())
        return SIZE_MAX;
    size_t best = 0;
    float bd = FLT_MAX;
    for (size_t i = 0; i < lines.size(); ++i) {
        const float d = std::fabs(lines[i] - midiPitch);
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}

void PianoRoll::refreshHoveredPitchLineIndex() {
    hoveredPitchLineIndex = SIZE_MAX;
    if (lines.empty())
        return;
    if (mouseX < leftMargin || mouseY < topMargin)
        return;
    if (mouseY > height - bottomMargin)
        return;
    size_t best = 0;
    float bd = FLT_MAX;
    for (size_t i = 0; i < lines.size(); ++i) {
        const float d = std::fabs(mouseY - getY(lines[i]));
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    hoveredPitchLineIndex = best;
}

int PianoRoll::hoveredHarmonicFromGrid() {
    if (tuningMode != TuningMode::Harmonic || lineLabels.empty() || lines.empty())
        return 0;
    size_t best = 0;
    float bd = FLT_MAX;
    for (size_t i = 0; i < lines.size(); ++i) {
        const float d = std::fabs(mouseY - getY(lines[i]));
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    try {
        return std::max(1, std::stoi(lineLabels.at(best)));
    } catch (...) {
        return 0;
    }
}

int PianoRoll::hoveredEdoKFromGrid() {
    if (tuningMode != TuningMode::EDO || lineStructural.empty() || lines.empty())
        return INT_MAX;
    size_t best = 0;
    float bd = FLT_MAX;
    for (size_t i = 0; i < lines.size(); ++i) {
        const float d = std::fabs(mouseY - getY(lines[i]));
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return lineStructural[best];
}

int PianoRoll::structuralHarmonicNearNote(const std::shared_ptr<Note>& n) {
    if (!n)
        return hoveredHarmonicFromGrid();
    if (n->harmonicNumber > 0) {
        for (size_t i = 0; i < lineStructural.size(); ++i) {
            if (lineStructural[i] == n->harmonicNumber)
                return std::max(1, n->harmonicNumber);
        }
    }
    if (hoveredPitchLineIndex != SIZE_MAX && hoveredPitchLineIndex < lineStructural.size())
        return std::max(1, lineStructural[hoveredPitchLineIndex]);
    return hoveredHarmonicFromGrid();
}

int PianoRoll::structuralEdoKNearNote(const std::shared_ptr<Note>& n) {
    if (!n)
        return hoveredEdoKFromGrid();
    const size_t li = closestLineIndexForMidi(n->num);
    if (li != SIZE_MAX && li < lineStructural.size())
        return lineStructural[li];
    if (hoveredPitchLineIndex != SIZE_MAX && hoveredPitchLineIndex < lineStructural.size())
        return lineStructural[hoveredPitchLineIndex];
    return hoveredEdoKFromGrid();
}

PianoRoll::PianoRoll(bool* detached, SDL_FRect* rect, Region* region, Window* w) : region(region), GridView(detached, rect, 40, w, region->project) {
   
    if (*detached) WindowHandler::instance()->addWindow(this);
    leftMargin = 80.0f;
 
    loadTuningFromRegion();
    updateLines();

    SDL_SetCursor(cursors.grabber);

    scrollY = 800;

    divHeight = 200; //octaveheight

    minHeight = 12.0f / 128;

    bottomMargin = 20;

    UpdateGrid();

    Scroll();

    initWindow();

    float x = -1000; //for now only this many measures
    times.clear();
    while(x < 1000) {
        times.push_back(x);
        x += 1.0f/notesPerBar;
    }

    createGridRect();
}

PianoRoll::~PianoRoll() {
    for(int i = 0; i<4; i++) {
        SDL_DestroyTexture(layers[i]);
    }
    
    WindowHandler::instance()->removeWindow(this);
}



void PianoRoll::UpdateGrid() {
    if(notesPerOctave <= 0) {
        notesPerOctave = 1;
    } else if(notesPerOctave > 128) {
        notesPerOctave = 128;
    }
    cellHeight = divHeight/notesPerOctave;
    cellHeight12 = divHeight/12.0;

    double a440 = cellHeight12*59;
    yMin = cellHeight12*59 - std::floor(cellHeight12*59/cellHeight)*cellHeight;
    yMax = cellHeight12*69 - std::floor(cellHeight12*69/cellHeight)*cellHeight;

    Scroll();
}

double PianoRoll::getNoteName(double y) {
    return 129-(y + scrollY)/cellHeight12;
}

float PianoRoll::getY(float noteMidiNum) {
    return -cellHeight12*((noteMidiNum-129)+(scrollY/cellHeight12)) - lineWidth;
}

float PianoRoll::getHoveredLine() {
    float closestDiff = FLT_MAX;
    float closestLine = -1.0f;

    for (auto line : lines) {
        float y = getY(line);
        float diff = std::abs(mouseY - y);
        if (diff < closestDiff) {
            closestDiff = diff;
            closestLine = line;
        }
    }

    return closestLine;
}


void PianoRoll::RenderDestinations() {

    if (fonts.mainFont) {
    } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error: mainFont is NULL in PianoRoll::RenderDestinations!\n");
        return;
    }

    auto target = SDL_GetRenderTarget(renderer);
    SDL_Texture* KeyTexture;
    SDL_SetRenderTarget(renderer, PianoTexture);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0); // Transparent
    SDL_RenderClear(renderer);
    SDL_Color textColor = {0, 0, 0, 255};


    SDL_FRect backgroundRect = {0, topMargin, leftMargin, height - topMargin - bottomMargin};

    setRenderColor(colors.keyWhite);
    SDL_RenderFillRect(renderer, &backgroundRect);
    SDL_SetRenderDrawColor(renderer,0,0,0,255);
    SDL_RenderLine(renderer, leftMargin+1,topMargin,leftMargin+1,height - topMargin - bottomMargin);

    SDL_SetRenderDrawColor(renderer, 100, 100, 100, 255);

    for (size_t i = 0; i < lines.size(); ++i) {
        float y = getY(lines[i]);
        std::string noteNumStrTemp = (i < lineLabels.size()) ? lineLabels[i] : std::to_string(lines[i]);
        const char* noteNumStr = noteNumStrTemp.c_str();
        
        SDL_Surface* textSurface = TTF_RenderText_Solid(fonts.mainFont, noteNumStr, noteNumStrTemp.size(), textColor);  // textColor is an SDL_Color

        KeyTexture = SDL_CreateTextureFromSurface(renderer, textSurface);

        SDL_FRect textRect = {
            0,
            y - textSurface->h/2,
            static_cast<float>(textSurface->w),
            static_cast<float>(textSurface->h)
        };

        SDL_RenderLine(renderer, textSurface->w, y, leftMargin, y);
        SDL_DestroySurface(textSurface);

        SDL_RenderTexture(renderer, KeyTexture, NULL, &textRect);

    }

    SDL_DestroyTexture(KeyTexture);
    
    SDL_SetRenderTarget(renderer, target);

}

void PianoRoll::Scroll() {


    numCellsRight = (scrollX)/dW;
    numCellsDown = (scrollY-yMin + topMargin)/cellHeight;
    numCellsDown12 = scrollY/cellHeight12;
    if((scrollY + topMargin -yMin - cellHeight12) <= 0) {
        scrollY = yMin + cellHeight12 - topMargin;
    } else {
        if(scrollY+height+yMin+yMax >= 129*cellHeight12) {
            scrollY = 129*cellHeight12 - height - yMin -yMax;
        }
    }
                numCellsDown = (scrollY-yMin)/cellHeight;


    yOffset = (std::ceil(numCellsDown) * cellHeight) - scrollY;
    yOffset12 = (std::ceil(numCellsDown12) * cellHeight12) - scrollY;

    xOffset = (std::ceil(numCellsRight) * dW) - scrollX;

        refreshGrid = true;
        handleMouse();
}

bool PianoRoll::customTick() {
    
    if(refreshGrid) {
        refreshGrid = false;
        RenderGridTexture();
        RenderDestinations();
    }

    RenderNotes();

    SDL_RenderTexture(renderer, backgroundTexture, nullptr, dstRect);

    if (selectingInterval) {
        const float yStart = getY(intervalStartLine);
        const float yEnd = getY(intervalEndLine);
        const float yTop = std::min(yStart, yEnd);
        const float yBot = std::max(yStart, yEnd);
        SDL_FRect band{leftMargin, yTop, width - leftMargin, std::max(1.0f, yBot - yTop)};
        // Layered between grey background and grid/notes.
        SDL_SetRenderDrawColor(renderer, 45, 110, 210, 32);
        SDL_RenderFillRect(renderer, &band);
        SDL_SetRenderDrawColor(renderer, 70, 150, 235, 78);
        SDL_RenderRect(renderer, &band);
    }

    SDL_RenderTexture(renderer, gridTexture, nullptr, dstRect);
    SDL_RenderTexture(renderer, NotesTexture, nullptr, dstRect);

    if(project->processing) {
       for(auto pos : region->positions) {
           playHead->render(renderer, dW, scrollX + (float)pos->start * dW);
        }
    }

    SDL_RenderTexture(renderer, PianoTexture, nullptr, dstRect);

    if (selectingInterval) {
        const float yEnd = getY(intervalEndLine);
        SDL_SetRenderDrawColor(renderer, 65, 190, 240, 128);
        SDL_RenderLine(renderer, mouseX, yEnd, leftMargin, yEnd);
    }

    transport->render();

    SDL_FRect bottomRect{
        0,
        height-bottomMargin,
        width,
        bottomMargin
    };

    SDL_SetRenderDrawColor(renderer, 20, 20, 20, 255);
    SDL_RenderFillRect(renderer, &bottomRect);

    modeButtonRect = SDL_FRect{
        8.0f,
        height - bottomMargin + 3.0f,
        220.0f,
        std::max(12.0f, bottomMargin - 6.0f)
    };
    SDL_SetRenderDrawColor(renderer, 50, 50, 50, 255);
    SDL_RenderFillRect(renderer, &modeButtonRect);
    SDL_SetRenderDrawColor(renderer, 130, 130, 130, 255);
    SDL_RenderRect(renderer, &modeButtonRect);
    if (fonts.mainFont) {
        const char* modeText = (tuningMode == TuningMode::Harmonic) ? "Mode: Harmonic" : "Mode: EDO";
        SDL_Surface* surf = TTF_RenderText_Blended(fonts.mainFont, modeText, 0, SDL_Color{230, 230, 230, 255});
        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
            if (tex) {
                const float scale = std::min(1.0f, (modeButtonRect.w - 10.0f) / static_cast<float>(surf->w));
                SDL_FRect tr{
                    modeButtonRect.x + (modeButtonRect.w - surf->w * scale) * 0.5f,
                    modeButtonRect.y + (modeButtonRect.h - surf->h * scale) * 0.5f,
                    surf->w * scale,
                    surf->h * scale
                };
                SDL_RenderTexture(renderer, tex, nullptr, &tr);
                SDL_DestroyTexture(tex);
            }
            SDL_DestroySurface(surf);
        }
    }

    renderPitchFactorsHoverTooltip();

    return true;
}

void PianoRoll::initWindow() {

    dstRect->w = width;
    dstRect->h = height;

    gridRect = {
        dstRect->x+leftMargin,
        dstRect->y+topMargin,
        dstRect->w-leftMargin,
        dstRect->h-topMargin - bottomMargin
    };

    SDL_DestroyTexture(backgroundTexture);
    SDL_DestroyTexture(gridTexture);
    SDL_DestroyTexture(PianoTexture);
    SDL_DestroyTexture(KeyTexture);
    SDL_DestroyTexture(NotesTexture);


    backgroundTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
    gridTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
    PianoTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
    KeyTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
    NotesTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);

    layers[0] = backgroundTexture;
    layers[1] = gridTexture;
    layers[2] = NotesTexture;
    layers[3] = PianoTexture;

    auto target = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, backgroundTexture);
    setRenderColor(colors.background);

    SDL_RenderClear(renderer); // Clear backgroundTexture with the background color

    SDL_SetRenderTarget(renderer, target);

    if(height > (128*cellHeight12 - yMax - yMin)) {
        divHeight = 12*height/128;
        UpdateGrid();
        
    }
    
    Scroll();
    RenderGridTexture();      

    RenderDestinations();
    
    RenderNotes();

}

void PianoRoll::clickMouse(SDL_Event& e) {
    switch (e.type) {

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (e.button.button == SDL_BUTTON_LEFT) {
                lmb = true;
                if(mouseY < topMargin) {
                    return;
                }
                if(mouseY > height - bottomMargin) {
                    if (mouseX >= modeButtonRect.x &&
                        mouseX <= modeButtonRect.x + modeButtonRect.w &&
                        mouseY >= modeButtonRect.y &&
                        mouseY <= modeButtonRect.y + modeButtonRect.h) {
                        newTuning();
                    }
                    return;
                }
                if (isShiftPressed && mouseX > leftMargin) {
                    selectingInterval = true;
                    intervalSelectStartedHarmonic = (tuningMode == TuningMode::Harmonic);
                    intervalStartNote = std::dynamic_pointer_cast<Note>(hoveredElement);
                    intervalStartLine = intervalStartNote ? intervalStartNote->num : getHoveredLine();
                    intervalEndLine = intervalStartLine;
                    intervalDragMoved = false;
                    if (tuningMode == TuningMode::Harmonic) {
                        intervalDragHarmA =
                            intervalStartNote ? structuralHarmonicNearNote(intervalStartNote) : hoveredHarmonicFromGrid();
                        intervalDragHarmB = intervalDragHarmA;
                        intervalDragEdoKA = INT_MAX;
                        intervalDragEdoKB = INT_MAX;
                    } else {
                        intervalDragEdoKA =
                            intervalStartNote ? structuralEdoKNearNote(intervalStartNote) : hoveredEdoKFromGrid();
                        intervalDragEdoKB = intervalDragEdoKA;
                        intervalDragHarmA = 0;
                        intervalDragHarmB = 0;
                    }
                    return;
                }
                if(mouseX > leftMargin && stretchingNote == nullptr) {
                    if(hoveredElement == nullptr) {
                        createElement();
                    } else {
                        auto n = std::dynamic_pointer_cast<Note>(hoveredElement);
                        if (isShiftPressed) {
                            if (!n) return;
                            auto before = captureRegionTuning(region);
                            auto after = before;
                            after.mode = n->tuningMode;
                            after.harmonicAnchorMidi = n->tuningAnchorMidi;
                            after.harmonicAnchorNumber = n->tuningAnchorHarmonic;
                            after.edoAnchorMidi = n->tuningEdoAnchorMidi;
                            after.edoStep = n->tuningEdoStep;
                            auto* pa = new ProjectAction(project, NullAction);
                            pa->name = "Recall Note Tuning View";
                            pa->doAction = [this, after]() {
                                applyRegionTuning(region, after);
                                loadTuningFromRegion();
                                updateLines();
                            };
                            pa->undoAction = [this, before]() {
                                applyRegionTuning(region, before);
                                loadTuningFromRegion();
                                updateLines();
                            };
                            project->um->newAction(pa);
                            return;
                        }
                        movingNote = n;
                    }
                }
            }
            if (e.button.button == SDL_BUTTON_RIGHT) {
                rmb = true;
                if(mouseX > leftMargin && stretchingNote == nullptr) {
                    if(isShiftPressed && hoveredElement != nullptr) {
                        auto ctxMenu = ContextMenu::get();
                        ctxMenu->active = true;
                        ctxMenu->window_id = SDL_GetWindowID(window);
                        ctxMenu->renderer = renderer;
                        SDL_StartTextInput(window);

                        ctxMenu->locX = mouseX;
                        ctxMenu->locY = mouseY;

                        ctxMenu->dynamicTick = getTextInputTicker([this](std::string text) {
                            try {
                                const int h = std::max(1, std::stoi(text));
                                if (!hoveredElement) return;
                                auto note = std::dynamic_pointer_cast<Note>(hoveredElement);
                                if (!note) return;
                                auto beforeRegion = captureRegionTuning(region);
                                auto beforeNote = captureNoteTuning(note);
                                auto afterRegion = beforeRegion;
                                afterRegion.mode = 0;
                                afterRegion.harmonicAnchorMidi = note->num;
                                afterRegion.harmonicAnchorNumber = h;
                                auto afterNote = beforeNote;
                                afterNote.harmonicNumber = h;
                                afterNote.tuningMode = 0;
                                afterNote.tuningAnchorMidi = note->num;
                                afterNote.tuningAnchorHarmonic = h;
                                auto* pa = new ProjectAction(project, NullAction);
                                pa->name = "Assign Note Harmonic";
                                pa->doAction = [this, note, afterRegion, afterNote]() {
                                    applyRegionTuning(region, afterRegion);
                                    loadTuningFromRegion();
                                    note->harmonicNumber = afterNote.harmonicNumber;
                                    note->tuningMode = afterNote.tuningMode;
                                    note->tuningAnchorMidi = afterNote.tuningAnchorMidi;
                                    note->tuningAnchorHarmonic = afterNote.tuningAnchorHarmonic;
                                    note->tuningEdoAnchorMidi = afterNote.tuningEdoAnchorMidi;
                                    note->tuningEdoStep = afterNote.tuningEdoStep;
                                    updateLines();
                                    stampNoteTuning(note);
                                };
                                pa->undoAction = [this, note, beforeRegion, beforeNote]() {
                                    applyRegionTuning(region, beforeRegion);
                                    loadTuningFromRegion();
                                    applyNoteTuningSnapshot(note, beforeNote);
                                    updateLines();
                                };
                                project->um->newAction(pa);
                                isShiftPressed = false;
                                rmb = false;
                            } catch (...) {
                            }
                        });
                    } else {
                        deleteElement();
                    }
                }

            }
            handleMouse();
            refreshGrid = true;
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (e.button.button == SDL_BUTTON_LEFT) {
                lmb = false;
                movingNote = nullptr;
                if (selectingInterval) {
                    auto endNote = std::dynamic_pointer_cast<Note>(hoveredElement);
                    const bool sameNoteClick = !intervalDragMoved &&
                        intervalStartNote && endNote && (intervalStartNote->id == endNote->id);
                    selectingInterval = false;
                    if (sameNoteClick) {
                        applyNoteTuning(intervalStartNote);
                        intervalStartNote = nullptr;
                        refreshGrid = true;
                        return;
                    }
                    if (std::fabs(intervalEndLine - intervalStartLine) < 0.01f) {
                        intervalStartNote = nullptr;
                        refreshGrid = true;
                        return;
                    }
                    const bool capStartedHarm = intervalSelectStartedHarmonic;
                    const int capHarmA = intervalDragHarmA;
                    const int capHarmB = intervalDragHarmB;
                    const int capEdoKA = intervalDragEdoKA;
                    const int capEdoKB = intervalDragEdoKB;
                    auto ctxMenu = ContextMenu::get();
                    ctxMenu->active = true;
                    ctxMenu->window_id = SDL_GetWindowID(window);
                    ctxMenu->renderer = renderer;
                    SDL_StartTextInput(window);
                    ctxMenu->locX = mouseX;
                    ctxMenu->locY = mouseY;
                    const float a = intervalStartLine;
                    const float b = intervalEndLine;
                    ctxMenu->dynamicTick = getTextInputTicker(
                        [this, a, b, capStartedHarm, capHarmA, capHarmB, capEdoKA, capEdoKB](std::string text) {
                        try {
                            const int steps = std::max(1, std::stoi(text));
                            auto before = captureRegionTuning(region);
                            auto after = before;
                            const float loF = std::min(a, b);
                            const float hiF = std::max(a, b);

                            if (capStartedHarm && capHarmA > 0 && capHarmB > 0) {
                                const int hLo = std::min(capHarmA, capHarmB);
                                const int hHi = std::max(capHarmA, capHarmB);
                                after.spanLoHarm = hLo;
                                after.spanHiHarm = hHi;
                                after.spanLoEdoK = INT_MAX;
                                after.spanHiEdoK = INT_MAX;
                                const int ah = std::max(1, after.harmonicAnchorNumber);
                                const auto harmToMidi = [ah](float anchorMidi, int h) {
                                    return anchorMidi + 12.0f * std::log2f(static_cast<float>(std::max(1, h)) /
                                                                           static_cast<float>(ah));
                                };
                                const float loM = harmToMidi(after.harmonicAnchorMidi, hLo);
                                const float hiM = harmToMidi(after.harmonicAnchorMidi, hHi);
                                after.edoSpanLoMidi = std::min(loM, hiM);
                                after.edoSpanHiMidi = std::max(loM, hiM);
                                after.edoAnchorMidi = after.edoSpanLoMidi;
                                const float diff = std::max(1e-6f, after.edoSpanHiMidi - after.edoSpanLoMidi);
                                after.edoStep = diff / static_cast<float>(steps);
                                after.edoSpanDivisions = steps;
                                after.mode = 1;
                            } else if (!capStartedHarm && capEdoKA != INT_MAX && capEdoKB != INT_MAX) {
                                // Span must match the drag (intervalStartLine / intervalEndLine → a, b), e.g. exact
                                // note->num. Do not rebuild from lattice k (structuralEdoK*); that snaps to lines.
                                after.spanLoHarm = 0;
                                after.spanHiHarm = 0;
                                after.edoSpanLoMidi = loF;
                                after.edoSpanHiMidi = hiF;
                                after.edoAnchorMidi = after.edoSpanLoMidi;
                                const float diff = std::max(1e-6f, after.edoSpanHiMidi - after.edoSpanLoMidi);
                                after.edoStep = diff / static_cast<float>(steps);
                                after.edoSpanDivisions = steps;
                                after.mode = 1;
                                after.spanLoEdoK = 0;
                                after.spanHiEdoK = steps;
                            } else {
                                const float diff = std::max(1e-6f, hiF - loF);
                                after.mode = 1;
                                after.edoAnchorMidi = loF;
                                after.edoStep = diff / static_cast<float>(steps);
                                after.edoSpanDivisions = steps;
                                after.edoSpanLoMidi = loF;
                                after.edoSpanHiMidi = hiF;
                                after.spanLoHarm = 0;
                                after.spanHiHarm = 0;
                                after.spanLoEdoK = INT_MAX;
                                after.spanHiEdoK = INT_MAX;
                            }
                            auto* pa = new ProjectAction(project, NullAction);
                            pa->name = "Define EDO Interval";
                            pa->doAction = [this, after]() {
                                applyRegionTuning(region, after);
                                loadTuningFromRegion();
                                updateLines();
                            };
                            pa->undoAction = [this, before]() {
                                applyRegionTuning(region, before);
                                loadTuningFromRegion();
                                updateLines();
                            };
                            project->um->newAction(pa);
                        } catch (...) {
                        }
                    });
                    intervalStartNote = nullptr;
                }
            }
            if (e.button.button == SDL_BUTTON_RIGHT) {
                rmb = false;
            }
            handleMouse();
            break;
    }
}

void PianoRoll::handleCustomInput(SDL_Event& e) {
    switch (e.type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            break;

        case SDL_EVENT_MOUSE_WHEEL:
            Scroll();
            break;

        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            width = e.window.data1;  // New width
            height = e.window.data2; // New height

            initWindow();
            break;

        case SDL_EVENT_WINDOW_RESIZED:
            width = e.window.data1;  // New width
            height = e.window.data2; // New height

            initWindow();
            break;

        case SDL_EVENT_KEY_DOWN:

            switch (e.key.scancode) {
                case SDL_SCANCODE_MINUS:
                    notesPerOctave -= 1;
                    UpdateGrid();
                    break;
                case SDL_SCANCODE_EQUALS:
                    notesPerOctave += 1;
                    UpdateGrid();
                    break;
                default:
                    break;
            }
            break;

        

        case SDL_EVENT_MOUSE_MOTION:

            handleMouse();

            if (selectingInterval) {
                auto endNote = std::dynamic_pointer_cast<Note>(hoveredElement);
                if (endNote) {
                    intervalEndLine = endNote->num;
                    if (!intervalStartNote || endNote->id != intervalStartNote->id) {
                        intervalDragMoved = true;
                    }
                } else {
                    const float nearest = getHoveredLine();
                    if (intervalStartNote) {
                        const float dStart = std::fabs(mouseY - getY(intervalStartLine));
                        const float dNearest = std::fabs(mouseY - getY(nearest));
                        if (dNearest < dStart) {
                            intervalEndLine = nearest;
                            intervalDragMoved = true;
                        } else {
                            intervalEndLine = intervalStartLine;
                        }
                    } else {
                        intervalEndLine = nearest;
                        if (std::fabs(intervalEndLine - intervalStartLine) > 0.01f) {
                            intervalDragMoved = true;
                        }
                    }
                }
                if (intervalSelectStartedHarmonic) {
                    if (endNote)
                        intervalDragHarmB = structuralHarmonicNearNote(endNote);
                    else if (hoveredPitchLineIndex != SIZE_MAX && hoveredPitchLineIndex < lineStructural.size())
                        intervalDragHarmB = std::max(1, lineStructural[hoveredPitchLineIndex]);
                    else
                        intervalDragHarmB = hoveredHarmonicFromGrid();
                } else {
                    if (endNote)
                        intervalDragEdoKB = structuralEdoKNearNote(endNote);
                    else if (hoveredPitchLineIndex != SIZE_MAX && hoveredPitchLineIndex < lineStructural.size())
                        intervalDragEdoKB = lineStructural[hoveredPitchLineIndex];
                    else
                        intervalDragEdoKB = hoveredEdoKFromGrid();
                }
                refreshGrid = true;
                break;
            }

            
            if(stretchingNote != nullptr) {
                if(!lmb) {
                    isStretchingNote = false;
                } else {
                    refreshGrid = true;
                    float dX = mouseX - last_lmb_x;
                    if(dX >= dW/notesPerBar) {
                        stretchElement(1);
                        last_lmb_x += dW/notesPerBar;
                    } else if(dX <= -dW/notesPerBar) {
                        stretchElement(-1);
                        last_lmb_x -= dW/notesPerBar;
                    }
                }

            } else if(lmb && movingNote != nullptr) {
                refreshGrid = true;

                float dX = mouseX - last_lmb_x;
                float step = dW / notesPerBar;
                int steps = dX / step;

                if(steps) {
                    moveNote(movingNote, steps, 0);
                    last_lmb_x += steps * step;
                } 

                if (getHoveredLine() != movingNote->num) {
                    moveNote(movingNote, 0, getHoveredLine() - movingNote->num);
                    lastHoveredLine = getHoveredLine();
                }
                
            } else {
                last_lmb_x = mouseX;
                last_lmb_y = mouseY;
            }

            break;


        // Optionally handle other events you might need:
        default:
            refreshGrid = false;
            break;
    }
}

void PianoRoll::createElement() {
    fract start = getHoveredTime();
    float pitch = getHoveredLine();
    project->createNote(
        region->parentNode->id,
        start,
        lastLength,
        pitch,
        region->id,
        region->parentNode->nm->managerPath
    );
    if (!region->notes.empty()) {
        auto note = region->notes.back();
        refreshHoveredPitchLineIndex();
        const size_t li = hoveredPitchLineIndex;
        if (tuningMode == TuningMode::Harmonic) {
            if (li != SIZE_MAX && li < lineStructural.size())
                note->harmonicNumber = std::max(1, lineStructural[li]);
            else
                note->harmonicNumber = 1;
        } else {
            note->harmonicNumber = 0;
        }
        stampNoteTuning(note);
    }
    refreshGrid = true;
}

void PianoRoll::RenderNotes() {
    auto target = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, NotesTexture);
    SDL_SetRenderDrawColor(renderer,0,0,0,0);
    SDL_RenderClear(renderer);

    //backgrounds first
    for(std::shared_ptr<Note> note : region->notes) {

        float noteX = getNotePosX(note) +1;
        float noteY = getY(note->num);
        float noteEnd = getNoteEnd(note) -2;
        float noteTop = noteY + noteHeight;


        setRenderColor(colors.noteBackground);
        SDL_FRect noteBGRect = { noteX, noteY, noteEnd - noteX, noteTop-noteY};
        SDL_RenderFillRect(renderer, &noteBGRect);
    }

    for(std::shared_ptr<Note> note : region->notes) {
            float noteX = getNotePosX(note) +1;
            float noteY = getY(note->num);
            float noteEnd = getNoteEnd(note) -2;

            //noteRadius = (noteTop - noteY)/2;

            setRenderColor(colors.note);
            SDL_FRect noteRect = { noteX, noteY - noteRadius, noteEnd - noteX, 2*noteRadius};
            SDL_RenderFillRect(renderer, &noteRect);

            setRenderColor(colors.noteBorder);

            SDL_RenderRect(renderer, &noteRect);

    }

    SDL_SetRenderTarget(renderer, target);

}

bool PianoRoll::getExistingNote() {
    hoveredElement = nullptr;
    if(mouseY < topMargin || mouseX < leftMargin) {
        return false;
    }
    int i = 0;
    for (std::shared_ptr<Note> note : region->notes) {
        
        // Get the required positions and size once per iteration
        const int notePosX = getNotePosX(note);
        const int noteEnd = getNoteEnd(note);
        const int noteY = getY(note->num);
        
        // Check if mouse is within note bounds
        if (mouseX >= notePosX && mouseX <= noteEnd &&
            mouseY <= noteY + noteRadius && mouseY >= (noteY - noteRadius)) {
            hoveredElement = note; // Found the hovered note
            lastHoveredLine = getHoveredLine();
            return true; // Exit early
        }
        i++;
    }
    return false;
}


float PianoRoll::getNotePosX(std::shared_ptr<Note> note) {
    return getX(note->start);
}

float PianoRoll::getNoteEnd(std::shared_ptr<Note> note) {
    return getX(note->end);
}

void PianoRoll::deleteElement() {
    if(hoveredElement != nullptr) {
        region->deleteNote(std::dynamic_pointer_cast<Note>(hoveredElement)->id);
        hoveredElement = nullptr;
        Scroll();
    }

}


void PianoRoll::handleMouse() {
    getStretchingNote();
    getExistingNote();

    if(rmb) {
        SDL_SetCursor(cursors.pencil);
        if(hoveredElement != nullptr && !ContextMenu::get()->active) {
            deleteElement();
        } 
    } else {
        if (stretchingNote != nullptr) {
            SDL_SetCursor(cursors.resize);
        } else if(hoveredElement != nullptr) {
            SDL_SetCursor(cursors.mover);
        } else {
            SDL_SetCursor(cursors.selector);
        }
    }

    refreshHoveredPitchLineIndex();
    refreshPitchFactorsHoverTiming();
}

bool PianoRoll::getStretchingNote() {
    if(movingNote != nullptr) {
        return false;
    }
    if(isStretchingNote) {
        return true;
    }
    for (std::shared_ptr<Note> note : region->notes) {
        const int notePosX = getNotePosX(note);
        const int noteEnd = getNoteEnd(note);
        const int noteY = getY(note->num);

        if ((mouseY >= noteY - noteRadius && mouseY <= (noteY + noteRadius))) {
            if(mouseX >= notePosX - selectThresholdX/2 && mouseX <= notePosX + selectThresholdX/2) {
                stretchingNote = note;
                resizeDir = -1;
                isStretchingNote = true;
                return true;
            } else if (mouseX >= noteEnd - selectThresholdX/2 && mouseX <= noteEnd + selectThresholdX/2) {
                stretchingNote = note;
                resizeDir = 1;
                isStretchingNote = true;
                return true;
            }
        }
    }
    stretchingNote = nullptr;
    isStretchingNote = false;
    return false;
}

void PianoRoll::stretchElement(int amount) {
    if(stretchingNote == nullptr) {
        return;
    }
    if(resizeDir == -1) {
        stretchingNote->start = stretchingNote->start + fract(amount,notesPerBar);
    } else if(resizeDir == 1) {
        stretchingNote->end = stretchingNote->end + fract(amount,notesPerBar);
    }

    if(stretchingNote->end < stretchingNote->start) {
        stretchingNote->end = stretchingNote->start;
    }
    lastLength = stretchingNote->end - stretchingNote->start;
    Scroll();
}


void PianoRoll::moveNote(std::shared_ptr<Note> note, int moveX, float y) {
    fract xm = fract(moveX, notesPerBar);

    note->start = note->start + xm;
    note->end = note->end + xm;
    const float dy = y;
    if (std::fabs(dy) < 1e-6f) {
        Scroll();
        return;
    }
    refreshHoveredPitchLineIndex();
    note->tuningAnchorMidi = harmonicAnchorMidi;
    note->tuningAnchorHarmonic = harmonicAnchorNumber;
    note->tuningEdoAnchorMidi = edoAnchorMidi;
    note->tuningEdoStep = edoStep;
    if (hoveredPitchLineIndex != SIZE_MAX && hoveredPitchLineIndex < lines.size()) {
        note->num = lines[hoveredPitchLineIndex];
        if (tuningMode == TuningMode::Harmonic && hoveredPitchLineIndex < lineStructural.size())
            note->harmonicNumber = std::max(1, lineStructural[hoveredPitchLineIndex]);
    } else {
        note->num += dy;
    }
    stampNoteTuning(note);

    Scroll();
}

void PianoRoll::handleWindowInput(SDL_Event& e) {
    float gx, gy;
    SDL_GetGlobalMouseState(&gx, &gy);
    int wx, wy;
    SDL_GetWindowPosition(window, &wx, &wy);
    mouseX = gx - wx;
    mouseY = gy - wy;
    handleInput(e);
}
