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
#include <string>
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
#include "PianoRollInternal.h"

PianoRoll::PianoRoll(Region* region_, Window* parent)
    : EmbeddedWindow(),
      region(region_),
      GridView(nullptr, 40, parent, region_->project)
{
    leftMargin = 80.0f;

    updateLines();
    updateRhythmLines();

    scrollY = 800;

    divHeight = 200; //octaveheight

    minHeight = 12.0f / 128;

    bottomMargin = 20;

    UpdateGrid();

    Scroll();

    float x = -1000; //for now only this many measures
    times.clear();
    while(x < 1000) {
        times.push_back(x);
        x += 1.0f/notesPerBar;
    }

    createGridRect();

    // Sync EmbeddedWindow dimensions with GridView.
    EmbeddedWindow::w = width;
    EmbeddedWindow::h = height;
    title = "Piano Roll";
}

PianoRoll::~PianoRoll() {
    for(int i = 0; i<4; i++) {
        SDL_DestroyTexture(layers[i]);
    }
    for (auto* t : lineLabelTextures) SDL_DestroyTexture(t);
}

static void pianoRollSyncCoords(PianoRoll* pr) {
    // Sync dstRect/gridRect to current embedded window position and size.
    float newW = pr->EmbeddedWindow::w - EmbeddedWindow::kBorderW * 2.f;
    float newH = pr->EmbeddedWindow::h - EmbeddedWindow::kTitleBarH - EmbeddedWindow::kBorderW;
    if (newW < 100.f) newW = 100.f;
    if (newH < 80.f) newH = 80.f;

    if (pr->width != newW || pr->height != newH) {
        pr->needsInit_ = true;
    }
    pr->width = newW;
    pr->height = newH;
    pr->dstRect->w = newW;
    pr->dstRect->h = newH;

    pr->dstRect->x = pr->EmbeddedWindow::x + EmbeddedWindow::kBorderW;
    pr->dstRect->y = pr->EmbeddedWindow::y + EmbeddedWindow::kTitleBarH;
    pr->gridRect.x = pr->leftMargin;
    pr->gridRect.y = pr->topMargin;
    pr->gridRect.w = newW - pr->leftMargin;
    pr->gridRect.h = newH - pr->topMargin - pr->bottomMargin;

    // Always start from global mouse pos, then subtract content origin.
    float gx, gy;
    SDL_GetMouseState(&gx, &gy);
    pr->mouseX = gx - pr->dstRect->x;
    pr->mouseY = gy - pr->dstRect->y;
    pr->transport->moveMouse(pr->mouseX, pr->mouseY);
}

bool PianoRoll::handleContentInput(SDL_Event& e) {
    pianoRollSyncCoords(this);
    return GridView::handleInput(e);
}

bool PianoRoll::handleInput(SDL_Event& e) {
    bool consumed = EmbeddedWindow::handleInput(e);
    pianoRollSyncCoords(this);
    return GridView::handleInput(e) || consumed;
}

float PianoRoll::adjustTransportSeekSec(float rawSec) {
    if (!region || region->positions.empty()) return rawSec;
    // Find the position whose playhead is closest to mouseX.
    double curEff = project->effectiveTime.load();
    float bestDist = FLT_MAX;
    float bestOffset = 0.f;
    for (auto* pos : region->positions) {
        float posStart = Note::beatsFromVector(pos->rhythmVector);
        float off = Note::beatsFromVector(pos->startOffsetPairs);
        float phX = static_cast<float>(curEff - static_cast<double>(posStart) + static_cast<double>(off)) * dW + leftMargin - scrollX;
        float dist = std::abs(mouseX - phX);
        if (dist < bestDist) {
            bestDist = dist;
            bestOffset = posStart - off;
        }
    }
    return rawSec + bestOffset;
}

void PianoRoll::renderContent(SDL_Renderer* r) {
    pianoRollSyncCoords(this);
    customTick(r);
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

void PianoRoll::renderPianoRollGridTexture(SDL_Renderer* renderer) {
    auto target = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, gridTexture);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);

    setRenderColor(renderer, colors.grid);

    for (const auto& rl : rhythmLines) {
        float x = getX(rl.seconds);
        if (x < leftMargin) continue;
        setRenderColor(renderer, colors.grid);
        if (rl.isBeat) {
            SDL_FRect lineRect{x - 1.f, 0, 2.f, static_cast<float>(height)};
            SDL_RenderFillRect(renderer, &lineRect);
        } else {
            SDL_RenderLine(renderer, x, 0, x, static_cast<float>(height));
        }
    }

    for (const auto& pl : pitchLines) {
        float val = getY(pl.midi);
        SDL_RenderLine(renderer, 0, val, width, val);
    }
    SDL_SetRenderTarget(renderer, target);
}

float PianoRoll::getHoveredLine() {
    float closestDiff = FLT_MAX;
    float closestLine = -1.0f;

    for (const auto& pl : pitchLines) {
        float y = getY(pl.midi);
        float diff = std::abs(mouseY - y);
        if (diff < closestDiff) {
            closestDiff = diff;
            closestLine = pl.midi;
        }
    }

    return closestLine;
}


void PianoRoll::RenderDestinations(SDL_Renderer* renderer) {

    if (fonts.mainFont) {
    } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error: mainFont is NULL in PianoRoll::RenderDestinations!\n");
        return;
    }

    auto target = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, PianoTexture);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0); // Transparent
    SDL_RenderClear(renderer);
    SDL_Color textColor = {0, 0, 0, 255};


    SDL_FRect backgroundRect = {0, topMargin, leftMargin, height - topMargin - bottomMargin};

    setRenderColor(renderer, colors.keyWhite);
    SDL_RenderFillRect(renderer, &backgroundRect);
    SDL_SetRenderDrawColor(renderer,0,0,0,255);
    SDL_RenderLine(renderer, leftMargin+1,topMargin,leftMargin+1,height - topMargin - bottomMargin);

    SDL_SetRenderDrawColor(renderer, 100, 100, 100, 255);

    bool labelsChanged = lineLabelTextures.size() != pitchLines.size();
    if (!labelsChanged) {
        for (size_t i = 0; i < pitchLines.size(); ++i) {
            const std::string& label = (i < lineLabels.size()) ? lineLabels[i] : std::to_string(pitchLines[i].midi);
            if (i >= cachedLineLabels.size() || cachedLineLabels[i] != label) {
                labelsChanged = true;
                break;
            }
        }
    }
    if (labelsChanged) {
        for (auto* t : lineLabelTextures) SDL_DestroyTexture(t);
        lineLabelTextures.clear();
        cachedLineLabels.clear();
        lineLabelTextures.reserve(pitchLines.size());
        cachedLineLabels.reserve(pitchLines.size());
        for (size_t i = 0; i < pitchLines.size(); ++i) {
            const std::string& label = (i < lineLabels.size()) ? lineLabels[i] : std::to_string(pitchLines[i].midi);
            cachedLineLabels.push_back(label);
            SDL_Surface* surf = TTF_RenderText_Solid(fonts.mainFont, label.c_str(), label.size(), textColor);
            lineLabelTextures.push_back(SDL_CreateTextureFromSurface(renderer, surf));
            SDL_DestroySurface(surf);
        }
    }

    for (size_t i = 0; i < pitchLines.size(); ++i) {
        float y = getY(pitchLines[i].midi);
        SDL_Texture* tex = lineLabelTextures[i];
        float tw, th;
        SDL_GetTextureSize(tex, &tw, &th);
        SDL_FRect textRect = {0, y - th/2, tw, th};
        SDL_RenderLine(renderer, tw, y, leftMargin, y);
        SDL_RenderTexture(renderer, tex, NULL, &textRect);
    }
    
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
        getStretchingNote();
        getExistingNote();
        refreshHoveredPitchLineIndex();
        refreshPitchFactorsHoverTiming();
}

bool PianoRoll::customTick(SDL_Renderer* renderer) {
    if (!backgroundTexture || needsInit_) {
        needsInit_ = false;
        initWindow(renderer);
    }

    updateRhythmLines();

    if(refreshGrid) {
        refreshGrid = false;
        renderPianoRollGridTexture(renderer);
        RenderDestinations(renderer);
    }

    RenderNotes(renderer);

    SDL_RenderTexture(renderer, backgroundTexture, nullptr, dstRect);

    const bool showIntervalPreview = selectingInterval || intervalEdoDefineDialogOpen;
    const float visIntervalStartLine =
        selectingInterval ? intervalStartLine : intervalDialogFrozenStartLine;
    const float visIntervalEndLine = selectingInterval ? intervalEndLine : intervalDialogFrozenEndLine;

    if (showIntervalPreview) {
        const float yStart = getY(visIntervalStartLine);
        const float yEnd = getY(visIntervalEndLine);
        const float yTop = std::min(yStart, yEnd);
        const float yBot = std::max(yStart, yEnd);
        SDL_FRect band{dstRect->x + leftMargin, dstRect->y + yTop, width - leftMargin, std::max(1.0f, yBot - yTop)};
        SDL_SetRenderDrawColor(renderer, 45, 110, 210, 32);
        SDL_RenderFillRect(renderer, &band);
        SDL_SetRenderDrawColor(renderer, 70, 150, 235, 78);
        SDL_RenderRect(renderer, &band);
    }

    const bool showRhythmIntervalPreview = selectingRhythmInterval || rhythmEdoDefineDialogOpen;
    if (showRhythmIntervalPreview) {
        const float startSec = selectingRhythmInterval ? rhythmIntervalStartSec : rhythmDialogFrozenStartSec;
        const float endSec = selectingRhythmInterval ? rhythmIntervalEndSec : rhythmDialogFrozenEndSec;
        renderRhythmIntervalPreviewBand(renderer, startSec, endSec);
    }

    SDL_RenderTexture(renderer, gridTexture, nullptr, dstRect);
    SDL_RenderTexture(renderer, NotesTexture, nullptr, dstRect);

    transport->render(renderer);

    for(auto pos : region->positions) {
        float posStart = Note::beatsFromVector(pos->rhythmVector);
        float off = Note::beatsFromVector(pos->startOffsetPairs);
        playHead->render(renderer, dW, scrollX + (posStart - off) * dW);
    }

    SDL_RenderTexture(renderer, PianoTexture, nullptr, dstRect);

    if (showIntervalPreview) {
        const float yEnd = getY(visIntervalEndLine);
        SDL_SetRenderDrawColor(renderer, 65, 190, 240, 128);
        SDL_RenderLine(renderer, dstRect->x + mouseX, dstRect->y + yEnd, dstRect->x + leftMargin, dstRect->y + yEnd);
    }

    if (showRhythmIntervalPreview) {
        const float endSec = selectingRhythmInterval ? rhythmIntervalEndSec : rhythmDialogFrozenEndSec;
        renderRhythmIntervalEndLine(renderer, endSec);
    }

    SDL_FRect bottomRect{
        dstRect->x,
        dstRect->y + height - bottomMargin,
        width,
        bottomMargin
    };
    SDL_SetRenderDrawColor(renderer, 20, 20, 20, 255);
    SDL_RenderFillRect(renderer, &bottomRect);

    modeButtonRect = SDL_FRect{
        dstRect->x + 8.0f,
        dstRect->y + height - bottomMargin + 3.0f,
        220.0f,
        std::max(12.0f, bottomMargin - 6.0f)
    };
    const bool edoModeBarLocked =
        region && tuningMode == TuningMode::Harmonic && !regionHasDragDefinedEdoLattice(region);
    SDL_SetRenderDrawColor(renderer, edoModeBarLocked ? 42 : 50, edoModeBarLocked ? 42 : 50, edoModeBarLocked ? 44 : 50, 255);
    SDL_RenderFillRect(renderer, &modeButtonRect);
    SDL_SetRenderDrawColor(renderer, edoModeBarLocked ? 92 : 130, edoModeBarLocked ? 92 : 130, edoModeBarLocked ? 96 : 130, 255);
    SDL_RenderRect(renderer, &modeButtonRect);
    if (fonts.mainFont) {
        const char* modeText = (tuningMode == TuningMode::Harmonic)
            ? (edoModeBarLocked ? "Mode: Harmonic (EDO: drag interval)" : "Mode: Harmonic")
            : "Mode: EDO";
        const SDL_Color modeColor =
            edoModeBarLocked ? SDL_Color{155, 160, 170, 255} : SDL_Color{230, 230, 230, 255};
        SDL_Surface* surf = TTF_RenderText_Blended(fonts.mainFont, modeText, 0, modeColor);
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

    // Channel picker (right of mode button)
    {
        const float chPickerX = modeButtonRect.x + modeButtonRect.w + 12.0f;
        const float chPickerY = dstRect->y + height - bottomMargin + 3.0f;
        const float chPickerH = std::max(12.0f, bottomMargin - 6.0f);
        const float btnW = 22.0f;

        channelLeftRect = {chPickerX, chPickerY, btnW, chPickerH};
        channelLabelRect = {chPickerX + btnW, chPickerY, 36.0f, chPickerH};
        channelRightRect = {chPickerX + btnW + 36.0f, chPickerY, btnW, chPickerH};

        // Left arrow button
        SDL_SetRenderDrawColor(renderer, 50, 50, 50, 255);
        SDL_RenderFillRect(renderer, &channelLeftRect);
        SDL_SetRenderDrawColor(renderer, 130, 130, 130, 255);
        SDL_RenderRect(renderer, &channelLeftRect);
        if (fonts.mainFont) {
            SDL_Surface* surf = TTF_RenderText_Blended(fonts.mainFont, "<", 0, {230, 230, 230, 255});
            if (surf) {
                SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
                if (tex) {
                    SDL_FRect tr{channelLeftRect.x + (channelLeftRect.w - surf->w) * 0.5f,
                                 channelLeftRect.y + (channelLeftRect.h - surf->h) * 0.5f,
                                 (float)surf->w, (float)surf->h};
                    SDL_RenderTexture(renderer, tex, nullptr, &tr);
                    SDL_DestroyTexture(tex);
                }
                SDL_DestroySurface(surf);
            }
        }

        // Channel label
        SDL_SetRenderDrawColor(renderer, 40, 40, 44, 255);
        SDL_RenderFillRect(renderer, &channelLabelRect);
        SDL_SetRenderDrawColor(renderer, 100, 100, 100, 255);
        SDL_RenderRect(renderer, &channelLabelRect);
        if (fonts.mainFont) {
            std::string chText = std::to_string(currentChannel);
            SDL_Surface* surf = TTF_RenderText_Blended(fonts.mainFont, chText.c_str(), 0, {230, 230, 230, 255});
            if (surf) {
                SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
                if (tex) {
                    SDL_FRect tr{channelLabelRect.x + (channelLabelRect.w - surf->w) * 0.5f,
                                 channelLabelRect.y + (channelLabelRect.h - surf->h) * 0.5f,
                                 (float)surf->w, (float)surf->h};
                    SDL_RenderTexture(renderer, tex, nullptr, &tr);
                    SDL_DestroyTexture(tex);
                }
                SDL_DestroySurface(surf);
            }
        }

        // Right arrow button
        SDL_SetRenderDrawColor(renderer, 50, 50, 50, 255);
        SDL_RenderFillRect(renderer, &channelRightRect);
        SDL_SetRenderDrawColor(renderer, 130, 130, 130, 255);
        SDL_RenderRect(renderer, &channelRightRect);
        if (fonts.mainFont) {
            SDL_Surface* surf = TTF_RenderText_Blended(fonts.mainFont, ">", 0, {230, 230, 230, 255});
            if (surf) {
                SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
                if (tex) {
                    SDL_FRect tr{channelRightRect.x + (channelRightRect.w - surf->w) * 0.5f,
                                 channelRightRect.y + (channelRightRect.h - surf->h) * 0.5f,
                                 (float)surf->w, (float)surf->h};
                    SDL_RenderTexture(renderer, tex, nullptr, &tr);
                    SDL_DestroyTexture(tex);
                }
                SDL_DestroySurface(surf);
            }
        }
    }

    renderPitchFactorsHoverTooltip();

    return true;
}

void PianoRoll::initWindow(SDL_Renderer* renderer) {

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
    for (auto* t : lineLabelTextures) SDL_DestroyTexture(t);
    lineLabelTextures.clear();

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
    setRenderColor(renderer, colors.background);

    SDL_RenderClear(renderer); // Clear backgroundTexture with the background color

    SDL_SetRenderTarget(renderer, target);

    if(height > (128*cellHeight12 - yMax - yMin)) {
        divHeight = 12*height/128;
        UpdateGrid();
        
    }
    
    Scroll();
    renderPianoRollGridTexture(renderer);

    RenderDestinations(renderer);

    RenderNotes(renderer);

}

void PianoRoll::RenderNotes(SDL_Renderer* renderer) {
    auto target = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, NotesTexture);
    SDL_SetRenderDrawColor(renderer,0,0,0,0);
    SDL_RenderClear(renderer);

    // Separate notes by channel: background channels first, then current channel on top
    std::vector<std::shared_ptr<Note>> bgNotes, chNotes;
    for (auto& note : region->notes) {
        if (note->channel == currentChannel)
            chNotes.push_back(note);
        else
            bgNotes.push_back(note);
    }

    // Background channel notes
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    for (auto& note : bgNotes) {
        float noteX = getNotePosX(note) +1;
        float noteY = getY(noteMidiForRender(note));
        float noteEnd = getNoteEnd(note) -2;
        float noteTop = noteY + noteHeight;

        SDL_SetRenderDrawColor(renderer, colors.noteBackground[0], colors.noteBackground[1],
                               colors.noteBackground[2], colors.noteBackground[3] / 4);
        SDL_FRect noteBGRect = { noteX, noteY, noteEnd - noteX, noteTop-noteY};
        SDL_RenderFillRect(renderer, &noteBGRect);
    }

    for (auto& note : bgNotes) {
        float noteX = getNotePosX(note) +1;
        float noteY = getY(noteMidiForRender(note));
        float noteEnd = getNoteEnd(note) -2;

        bool selected = selectedNoteIds.count(note->id) > 0;
        auto& col = selected ? colors.noteSelected : colors.note;
        auto& bcol = selected ? colors.noteSelectedBorder : colors.noteBorder;

        SDL_SetRenderDrawColor(renderer, col[0], col[1], col[2], col[3] / 4);
        SDL_FRect noteRect = { noteX, noteY - noteRadius, noteEnd - noteX, 2*noteRadius};
        SDL_RenderFillRect(renderer, &noteRect);

        SDL_SetRenderDrawColor(renderer, bcol[0], bcol[1], bcol[2], bcol[3] / 4);
        SDL_RenderRect(renderer, &noteRect);
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    // Current channel notes: backgrounds
    for (auto& note : chNotes) {
        float noteX = getNotePosX(note) +1;
        float noteY = getY(noteMidiForRender(note));
        float noteEnd = getNoteEnd(note) -2;
        float noteTop = noteY + noteHeight;

        setRenderColor(renderer, colors.noteBackground);
        SDL_FRect noteBGRect = { noteX, noteY, noteEnd - noteX, noteTop-noteY};
        SDL_RenderFillRect(renderer, &noteBGRect);
    }

    // Current channel notes: rects
    for (auto& note : chNotes) {
        float noteX = getNotePosX(note) +1;
        float noteY = getY(noteMidiForRender(note));
        float noteEnd = getNoteEnd(note) -2;

        bool selected = selectedNoteIds.count(note->id) > 0;

        setRenderColor(renderer, selected ? colors.noteSelected : colors.note);
        SDL_FRect noteRect = { noteX, noteY - noteRadius, noteEnd - noteX, 2*noteRadius};
        SDL_RenderFillRect(renderer, &noteRect);

        setRenderColor(renderer, selected ? colors.noteSelectedBorder : colors.noteBorder);
        SDL_RenderRect(renderer, &noteRect);
    }

    // Rubber band selection rect
    if (selectingRubberBand) {
        float bx = std::min(rubberBandStartX, rubberBandEndX);
        float by = std::min(rubberBandStartY, rubberBandEndY);
        float bw = std::fabs(rubberBandEndX - rubberBandStartX);
        float bh = std::fabs(rubberBandEndY - rubberBandStartY);
        SDL_FRect bandRect = {bx, by, bw, bh};
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 100, 150, 255, 40);
        SDL_RenderFillRect(renderer, &bandRect);
        SDL_SetRenderDrawColor(renderer, 100, 150, 255, 180);
        SDL_RenderRect(renderer, &bandRect);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }

    SDL_SetRenderTarget(renderer, target);

}

bool PianoRoll::getExistingNote() {
    hoveredElement = nullptr;
    if(mouseY < topMargin || mouseX < leftMargin) {
        return false;
    }
    for (std::shared_ptr<Note> note : region->notes) {
        if (note->channel != currentChannel) continue;

        const int notePosX = getNotePosX(note);
        const int noteEnd = getNoteEnd(note);
        const int noteY = getY(noteMidiForRender(note));

        if (mouseX >= notePosX && mouseX <= noteEnd &&
            mouseY <= noteY + noteRadius && mouseY >= (noteY - noteRadius)) {
            hoveredElement = note;
            lastHoveredLine = getHoveredLine();
            return true;
        }
    }
    return false;
}


float PianoRoll::getNotePosX(std::shared_ptr<Note> note) {
    if (movingNote && (selectedNoteIds.count(note->id) || movingNote.get() == note.get()) && movingNoteRhythmPreviewLineIdx) {
        const size_t rli = *movingNoteRhythmPreviewLineIdx;
        if (rli < rhythmLines.size()) {
            const auto startDelta = subVec(rhythmLines[rli].integerPairs, rhythmDragStartPairs);
            return getX(Note::beatsFromVector(addVec(note->rhythmVector, startDelta)));
        }
    }
    return getX(note->startSeconds());
}

float PianoRoll::getNoteEnd(std::shared_ptr<Note> note) {
    if (movingNote && (selectedNoteIds.count(note->id) || movingNote.get() == note.get()) && movingNoteRhythmPreviewLineIdx) {
        const size_t rli = *movingNoteRhythmPreviewLineIdx;
        if (rli < rhythmLines.size()) {
            const auto startDelta = subVec(rhythmLines[rli].integerPairs, rhythmDragStartPairs);
            return getX(Note::beatsFromVector(addVec(note->rhythmEndVector, startDelta)));
        }
    }
    return getX(note->endSeconds());
}

