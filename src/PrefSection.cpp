#include "PrefSection.h"
#include "WindowHandler.h"
#include "ContextMenu.h"
#include "Settings.h"
#include "AudioManager.h"
#include "styles.h"
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// --- Symbol drawing ---

static void drawFilledCircle(SDL_Renderer* r, float cx, float cy, float radius, int segs) {
    std::vector<SDL_Vertex> verts(static_cast<size_t>(segs) + 2);
    SDL_FColor white{0.90f, 0.90f, 0.94f, 1.f};
    verts[0].position = {cx, cy};
    verts[0].color = white;
    for (int i = 0; i <= segs; ++i) {
        float a = static_cast<float>(i) * 2.f * static_cast<float>(M_PI) / static_cast<float>(segs);
        verts[static_cast<size_t>(i) + 1].position = {cx + radius * std::cos(a), cy + radius * std::sin(a)};
        verts[static_cast<size_t>(i) + 1].color = white;
    }
    std::vector<int> idx(static_cast<size_t>(segs) * 3);
    for (int i = 0; i < segs; ++i) {
        idx[static_cast<size_t>(i) * 3 + 0] = 0;
        idx[static_cast<size_t>(i) * 3 + 1] = i + 1;
        idx[static_cast<size_t>(i) * 3 + 2] = i + 2;
    }
    SDL_RenderGeometry(r, nullptr, verts.data(), static_cast<int>(verts.size()),
                       idx.data(), static_cast<int>(idx.size()));
}

void AudioSection::drawSymbol(SDL_Renderer* r, float cx, float cy, float sz) const {
    const float rad = sz * 0.2f;
    const float headY = cy + sz * 0.3f;
    const float stemX = cx + rad * 0.85f;
    const float stemTop = cy - sz * 0.35f;

    SDL_SetRenderDrawColor(r, 230, 230, 240, 255);

    drawFilledCircle(r, cx, headY, rad, 14);

    SDL_RenderLine(r, stemX, headY, stemX, stemTop);

    const float fx = stemX + sz * 0.28f;
    SDL_RenderLine(r, stemX, stemTop, fx, stemTop + sz * 0.16f);
    SDL_RenderLine(r, stemX, stemTop + sz * 0.08f, fx, stemTop + sz * 0.22f);
}

void GUISection::drawSymbol(SDL_Renderer* r, float cx, float cy, float sz) const {
    const float hw = sz * 0.36f, hh = sz * 0.30f;
    SDL_SetRenderDrawColor(r, 230, 230, 240, 255);

    SDL_FRect frame{cx - hw, cy - hh, hw * 2.f, hh * 2.f};
    SDL_RenderRect(r, &frame);

    const float tbY = cy - hh + sz * 0.11f;
    SDL_RenderLine(r, cx - hw, tbY, cx + hw, tbY);

    const float bbY = cy + hh - sz * 0.09f;
    SDL_RenderLine(r, cx - hw, bbY, cx + hw, bbY);

    const float ci = sz * 0.08f;
    SDL_RenderLine(r, cx - ci, cy, cx + ci, cy);
    SDL_RenderLine(r, cx, cy - ci, cx, cy + ci);
}

void ControlsSection::drawSymbol(SDL_Renderer* r, float cx, float cy, float sz) const {
    const float hw = sz * 0.32f, hh = sz * 0.28f;
    SDL_SetRenderDrawColor(r, 230, 230, 240, 255);

    SDL_FRect frame{cx - hw, cy - hh, hw * 2.f, hh * 2.f};
    SDL_RenderRect(r, &frame);

    for (int k = -1; k <= 1; ++k) {
        const float sx = cx + static_cast<float>(k) * sz * 0.17f;
        const float sTop = cy - hh + sz * 0.06f;
        const float sBot = cy + hh - sz * 0.06f;
        SDL_RenderLine(r, sx, sTop, sx, sBot);

        const float knobY = cy + static_cast<float>(k) * sz * 0.06f;
        drawFilledCircle(r, sx, knobY, sz * 0.05f, 8);
    }
}

// --- Title rendering with word wrapping ---

static void renderTextCentered(SDL_Renderer* r, const char* t, float cx, float y, float s,
                               const SDL_Color& col) {
    if (!fonts.mainFont) return;
    SDL_Surface* sf = TTF_RenderText_Blended(fonts.mainFont, t, 0, col);
    if (!sf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(r, sf);
    if (tex) {
        SDL_FRect tr{cx - static_cast<float>(sf->w) * s * 0.5f, y,
                     static_cast<float>(sf->w) * s, static_cast<float>(sf->h) * s};
        SDL_RenderTexture(r, tex, nullptr, &tr);
        SDL_DestroyTexture(tex);
    }
    SDL_DestroySurface(sf);
}

static void renderTextLeft(SDL_Renderer* r, const char* t, float x, float y, float s,
                           const SDL_Color& col) {
    if (!fonts.mainFont) return;
    SDL_Surface* sf = TTF_RenderText_Blended(fonts.mainFont, t, 0, col);
    if (!sf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(r, sf);
    if (tex) {
        SDL_FRect tr{x, y, static_cast<float>(sf->w) * s, static_cast<float>(sf->h) * s};
        SDL_RenderTexture(r, tex, nullptr, &tr);
        SDL_DestroyTexture(tex);
    }
    SDL_DestroySurface(sf);
}

// Returns the y position just after the rendered title (bottom of last line + gap).
static float renderSectionTitle(SDL_Renderer* r, const char* t, const SDL_FRect& b, float s) {
    if (!fonts.mainFont) return b.y + 16.f * s;

    const float cx = b.x + b.w * 0.5f;
    const float Ri = b.w * 0.5f;
    const float ty = b.y + 16.f * s;
    const float dy = ty - b.y;
    const float halfChord = std::sqrt(std::max(0.f, Ri * Ri - (Ri - dy) * (Ri - dy)));
    const float usableW = 2.f * halfChord - 12.f * s;
    const SDL_Color col{230, 230, 240, 255};

    int tw = 0, th = 0;
    if (!TTF_GetStringSize(fonts.mainFont, t, 0, &tw, &th)) {
        renderTextCentered(r, t, cx, ty, s, col);
        return ty + static_cast<float>(th) * s + 8.f * s;
    }
    const float fullW = static_cast<float>(tw) * s;

    if (fullW <= usableW) {
        renderTextCentered(r, t, cx, ty, s, col);
        return ty + static_cast<float>(th) * s + 8.f * s;
    }

    // Smart wrap: split into words, greedily fill each line.
    std::string text(t);
    std::vector<std::string> words;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t sp = text.find(' ', pos);
        if (sp == std::string::npos) {
            words.push_back(text.substr(pos));
            break;
        }
        if (sp > pos) words.push_back(text.substr(pos, sp - pos));
        pos = sp + 1;
    }
    if (words.empty()) {
        renderTextCentered(r, t, cx, ty, s, col);
        return ty + static_cast<float>(th) * s + 8.f * s;
    }

    // Build lines greedily.
    std::vector<std::string> lines;
    size_t wi = 0;
    while (wi < words.size()) {
        std::string cur = words[wi++];
        while (wi < words.size()) {
            std::string trial = cur + " " + words[wi];
            int lw = 0, lh = 0;
            TTF_GetStringSize(fonts.mainFont, trial.c_str(), 0, &lw, &lh);
            if (static_cast<float>(lw) * s <= usableW) {
                cur = trial;
                ++wi;
            } else {
                break;
            }
        }
        lines.push_back(cur);
    }

    float curY = ty;
    for (size_t i = 0; i < lines.size(); ++i) {
        int lw = 0, lh = 0;
        TTF_GetStringSize(fonts.mainFont, lines[i].c_str(), 0, &lw, &lh);
        renderTextCentered(r, lines[i].c_str(), cx, curY, s, col);
        curY += static_cast<float>(lh) * s + 3.f * s;
    }
    return curY + 5.f * s; // padding after last line
}

// --- Setting widget rendering ---

static constexpr float kRowH = 22.f;

static SDL_FRect renderSettingBool(SDL_Renderer* r, const SettingDesc& d, const SDL_FRect& b,
                                    float y, float s, const SDL_Color& col) {
    const float cx = b.x + b.w * 0.5f;
    const float cbSz = 11.f * s;
    const float cbTop = y + (kRowH * s - cbSz) * 0.5f;

    int lw = 0, lh = 0;
    TTF_GetStringSize(fonts.mainFont, d.label, 0, &lw, &lh);
    const float lblW = static_cast<float>(lw) * s;
    const float gap = 6.f * s;
    const float groupW = cbSz + gap + lblW;
    const float cbX = cx - groupW * 0.5f;

    // Checkbox
    SDL_FRect cb{cbX, cbTop, cbSz, cbSz};
    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
    SDL_RenderRect(r, &cb);
    if (Settings::instance().getBool(d.key, false)) {
        SDL_SetRenderDrawColor(r, 150, 210, 150, 255);
        SDL_FRect fill{cbX + 2.f * s, cbTop + 2.f * s, cbSz - 4.f * s, cbSz - 4.f * s};
        SDL_RenderFillRect(r, &fill);
    }

    // Label
    const float lblX = cbX + cbSz + gap;
    const float lblY = y + (kRowH * s - static_cast<float>(lh) * s) * 0.5f;
    renderTextLeft(r, d.label, lblX, lblY, s, col);

    // Hit rect for the whole row
    return {cbX - 4.f * s, y, groupW + 8.f * s, kRowH * s};
}

// Extract the idx-th pipe-separated label into buf (max bufsz).
static void cycleLabelForValue(const SettingDesc& d, int val, char* buf, size_t bufsz) {
    if (!d.cycleLabels) { buf[0] = '\0'; return; }
    const char* p = d.cycleLabels;
    int idx = (val - d.minVal) / d.step;
    for (int i = 0; i < idx && p; ++i) {
        p = strchr(p, '|');
        if (p) ++p;
    }
    if (!p) { buf[0] = '\0'; return; }
    const char* end = strchr(p, '|');
    size_t len = end ? static_cast<size_t>(end - p) : strlen(p);
    if (len >= bufsz) len = bufsz - 1;
    memcpy(buf, p, len);
    buf[len] = '\0';
}

static SDL_FRect renderSettingInt(SDL_Renderer* r, const SettingDesc& d, const SDL_FRect& b,
                                   float y, float s, const SDL_Color& col) {
    const float cx = b.x + b.w * 0.5f;
    const int val = Settings::instance().getInt(d.key, d.minVal);

    int lw = 0, lh = 0;
    TTF_GetStringSize(fonts.mainFont, d.label, 0, &lw, &lh);
    const float lblW = static_cast<float>(lw) * s;
    const float gap = 8.f * s;

    // Value text
    char valBuf[32];
    int vw = 0, vh = 0;
    if (d.cycleLabels) {
        cycleLabelForValue(d, val, valBuf, sizeof(valBuf));
    } else {
        snprintf(valBuf, sizeof(valBuf), "%d", val);
    }
    TTF_GetStringSize(fonts.mainFont, valBuf, 0, &vw, &vh);

    const float pad = 14.f * s;
    const float valW = static_cast<float>(vw) * s + pad * 2.f;
    const float groupW = lblW + gap + valW;
    const float left = cx - groupW * 0.5f;

    // Label
    const float lblY = y + (kRowH * s - static_cast<float>(lh) * s) * 0.5f;
    renderTextLeft(r, d.label, left, lblY, s, col);

    // Value box / toggle button
    const float boxX = left + lblW + gap;
    const float boxH = kRowH * s;
    SDL_FRect valBox{boxX, y, valW, boxH};
    SDL_SetRenderDrawColor(r, 50, 50, 58, 255);
    SDL_RenderFillRect(r, &valBox);
    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
    SDL_RenderRect(r, &valBox);

    // Value text centered in box
    renderTextCentered(r, valBuf, boxX + valW * 0.5f,
                       y + (boxH - static_cast<float>(vh) * s) * 0.5f, s, col);

    return {left - 4.f * s, y, groupW + 8.f * s, kRowH * s};
}

static bool hitTestSetting(const SettingDesc& d, float mx, float my, const SDL_FRect& b,
                           float y, float s) {
    const float cx = b.x + b.w * 0.5f;

    if (d.type == SettingType::Bool) {
        const float cbSz = 11.f * s;
        int lw = 0, lh = 0;
        TTF_GetStringSize(fonts.mainFont, d.label, 0, &lw, &lh);
        const float lblW = static_cast<float>(lw) * s;
        const float gap = 6.f * s;
        const float groupW = cbSz + gap + lblW;
        const float left = cx - groupW * 0.5f;
        if (mx >= left - 4.f * s && mx <= left + groupW + 4.f * s &&
            my >= y && my <= y + kRowH * s) {
            Settings::instance().setBool(d.key, !Settings::instance().getBool(d.key, false));
            if (d.onChange) d.onChange();
            return true;
        }
    } else if (d.type == SettingType::Int) {
        int val = Settings::instance().getInt(d.key, d.minVal);
        char valBuf[32];
        if (d.cycleLabels) {
            cycleLabelForValue(d, val, valBuf, sizeof(valBuf));
        } else {
            snprintf(valBuf, sizeof(valBuf), "%d", val);
        }
        int vw = 0, vh = 0;
        TTF_GetStringSize(fonts.mainFont, valBuf, 0, &vw, &vh);
        int lw = 0, lh = 0;
        TTF_GetStringSize(fonts.mainFont, d.label, 0, &lw, &lh);
        const float lblW = static_cast<float>(lw) * s;
        const float pad = 14.f * s;
        const float valW = static_cast<float>(vw) * s + pad * 2.f;
        const float gap = 8.f * s;
        const float boxX = cx - (lblW + gap + valW) * 0.5f + lblW + gap;

        if (mx >= boxX && mx < boxX + valW && my >= y && my <= y + kRowH * s) {
            if (d.cycleLabels) {
                int next = val + d.step;
                if (next > d.maxVal) next = d.minVal;
                Settings::instance().setInt(d.key, next);
                if (d.onChange) d.onChange();
                return true;
            }
            auto* proj = WindowHandler::instance()->project;
            auto* ctxMenu = ContextMenu::get();
            ctxMenu->activate();
            ctxMenu->locX = boxX;
            ctxMenu->locY = y;
            if (proj && proj->window)
                SDL_StartTextInput(proj->window);
            ctxMenu->dynamicTick = getTextInputTicker(
                [key = d.key, minV = d.minVal, maxV = d.maxVal, onChange = d.onChange](std::string text) {
                    try {
                        int v = std::stoi(text);
                        if (v < minV) v = minV;
                        if (v > maxV) v = maxV;
                        Settings::instance().setInt(key, v);
                        if (onChange) onChange();
                    } catch (...) {}
                },
                nullptr,
                valBuf
            );
            return true;
        }
    }
    return false;
}

// --- Section content rendering ---

// Helper: compute start Y for settings so they are centered in the space
// between titleBottom and the bottom of the circle (with margin).
static float settingsStartY(float titleBottom, const SDL_FRect& b, float s,
                            size_t numSettings, float rowH) {
    const float contentBottom = b.y + b.h - 12.f * s; // margin from bottom of circle
    const float avail = contentBottom - titleBottom;
    const float totalH = static_cast<float>(numSettings) * rowH * s;
    if (totalH >= avail) return titleBottom;
    return titleBottom + (avail - totalH) * 0.5f;
}

// --- Device selector helpers ---

static void renderDeviceRow(SDL_Renderer* r, const char* label, const char* valueName,
                            float y, const SDL_FRect& b, float s, const SDL_Color& col) {
    const float cx = b.x + b.w * 0.5f;

    int lw = 0, lh = 0;
    TTF_GetStringSize(fonts.mainFont, label, 0, &lw, &lh);
    const float lblW = static_cast<float>(lw) * s;
    const float lblY = y + (kRowH * s - static_cast<float>(lh) * s) * 0.5f;
    renderTextLeft(r, label, cx - (lblW + 8.f * s + 130.f * s) * 0.5f, lblY, s, col);

    // Value box
    const float pad = 8.f * s;
    const float boxX = cx - (lblW + 8.f * s + 130.f * s) * 0.5f + lblW + 8.f * s;
    const float boxW = 130.f * s;
    const float boxH = kRowH * s;
    SDL_FRect valBox{boxX, y, boxW, boxH};
    SDL_SetRenderDrawColor(r, 50, 50, 58, 255);
    SDL_RenderFillRect(r, &valBox);
    SDL_SetRenderDrawColor(r, 90, 90, 100, 255);
    SDL_RenderRect(r, &valBox);

    // Device name (truncated)
    char nameBuf[32];
    snprintf(nameBuf, sizeof(nameBuf), "%.25s", valueName);
    int nw = 0, nh = 0;
    TTF_GetStringSize(fonts.mainFont, nameBuf, 0, &nw, &nh);
    const float scale = std::min(s, (boxW - pad * 2.f) / static_cast<float>(nw));
    const float tw = static_cast<float>(nw) * scale;
    const float th = static_cast<float>(nh) * scale;
    SDL_Color tc{200, 200, 210, 255};
    renderTextLeft(r, nameBuf, boxX + pad, y + (boxH - th) * 0.5f, scale, tc);
}

static bool hitTestDeviceRow(float mx, float my, const char* label,
                             float y, const SDL_FRect& b, float s,
                             const char* settingsKey,
                             std::vector<RtAudio::DeviceInfo> devices,
                             int noneId, const char* noneLabel) {
    const float cx = b.x + b.w * 0.5f;
    int lw = 0, lh = 0;
    TTF_GetStringSize(fonts.mainFont, label, 0, &lw, &lh);
    const float lblW = static_cast<float>(lw) * s;
    const float boxX = cx - (lblW + 8.f * s + 130.f * s) * 0.5f + lblW + 8.f * s;
    const float boxW = 130.f * s;
    if (mx < boxX || mx > boxX + boxW || my < y || my > y + kRowH * s)
        return false;

    auto tree = uTreeEntry();
    auto noneEntry = uTreeEntry();
    noneEntry->label = noneLabel;
    noneEntry->click = [key = settingsKey, noneId] {
        Settings::instance().setInt(key, noneId);
        AudioManager::instance()->restart();
    };
    tree->addChild(noneEntry);
    for (auto& dev : devices) {
        auto entry = uTreeEntry();
        entry->label = dev.name;
        int devId = static_cast<int>(dev.ID);
        entry->click = [key = settingsKey, devId] {
            Settings::instance().setInt(key, devId);
            AudioManager::instance()->restart();
        };
        tree->addChild(entry);
    }

    auto* ctx = ContextMenu::get();
    ctx->activate();
    ctx->locX = boxX;
    ctx->locY = y + kRowH * s;
    ctx->dynamicTick = getTreeMenuTicker(tree);
    return true;
}

// --- AudioSection ---

const std::vector<SettingDesc>& AudioSection::settings() const {
    static const std::vector<SettingDesc> s = {
        {SettingType::Int, "audioEngine", "Audio engine", 0, 1, 1, "SDL|RtAudio",
         []{ AudioManager::instance()->restart(); }},
        {SettingType::Int, "audioBufferSize", "Buffer size", 0, 6, 1, "64|128|256|512|1024|2048|4096",
         []{ AudioManager::instance()->restart(); }},
        {SettingType::Int, "audioSampleRate", "Sample rate", 0, 4, 1, "Auto|44100|48000|96000|192000",
         []{ AudioManager::instance()->restart(); }},
        {SettingType::Bool, "audioTripleBuffer", "Triple buffer", 0, 0, 0, nullptr,
         []{ AudioManager::instance()->restart(); }},
    };
    return s;
}

// Shared title-bottom calculation so render and hit-test are always aligned.
static float audioTitleBottom(const SDL_FRect& b, float s) {
    int th = 0;
    TTF_GetStringSize(fonts.mainFont, "Audio Settings", 0, nullptr, &th);
    return b.y + 16.f * s + static_cast<float>(th) * s + 8.f * s;
}

void AudioSection::renderContent(SDL_Renderer* r, const SDL_FRect& b, float s) {
    float afterTitle = renderSectionTitle(r, "Audio Settings", b, s);
    afterTitle_ = afterTitle;

    const SDL_Color col{180, 180, 195, 255};
    auto* am = AudioManager::instance();

    static constexpr int kDeviceRows = 2;
    float y = settingsStartY(afterTitle, b, s, settings().size() + kDeviceRows, kRowH);

    // Output device
    int outDev = Settings::instance().audioOutputDevice();
    std::string outName = outDev < 0 ? "Default Output" : am->getDeviceName(outDev);
    renderDeviceRow(r, "Output device", outName.c_str(), y, b, s, col);
    y += kRowH * s;

    // Input device
    int inDev = Settings::instance().audioInputDevice();
    std::string inName = inDev < 0 ? "None" : am->getDeviceName(inDev);
    renderDeviceRow(r, "Input device", inName.c_str(), y, b, s, col);
    y += kRowH * s;

    // Standard settings
    for (auto& d : settings()) {
        if (d.type == SettingType::Bool)
            renderSettingBool(r, d, b, y, s, col);
        else if (d.type == SettingType::Int)
            renderSettingInt(r, d, b, y, s, col);
        y += kRowH * s;
    }
}

bool AudioSection::handleContentInput(SDL_Event& e, float mx, float my,
                                      const SDL_FRect& b) {
    if (e.type != SDL_EVENT_MOUSE_BUTTON_DOWN || e.button.button != SDL_BUTTON_LEFT)
        return false;

    const float s = contentScale_;
    float y = settingsStartY(afterTitle_, b, s, settings().size() + 2, kRowH);

    // Output device row
    if (hitTestDeviceRow(mx, my, "Output device", y, b, s,
                         "audioOutputDevice",
                         AudioManager::instance()->getOutputDevices(),
                         -1, "Default Output"))
        return true;
    y += kRowH * s;

    // Input device row
    if (hitTestDeviceRow(mx, my, "Input device", y, b, s,
                         "audioInputDevice",
                         AudioManager::instance()->getInputDevices(),
                         -1, "None"))
        return true;
    y += kRowH * s;

    // Standard settings
    for (auto& d : settings()) {
        if (hitTestSetting(d, mx, my, b, y, s))
            return true;
        y += kRowH * s;
    }
    return false;
}

const std::vector<SettingDesc>& GUISection::settings() const {
    static const std::vector<SettingDesc> s = {
        {SettingType::Bool, "showFps", "Show FPS"},
        {SettingType::Int, "portDisplayMode", "Port display", 0, 1, 1, "Labels|Square"},
    };
    return s;
}

void GUISection::renderContent(SDL_Renderer* r, const SDL_FRect& b, float s) {
    float afterTitle = renderSectionTitle(r, "GUI Settings", b, s);
    afterTitle_ = afterTitle;
    auto& descs = settings();
    float y = settingsStartY(afterTitle, b, s, descs.size(), kRowH);

    const SDL_Color col{180, 180, 195, 255};
    for (auto& d : descs) {
        if (d.type == SettingType::Bool)
            renderSettingBool(r, d, b, y, s, col);
        else if (d.type == SettingType::Int)
            renderSettingInt(r, d, b, y, s, col);
        y += kRowH * s;
    }
}

bool GUISection::handleContentInput(SDL_Event& e, float mx, float my,
                                    const SDL_FRect& b) {
    if (e.type != SDL_EVENT_MOUSE_BUTTON_DOWN || e.button.button != SDL_BUTTON_LEFT)
        return false;

    const float s = contentScale_;
    float y = settingsStartY(afterTitle_, b, s, settings().size(), kRowH);
    for (auto& d : settings()) {
        if (hitTestSetting(d, mx, my, b, y, s))
            return true;
        y += kRowH * s;
    }
    return false;
}

const std::vector<SettingDesc>& ControlsSection::settings() const {
    static const std::vector<SettingDesc> s = {
        {SettingType::Int, "doubleClickTimeMs", "Double-click time (ms)", 50, 1000, 10},
    };
    return s;
}

void ControlsSection::renderContent(SDL_Renderer* r, const SDL_FRect& b, float s) {
    float afterTitle = renderSectionTitle(r, "Controls Settings", b, s);
    afterTitle_ = afterTitle;
    auto& descs = settings();
    float y = settingsStartY(afterTitle, b, s, descs.size(), kRowH);

    const SDL_Color col{180, 180, 195, 255};
    for (auto& d : descs) {
        if (d.type == SettingType::Int)
            renderSettingInt(r, d, b, y, s, col);
        y += kRowH * s;
    }
}

bool ControlsSection::handleContentInput(SDL_Event& e, float mx, float my,
                                         const SDL_FRect& b) {
    if (e.type != SDL_EVENT_MOUSE_BUTTON_DOWN || e.button.button != SDL_BUTTON_LEFT)
        return false;

    const float s = contentScale_;
    float y = settingsStartY(afterTitle_, b, s, settings().size(), kRowH);
    for (auto& d : settings()) {
        if (hitTestSetting(d, mx, my, b, y, s))
            return true;
        y += kRowH * s;
    }
    return false;
}

// --- General section (Load Defaults / Restore) ---

void GeneralSection::drawSymbol(SDL_Renderer* r, float cx, float cy, float sz) const {
    const float rad = sz * 0.28f;
    SDL_SetRenderDrawColor(r, 230, 230, 240, 255);

    // Circular arrow (reset/restore icon)
    const float startAngle = static_cast<float>(M_PI) * 0.3f;
    const float endAngle = static_cast<float>(M_PI) * 1.9f;
    const int segs = 24;
    for (int i = 0; i < segs; ++i) {
        float a0 = startAngle + (endAngle - startAngle) * static_cast<float>(i) / static_cast<float>(segs);
        float a1 = startAngle + (endAngle - startAngle) * static_cast<float>(i + 1) / static_cast<float>(segs);
        SDL_RenderLine(r, cx + rad * std::cos(a0), cy + rad * std::sin(a0),
                       cx + rad * std::cos(a1), cy + rad * std::sin(a1));
    }

    // Arrowhead at the end
    const float tipX = cx + rad * std::cos(endAngle);
    const float tipY = cy + rad * std::sin(endAngle);
    const float arrLen = sz * 0.12f;
    const float arrAngle = endAngle + static_cast<float>(M_PI) * 0.6f;
    SDL_RenderLine(r, tipX, tipY,
                   tipX + arrLen * std::cos(arrAngle),
                   tipY + arrLen * std::sin(arrAngle));
    SDL_RenderLine(r, tipX, tipY,
                   tipX + arrLen * std::cos(arrAngle + static_cast<float>(M_PI) * 0.5f),
                   tipY + arrLen * std::sin(arrAngle + static_cast<float>(M_PI) * 0.5f));
}

void GeneralSection::renderContent(SDL_Renderer* r, const SDL_FRect& b, float s) {
    float afterTitle = renderSectionTitle(r, "General", b, s);

    size_t numRows = 1 + (Settings::instance().hasBackup() ? 1 : 0);
    float y = settingsStartY(afterTitle, b, s, numRows, kRowH);
    const SDL_Color col{180, 180, 195, 255};

    const float cx = b.x + b.w * 0.5f;
    const float pad = 10.f * s;
    const float btnH = kRowH * s - 2.f * s;

    auto renderBtn = [&](const char* label, float by) {
        int tw = 0, th2 = 0;
        TTF_GetStringSize(fonts.mainFont, label, 0, &tw, &th2);
        const float btnW = static_cast<float>(tw) * s + pad * 2.f;
        const float btnX = cx - btnW * 0.5f;
        SDL_FRect rect{btnX, by, btnW, btnH};
        SDL_SetRenderDrawColor(r, 50, 50, 58, 255);
        SDL_RenderFillRect(r, &rect);
        SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
        SDL_RenderRect(r, &rect);
        renderTextCentered(r, label, cx, by + (btnH - static_cast<float>(th2) * s) * 0.5f, s, col);
        return rect;
    };

    renderBtn("Load Defaults", y);
    y += btnH + 4.f * s;

    if (Settings::instance().hasBackup())
        renderBtn("Restore Previous", y);
}

bool GeneralSection::handleContentInput(SDL_Event& e, float mx, float my,
                                        const SDL_FRect& b) {
    if (e.type != SDL_EVENT_MOUSE_BUTTON_DOWN || e.button.button != SDL_BUTTON_LEFT)
        return false;

    const float s = 1.f;
    int th = 0;
    TTF_GetStringSize(fonts.mainFont, "General", 0, nullptr, &th);
    float titleBottom = b.y + 16.f + static_cast<float>(th) + 8.f;

    size_t numRows = 1 + (Settings::instance().hasBackup() ? 1 : 0);
    float y = settingsStartY(titleBottom, b, s, numRows, kRowH);

    const float cx = b.x + b.w * 0.5f;
    const float pad = 10.f * s;
    const float btnH = kRowH * s - 2.f * s;

    auto hitBtn = [&](const char* label, float by) -> SDL_FRect {
        int tw = 0, th2 = 0;
        TTF_GetStringSize(fonts.mainFont, label, 0, &tw, &th2);
        const float bw = static_cast<float>(tw) * s + pad * 2.f;
        const float bx = cx - bw * 0.5f;
        return {bx, by, bw, btnH};
    };

    SDL_FRect ldRect = hitBtn("Load Defaults", y);
    if (mx >= ldRect.x && mx < ldRect.x + ldRect.w && my >= ldRect.y && my < ldRect.y + ldRect.h) {
        Settings::instance().loadDefaults();
        return true;
    }
    y += btnH + 4.f * s;

    if (Settings::instance().hasBackup()) {
        SDL_FRect rpRect = hitBtn("Restore Previous", y);
        if (mx >= rpRect.x && mx < rpRect.x + rpRect.w && my >= rpRect.y && my < rpRect.y + rpRect.h) {
            Settings::instance().restoreBackup();
            return true;
        }
    }
    return false;
}
