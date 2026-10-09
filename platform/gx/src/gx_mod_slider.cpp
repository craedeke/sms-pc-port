// Mod proof of concept: a "Mario size" slider in the top-right corner of the
// game window. Dragging it sets port_mario_scale (0.25 to 2.0), which
// decomp-patches/mod-01-mario-scale.patch applies to Mario's model every frame.
//
// Mouse: drag the slider; right-click it to reset to 1.0. With mouse look on,
// press F10 to free the cursor first. SMS_MARIO_SLIDER=0 hides it (scale 1).
#include "gx_internal.h"
#include "sms_gx/gx_pc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#ifdef SMS_GX_HAVE_SDL2
#include <SDL.h>
#endif

#include "third_party/stb_easy_font.h"

extern "C" float port_mario_scale;
float port_mario_scale = 1.0f;

namespace {

const float kMin = 0.25f, kMax = 2.0f;

// Panel layout in panel pixels (drawn upscaled by s_scale).
const int kPanelW = 128, kPanelH = 26;
const int kTrackX0 = 8, kTrackX1 = kPanelW - 8, kTrackY = 19;
const int kMargin = 8;

int s_enabled = -1;     // -1: not read from the environment yet
bool s_dragging = false;
// Where the panel was last drawn, in drawable pixels.
int s_px = 0, s_py = 0, s_scale = 2;

bool enabled() {
    if (s_enabled < 0) {
        const char* e = getenv("SMS_MARIO_SLIDER");
        s_enabled = !(e && strcmp(e, "0") == 0);
    }
    return s_enabled != 0;
}

void fill(std::vector<uint8_t>& px, int x0, int y0, int x1, int y1, const uint8_t c[4]) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > kPanelW) x1 = kPanelW;
    if (y1 > kPanelH) y1 = kPanelH;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) memcpy(&px[(size_t(y) * kPanelW + x) * 4], c, 4);
}

void text(std::vector<uint8_t>& px, int x, int y, const char* s, const uint8_t c[4]) {
    static char buf[64 * 1024];
    int quads = stb_easy_font_print(float(x), float(y), const_cast<char*>(s), nullptr, buf, sizeof buf);
    for (int q = 0; q < quads; q++) {
        const float* v = reinterpret_cast<const float*>(buf + q * 64);
        float minX = v[0], maxX = v[0], minY = v[1], maxY = v[1];
        for (int i = 1; i < 4; i++) {
            const float* p = reinterpret_cast<const float*>(buf + q * 64 + i * 16);
            if (p[0] < minX) minX = p[0];
            if (p[0] > maxX) maxX = p[0];
            if (p[1] < minY) minY = p[1];
            if (p[1] > maxY) maxY = p[1];
        }
        fill(px, int(minX + 0.5f), int(minY + 0.5f), int(maxX + 0.5f), int(maxY + 0.5f), c);
    }
}

float valueToT(float v) { return (v - kMin) / (kMax - kMin); }

// Set the value from a mouse x in drawable pixels.
void setFromX(int x) {
    float local = float(x - s_px) / float(s_scale);
    float t = (local - kTrackX0) / float(kTrackX1 - kTrackX0);
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    float v = kMin + t * (kMax - kMin);
    if (v > 0.96f && v < 1.04f) v = 1.0f;  // snap to normal size
    port_mario_scale = v;
}

bool inside(int x, int y) {
    return x >= s_px && x < s_px + kPanelW * s_scale && y >= s_py && y < s_py + kPanelH * s_scale;
}

}  // namespace

extern "C" {

void GXPC_ModSliderDraw(int winW, int winH) {
    if (!enabled()) {
        port_mario_scale = 1.0f;
        return;
    }
    s_scale = winH >= 1400 ? 3 : 2;
    s_px = winW - kPanelW * s_scale - kMargin;
    s_py = kMargin;

    std::vector<uint8_t> px(size_t(kPanelW) * kPanelH * 4);
    const uint8_t bg[4] = {16, 16, 24, 150};
    const uint8_t fg[4] = {255, 255, 255, 255};
    const uint8_t track[4] = {110, 110, 130, 255};
    const uint8_t fillc[4] = {230, 40, 40, 255};  // Mario red
    const uint8_t knob[4] = {255, 220, 64, 255};
    fill(px, 0, 0, kPanelW, kPanelH, bg);

    char label[32];
    snprintf(label, sizeof label, "MARIO SIZE  x%.2f", port_mario_scale);
    text(px, kTrackX0, 3, label, fg);

    const int tx = kTrackX0 + int(valueToT(port_mario_scale) * (kTrackX1 - kTrackX0) + 0.5f);
    fill(px, kTrackX0, kTrackY - 1, kTrackX1, kTrackY + 1, track);
    fill(px, kTrackX0, kTrackY - 1, tx, kTrackY + 1, fillc);
    // tick at 1.0
    const int one = kTrackX0 + int(valueToT(1.0f) * (kTrackX1 - kTrackX0) + 0.5f);
    fill(px, one, kTrackY - 3, one + 1, kTrackY + 3, track);
    fill(px, tx - 2, kTrackY - 4, tx + 2, kTrackY + 4, knob);

    GXPC_DrawOverlay(px.data(), kPanelW, kPanelH, s_px, s_py, s_scale, winW, winH);
}

#ifdef SMS_GX_HAVE_SDL2
// Returns 1 when the slider used the event (keep it from the game).
int GXPC_ModSliderEvent(const SDL_Event* ev, SDL_Window* window) {
    if (!enabled() || !window) return 0;
    if (ev->type != SDL_MOUSEBUTTONDOWN && ev->type != SDL_MOUSEBUTTONUP && ev->type != SDL_MOUSEMOTION) return 0;
    if (SDL_GetRelativeMouseMode()) return 0;  // mouse look owns the mouse

    // SDL mouse coordinates are in window units; the panel is in drawable pixels.
    int ww = 1, wh = 1, dw = 1, dh = 1;
    SDL_GetWindowSize(window, &ww, &wh);
    SDL_GL_GetDrawableSize(window, &dw, &dh);
    int mx, my;
    if (ev->type == SDL_MOUSEMOTION) {
        mx = ev->motion.x;
        my = ev->motion.y;
    } else {
        mx = ev->button.x;
        my = ev->button.y;
    }
    mx = mx * dw / (ww > 0 ? ww : 1);
    my = my * dh / (wh > 0 ? wh : 1);

    switch (ev->type) {
    case SDL_MOUSEBUTTONDOWN:
        if (!inside(mx, my)) return 0;
        if (ev->button.button == SDL_BUTTON_RIGHT) {
            port_mario_scale = 1.0f;
        } else if (ev->button.button == SDL_BUTTON_LEFT) {
            s_dragging = true;
            setFromX(mx);
        }
        return 1;
    case SDL_MOUSEMOTION:
        if (!s_dragging) return 0;
        setFromX(mx);
        return 1;
    case SDL_MOUSEBUTTONUP:
        if (!s_dragging || ev->button.button != SDL_BUTTON_LEFT) return 0;
        s_dragging = false;
        return 1;
    }
    return 0;
}
#endif

}  // extern "C"
