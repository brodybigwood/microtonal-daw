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

#ifndef __EMSCRIPTEN__
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
#endif // __EMSCRIPTEN__

// --- AudioSection ---

const std::vector<SettingDesc>& AudioSection::settings() const {
    static const std::vector<SettingDesc> s = {
        {SettingType::Int, "audioEngine", "Audio engine", 0, 1, 1,
#ifndef __EMSCRIPTEN__
         "SDL|RtAudio",
#else
         "SDL",
#endif
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
#ifndef __EMSCRIPTEN__
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
#else
    float y = settingsStartY(afterTitle, b, s, settings().size(), kRowH);
#endif

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
#ifndef __EMSCRIPTEN__
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
#else
    float y = settingsStartY(afterTitle_, b, s, settings().size(), kRowH);
#endif

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

// --- ColorsSection ---------------------------------------------------------------

static const char* kColorKeys[] = {
    "background", "grid", "note", "noteSelected", "noteBorder",
    "noteSelectedBorder", "noteBackground", "keyWhite", "playHead",
    "trackBackground", "trackBody", "trackBorder",
    "trackAudio", "trackNotes", "trackAutomation", "nodeGraphBg", "elementListBg"
};
static constexpr int kNumColorKeys = static_cast<int>(sizeof(kColorKeys) / sizeof(kColorKeys[0]));

static void hsvToRgb(float h, float s, float v, uint8_t out[4]) {
    float r, g, b;
    int i = static_cast<int>(h * 6.f);
    float f = h * 6.f - static_cast<float>(i);
    float p = v * (1.f - s);
    float q = v * (1.f - f * s);
    float t = v * (1.f - (1.f - f) * s);
    switch (i % 6) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
    out[0] = static_cast<uint8_t>(r * 255.f);
    out[1] = static_cast<uint8_t>(g * 255.f);
    out[2] = static_cast<uint8_t>(b * 255.f);
    out[3] = 255;
}

static void rgbToHsv(const uint8_t rgb[4], float& h, float& s, float& v) {
    float r = rgb[0] / 255.f, g = rgb[1] / 255.f, b = rgb[2] / 255.f;
    float mx = std::max({r, g, b}), mn = std::min({r, g, b});
    float d = mx - mn;
    v = mx;
    s = mx < 0.001f ? 0.f : d / mx;
    if (d < 0.001f) { h = 0.f; return; }
    if (mx == r)      h = (g - b) / d + (g < b ? 6.f : 0.f);
    else if (mx == g) h = (b - r) / d + 2.f;
    else              h = (r - g) / d + 4.f;
    h /= 6.f;
}

static uint8_t* colorKeyToField(int idx) {
    switch (idx) {
        case 0: return colors.background;
        case 1: return colors.grid;
        case 2: return colors.note;
        case 3: return colors.noteSelected;
        case 4: return colors.noteBorder;
        case 5: return colors.noteSelectedBorder;
        case 6: return colors.noteBackground;
        case 7: return colors.keyWhite;
        case 8: return colors.playHead;
        case 9: return colors.trackBackground;
        case 10: return colors.trackBody;
        case 11: return colors.trackBorder;
        case 12: return colors.trackAudio;
        case 13: return colors.trackNotes;
        case 14: return colors.trackAutomation;
        case 15: return colors.nodeGraphBg;
        case 16: return colors.elementListBg;
        default: return nullptr;
    }
}

void ColorsSection::drawSymbol(SDL_Renderer* r, float cx, float cy, float sz) const {
    float s2 = sz * 0.25f;
    SDL_SetRenderDrawColor(r, 220, 90, 90, 255);
    SDL_FRect sq1{cx - sz * 0.35f, cy - sz * 0.15f, s2, s2}; SDL_RenderFillRect(r, &sq1);
    SDL_SetRenderDrawColor(r, 90, 220, 120, 255);
    SDL_FRect sq2{cx - s2 * 0.5f, cy + sz * 0.05f, s2, s2}; SDL_RenderFillRect(r, &sq2);
    SDL_SetRenderDrawColor(r, 90, 120, 220, 255);
    SDL_FRect sq3{cx + sz * 0.10f, cy + sz * 0.05f, s2, s2}; SDL_RenderFillRect(r, &sq3);
}

void ColorsSection::applyEdit() {
    if (selectedColorIdx_ < 0 || selectedColorIdx_ >= kNumColorKeys) return;
    uint8_t out[4]; hsvToRgb(hue_, sat_, val_, out);
    auto& s = Settings::instance();
    std::string presetName = s.currentColorPreset();
    auto presets = s.getColorPresets();
    nlohmann::json colorsJson;
    if (presets.contains(presetName) && presets[presetName].contains("colors"))
        colorsJson = presets[presetName]["colors"];
    colorsJson[kColorKeys[selectedColorIdx_]] = {out[0], out[1], out[2], out[3]};
    s.setColorPreset(presetName, colorsJson);
    uint8_t* dst = colorKeyToField(selectedColorIdx_);
    if (dst) { dst[0] = out[0]; dst[1] = out[1]; dst[2] = out[2]; dst[3] = out[3]; }
}

// --- shared list helpers ---

struct ListItem {
    std::string label;
    uint8_t color[4]{128,128,128,255};
    bool locked = false;
};

static void renderList(SDL_Renderer* r, const std::vector<ListItem>& items,
                       const SDL_FRect& b, float scale, int selected,
                       const std::string& subtitle, float listStartY, float maxY, float scrollY) {
    const float rowH = 15.f * scale, gap = 3.f * scale, swatchSz = rowH;
    const float totalH = static_cast<float>(items.size()) * (rowH + gap);
    const float clampedScroll = std::max(0.f, std::min(scrollY, totalH - (maxY - listStartY)));
    for (size_t i = 0; i < items.size(); ++i) {
        float iy = listStartY + static_cast<float>(i) * (rowH + gap) - clampedScroll;
        if (iy + rowH < listStartY || iy > maxY) continue;
        bool hover = false;
        { float mx, my; SDL_GetMouseState(&mx, &my);
          hover = (mx >= b.x && mx < b.x + b.w && my >= iy && my < iy + rowH); }
        if (hover) {
            SDL_SetRenderDrawColor(r, 60, 70, 90, 100);
            SDL_FRect bg{b.x, iy, b.w, rowH}; SDL_RenderFillRect(r, &bg);
        }
        SDL_FRect sw{b.x + 4.f * scale, iy, swatchSz, swatchSz};
        SDL_SetRenderDrawColor(r, items[i].color[0], items[i].color[1], items[i].color[2], items[i].color[3]);
        SDL_RenderFillRect(r, &sw);
        SDL_SetRenderDrawColor(r, selected == (int)i ? 200 : 80, selected == (int)i ? 200 : 80, selected == (int)i ? 200 : 80, 255);
        SDL_RenderRect(r, &sw);
        if (fonts.mainFont) {
            SDL_Color lc = items[i].locked ? SDL_Color{130,130,140,255} : SDL_Color{200,200,210,255};
            SDL_Surface* sf = TTF_RenderText_Blended(fonts.mainFont, items[i].label.c_str(), (int)items[i].label.size(), lc);
            if (sf) {
                SDL_Texture* tx = SDL_CreateTextureFromSurface(r, sf);
                if (tx) {
                    SDL_FRect tr{b.x + 4.f * scale + swatchSz + 4.f * scale, iy, static_cast<float>(sf->w) * scale, static_cast<float>(sf->h) * scale};
                    SDL_RenderTexture(r, tx, nullptr, &tr);
                    SDL_DestroyTexture(tx);
                }
                SDL_DestroySurface(sf);
            }
            if (items[i].locked) {
                SDL_Surface* s2 = TTF_RenderText_Blended(fonts.mainFont, " (locked)", 9, SDL_Color{130,130,140,255});
                if (s2) {
                    SDL_Texture* t2 = SDL_CreateTextureFromSurface(r, s2);
                    if (t2) {
                        SDL_FRect tr2{b.x + 4.f * scale + swatchSz + 4.f * scale + 100.f * scale, iy,
                                      static_cast<float>(s2->w) * scale, static_cast<float>(s2->h) * scale};
                        SDL_RenderTexture(r, t2, nullptr, &tr2);
                        SDL_DestroyTexture(t2);
                    }
                    SDL_DestroySurface(s2);
                }
            }
        }
    }
}

static int hitList(const std::vector<ListItem>& items, float mx, float my,
                   const SDL_FRect& b, float scale, float listStartY, float scrollY, float maxY) {
    if (mx < b.x || mx > b.x + b.w) return -1;
    const float rowH = 15.f * scale, gap = 3.f * scale;
    const float totalH = static_cast<float>(items.size()) * (rowH + gap);
    const float clampedScroll = std::max(0.f, std::min(scrollY, totalH - (maxY - listStartY)));
    for (size_t i = 0; i < items.size(); ++i) {
        float iy = listStartY + static_cast<float>(i) * (rowH + gap) - clampedScroll;
        if (iy + rowH < listStartY || iy > maxY) continue;
        if (my >= iy && my < iy + rowH) return static_cast<int>(i);
    }
    return -1;
}

// --- ColorsSection render / input ---

void ColorsSection::renderContent(SDL_Renderer* renderer, const SDL_FRect& b_, float scale) {
    // b_ is the circle bounding box — inset to stay inside the circle
    const float inset = 0.15f;
    SDL_FRect b{b_.x + b_.w * inset, b_.y + b_.h * inset, b_.w * (1.f - 2.f * inset), b_.h * (1.f - 2.f * inset)};

    auto& s = Settings::instance();
    std::string curPreset = s.currentColorPreset();
    auto presets = s.getColorPresets();
    const bool isDefault = (curPreset == "Default");

    float afterTitle = renderSectionTitle(renderer, "Colors", b, scale);
    const float barY = afterTitle + 4.f * scale;
    const float btnH = 16.f * scale;
    const float barW = b.w - 8.f * scale;
    const float listY = barY + btnH + 4.f * scale;

    // Back button — in the header area, top-left corner
    if (viewLevel_ > 0) {
        SDL_FRect backBtn{b.x + 4.f * scale, b.y + 2.f * scale, 28.f * scale, btnH};
        SDL_SetRenderDrawColor(renderer, 60, 60, 70, 255); SDL_RenderFillRect(renderer, &backBtn);
        SDL_SetRenderDrawColor(renderer, 120, 120, 140, 255); SDL_RenderRect(renderer, &backBtn);
        if (fonts.mainFont) {
            SDL_Surface* sf = TTF_RenderText_Blended(fonts.mainFont, "<", 1, SDL_Color{200,200,210,255});
            if (sf) {
                SDL_Texture* tx = SDL_CreateTextureFromSurface(renderer, sf);
                if (tx) {
                    SDL_FRect tr{backBtn.x + 8.f * scale, backBtn.y, static_cast<float>(sf->w) * scale, static_cast<float>(sf->h) * scale};
                    SDL_RenderTexture(renderer, tx, nullptr, &tr);
                    SDL_DestroyTexture(tx);
                }
                SDL_DestroySurface(sf);
            }
        }
    }

    if (viewLevel_ == 0) {
        // Preset list
        SDL_FRect newBtn{b.x + barW - 44.f * scale, barY, 42.f * scale, btnH};
        SDL_SetRenderDrawColor(renderer, 50, 140, 60, 255); SDL_RenderFillRect(renderer, &newBtn);
        SDL_SetRenderDrawColor(renderer, 100, 200, 120, 255); SDL_RenderRect(renderer, &newBtn);
        if (fonts.mainFont) {
            SDL_Surface* sf = TTF_RenderText_Blended(fonts.mainFont, "New", 3, SDL_Color{230,255,230,255});
            if (sf) { SDL_Texture* tx = SDL_CreateTextureFromSurface(renderer, sf);
                if (tx) { SDL_FRect tr{newBtn.x + 4.f, newBtn.y + 1.f, static_cast<float>(sf->w) * scale, static_cast<float>(sf->h) * scale};
                    SDL_RenderTexture(renderer, tx, nullptr, &tr); SDL_DestroyTexture(tx); } SDL_DestroySurface(sf); }
        }
        if (!isDefault) {
            SDL_FRect editBtn{b.x + barW - 44.f * scale - 46.f * scale, barY, 42.f * scale, btnH};
            SDL_SetRenderDrawColor(renderer, 50, 100, 160, 255); SDL_RenderFillRect(renderer, &editBtn);
            SDL_SetRenderDrawColor(renderer, 100, 160, 220, 255); SDL_RenderRect(renderer, &editBtn);
            if (fonts.mainFont) {
                SDL_Surface* sf = TTF_RenderText_Blended(fonts.mainFont, "Edit", 4, SDL_Color{220,230,255,255});
                if (sf) { SDL_Texture* tx = SDL_CreateTextureFromSurface(renderer, sf);
                    if (tx) { SDL_FRect tr{editBtn.x + 4.f, editBtn.y + 1.f, static_cast<float>(sf->w) * scale, static_cast<float>(sf->h) * scale};
                        SDL_RenderTexture(renderer, tx, nullptr, &tr); SDL_DestroyTexture(tx); } SDL_DestroySurface(sf); }
            }
            SDL_FRect delBtn{b.x + barW - 44.f * scale - 46.f * scale - 48.f * scale, barY, 42.f * scale, btnH};
            SDL_SetRenderDrawColor(renderer, 140, 50, 50, 255); SDL_RenderFillRect(renderer, &delBtn);
            SDL_SetRenderDrawColor(renderer, 200, 100, 100, 255); SDL_RenderRect(renderer, &delBtn);
            if (fonts.mainFont) {
                SDL_Surface* sf = TTF_RenderText_Blended(fonts.mainFont, "Del", 3, SDL_Color{255,230,230,255});
                if (sf) { SDL_Texture* tx = SDL_CreateTextureFromSurface(renderer, sf);
                    if (tx) { SDL_FRect tr{delBtn.x + 4.f, delBtn.y + 1.f, static_cast<float>(sf->w) * scale, static_cast<float>(sf->h) * scale};
                        SDL_RenderTexture(renderer, tx, nullptr, &tr); SDL_DestroyTexture(tx); } SDL_DestroySurface(sf); }
            }
        }

        std::vector<ListItem> items;
        std::vector<std::string> names;
        for (auto& [k, v] : presets.items()) {
            names.push_back(k);
            ListItem it; it.label = k; it.locked = v.value("immutable", false);
            if (v.contains("colors") && v["colors"].contains("background") && v["colors"]["background"].is_array() && v["colors"]["background"].size() >= 3) {
                for (int c = 0; c < 3; ++c) it.color[c] = v["colors"]["background"][c].get<uint8_t>();
                it.color[3] = v["colors"]["background"].size() >= 4 ? v["colors"]["background"][3].get<uint8_t>() : 255;
            }
            items.push_back(it);
        }
        renderList(renderer, items, b, scale, selectedPresetIdx_, "Presets", listY, b.y + b.h, scrollOffset_);
    } else if (viewLevel_ == 1) {
        // Color list
        nlohmann::json curColors;
        if (presets.contains(curPreset) && presets[curPreset].contains("colors"))
            curColors = presets[curPreset]["colors"];
        std::vector<ListItem> items;
        for (int i = 0; i < kNumColorKeys; ++i) {
            ListItem it; it.label = kColorKeys[i];
            if (curColors.contains(kColorKeys[i]) && curColors[kColorKeys[i]].is_array() && curColors[kColorKeys[i]].size() >= 3) {
                for (int c = 0; c < 3; ++c) it.color[c] = curColors[kColorKeys[i]][c].get<uint8_t>();
                it.color[3] = curColors[kColorKeys[i]].size() >= 4 ? curColors[kColorKeys[i]][3].get<uint8_t>() : 255;
            }
            items.push_back(it);
        }
        renderList(renderer, items, b, scale, selectedColorIdx_, curPreset, listY, b.y + b.h, scrollOffset_);
    } else if (viewLevel_ == 2) {
        // HSV picker
        const float px = b.x + 4.f * scale, py = barY + 4.f * scale;
        const float pw = b.w - 8.f * scale, ph = b.y + b.h - py - 4.f * scale;
        const float hueW = 12.f * scale, svW = pw - hueW - 4.f * scale, svH = ph - 22.f * scale;
        int iw = static_cast<int>(svW), ih = static_cast<int>(svH);
        for (int y2 = 0; y2 < ih; ++y2) {
            float vv = 1.f - static_cast<float>(y2) / svH;
            for (int x2 = 0; x2 < iw; ++x2) {
                float ss = static_cast<float>(x2) / svW; uint8_t c[4]; hsvToRgb(hue_, ss, vv, c);
                SDL_SetRenderDrawColor(renderer, c[0], c[1], c[2], 255);
                SDL_RenderPoint(renderer, px + static_cast<float>(x2), py + static_cast<float>(y2));
            }
        }
        float curX = px + sat_ * svW, curY = py + (1.f - val_) * svH;
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_FRect cs{curX - 3.f, curY - 3.f, 6.f, 6.f}; SDL_RenderRect(renderer, &cs);
        float hx = px + svW + 4.f * scale;
        for (int y2 = 0; y2 < ih; ++y2) {
            float h = static_cast<float>(y2) / svH; uint8_t c[4]; hsvToRgb(h, 1.f, 1.f, c);
            SDL_SetRenderDrawColor(renderer, c[0], c[1], c[2], 255);
            SDL_RenderLine(renderer, hx, py + static_cast<float>(y2), hx + hueW, py + static_cast<float>(y2));
        }
        float hy = py + hue_ * svH;
        SDL_FRect hc{hx - 1.f, hy - 2.f, hueW + 2.f, 4.f}; SDL_RenderRect(renderer, &hc);
        uint8_t prev[4]; hsvToRgb(hue_, sat_, val_, prev);
        SDL_FRect pr{px, py + svH + 4.f * scale, pw, 14.f * scale};
        SDL_SetRenderDrawColor(renderer, prev[0], prev[1], prev[2], 255); SDL_RenderFillRect(renderer, &pr);
        SDL_SetRenderDrawColor(renderer, 120, 120, 130, 255); SDL_RenderRect(renderer, &pr);
        if (fonts.mainFont && selectedColorIdx_ >= 0 && selectedColorIdx_ < kNumColorKeys) {
            SDL_Surface* sf = TTF_RenderText_Blended(fonts.mainFont, kColorKeys[selectedColorIdx_], strlen(kColorKeys[selectedColorIdx_]), SDL_Color{200,200,210,255});
            if (sf) { SDL_Texture* tx = SDL_CreateTextureFromSurface(renderer, sf);
                if (tx) { SDL_FRect tr{px, pr.y + pr.h + 2.f, static_cast<float>(sf->w) * scale, static_cast<float>(sf->h) * scale};
                    SDL_RenderTexture(renderer, tx, nullptr, &tr); SDL_DestroyTexture(tx); } SDL_DestroySurface(sf); }
        }
    }
}

bool ColorsSection::handleContentInput(SDL_Event& e, float mx, float my,
                                        const SDL_FRect& b_) {
    const float inset = 0.15f;
    SDL_FRect b{b_.x + b_.w * inset, b_.y + b_.h * inset, b_.w * (1.f - 2.f * inset), b_.h * (1.f - 2.f * inset)};

    auto& s = Settings::instance();
    std::string curPreset = s.currentColorPreset();
    const bool isDefault = (curPreset == "Default");
    float scale = contentScale_;

    float afterTitle = 0.f; { int th=0; TTF_GetStringSize(fonts.mainFont, "Colors", 0, nullptr, &th);
        afterTitle = b.y + 16.f * scale + static_cast<float>(th) * scale + 8.f * scale; }
    const float barY = afterTitle + 4.f * scale, btnH = 16.f * scale, barW = b.w - 8.f * scale;
    const float listY = barY + btnH + 4.f * scale;

    // Wheel scrolling for list views (levels 0, 1)
    if ((viewLevel_ == 0 || viewLevel_ == 1) && e.type == SDL_EVENT_MOUSE_WHEEL) {
        scrollOffset_ = std::max(0.f, scrollOffset_ - e.wheel.y * 30.f);
        return true;
    }

    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
        // Back
        if (viewLevel_ > 0) {
            SDL_FRect back{b.x + 4.f * scale, b.y + 2.f * scale, 28.f * scale, btnH};
            if (mx >= back.x && mx < back.x + back.w && my >= back.y && my < back.y + back.h) { viewLevel_--; scrollOffset_ = 0.f; return true; }
        }
        if (viewLevel_ == 0) {
            SDL_FRect nb{b.x + barW - 44.f * scale, barY, 42.f * scale, btnH};
            if (mx >= nb.x && mx < nb.x + nb.w && my >= nb.y && my < nb.y + nb.h) {
                auto presets = s.getColorPresets();
                std::string nn = "Preset " + std::to_string(presets.size());
                nlohmann::json cc; if (presets.contains(curPreset) && presets[curPreset].contains("colors")) cc = presets[curPreset]["colors"];
                s.setColorPreset(nn, cc); s.setCurrentColorPreset(nn); loadColorsFromSettings(); return true;
            }
            if (!isDefault) {
                SDL_FRect eb{b.x + barW - 44.f * scale - 46.f * scale, barY, 42.f * scale, btnH};
                if (mx >= eb.x && mx < eb.x + eb.w && my >= eb.y && my < eb.y + eb.h) {
                    viewLevel_ = 1; scrollOffset_ = 0.f; return true;
                }
                SDL_FRect db{b.x + barW - 44.f * scale - 46.f * scale - 48.f * scale, barY, 42.f * scale, btnH};
                if (mx >= db.x && mx < db.x + db.w && my >= db.y && my < db.y + db.h) {
                    s.deleteColorPreset(curPreset); selectedColorIdx_ = -1; selectedPresetIdx_ = -1; loadColorsFromSettings(); return true;
                }
            }
            auto presets = s.getColorPresets();
            std::vector<ListItem> items; std::vector<std::string> names;
            for (auto& [k, v] : presets.items()) { names.push_back(k); ListItem it; it.label = k; it.locked = v.value("immutable", false); items.push_back(it); }
            int hit = hitList(items, mx, my, b, scale, listY, scrollOffset_, b.y + b.h);
            if (hit >= 0 && hit < (int)names.size()) {
                s.setCurrentColorPreset(names[hit]); loadColorsFromSettings();
                selectedPresetIdx_ = hit; scrollOffset_ = 0.f; return true;
            }
        } else if (viewLevel_ == 1) {
            std::vector<ListItem> items;
            for (int i = 0; i < kNumColorKeys; ++i) { ListItem it; it.label = kColorKeys[i]; items.push_back(it); }
            int hit = hitList(items, mx, my, b, scale, listY, scrollOffset_, b.y + b.h);
            if (hit >= 0 && hit < kNumColorKeys) {
                selectedColorIdx_ = hit;
                nlohmann::json curCols;
                auto ps = s.getColorPresets();
                if (ps.contains(curPreset) && ps[curPreset].contains("colors"))
                    curCols = ps[curPreset]["colors"];
                if (curCols.contains(kColorKeys[hit]) && curCols[kColorKeys[hit]].is_array() && curCols[kColorKeys[hit]].size() >= 3) {
                    uint8_t cb[4];
                    for (int c = 0; c < 3; ++c) cb[c] = curCols[kColorKeys[hit]][c].get<int>();
                    cb[3] = curCols[kColorKeys[hit]].size() >= 4 ? curCols[kColorKeys[hit]][3].get<int>() : 255;
                    rgbToHsv(cb, hue_, sat_, val_);
                }
                if (!isDefault) viewLevel_ = 2;
                return true;
            }
        } else if (viewLevel_ == 2) {
            const float px = b.x + 4.f * scale, py = barY + 4.f * scale;
            const float pw = b.w - 8.f * scale, ph = b.y + b.h - py - 4.f * scale;
            const float hueW = 12.f * scale, svW = pw - hueW - 4.f * scale, svH = ph - 22.f * scale;
            float hx = px + svW + 4.f * scale;
            if (mx >= hx && mx <= hx + hueW && my >= py && my <= py + svH) {
                draggingHue_ = true; hue_ = std::clamp((my - py) / svH, 0.f, 1.f); applyEdit(); return true;
            }
            if (mx >= px && mx <= px + svW && my >= py && my <= py + svH) {
                draggingHSV_ = true; sat_ = std::clamp((mx - px) / svW, 0.f, 1.f); val_ = 1.f - std::clamp((my - py) / svH, 0.f, 1.f); applyEdit(); return true;
            }
        }
    }
    if ((draggingHSV_ || draggingHue_) && e.type == SDL_EVENT_MOUSE_BUTTON_UP) { draggingHSV_ = false; draggingHue_ = false; return true; }
    if (e.type == SDL_EVENT_MOUSE_MOTION && (draggingHSV_ || draggingHue_) && viewLevel_ == 2) {
        const float px = b.x + 4.f * scale, py = barY + 4.f * scale;
        const float pw = b.w - 8.f * scale, ph = b.y + b.h - py - 4.f * scale;
        const float hueW = 12.f * scale, svW = pw - hueW - 4.f * scale, svH = ph - 22.f * scale;
        if (draggingHSV_) { sat_ = std::clamp((mx - px) / svW, 0.f, 1.f); val_ = 1.f - std::clamp((my - py) / svH, 0.f, 1.f); }
        else { hue_ = std::clamp((my - py) / svH, 0.f, 1.f); }
        applyEdit(); return true;
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
