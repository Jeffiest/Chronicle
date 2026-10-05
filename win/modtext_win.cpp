#include "modtext.hpp"

#include <algorithm>
#include <vector>

#include "overlay.hpp"

namespace {

std::vector<ModTextLine> g_lines;
std::vector<ModRect>     g_rects;
unsigned                 g_version = 1;

void DrawLine(const ModTextLine &line) {
    if (line.text.empty() || line.scale <= 0.0f) {
        return;
    }
    const float    s = line.scale;
    const uint8_t  r = static_cast<uint8_t>(line.rgba >> 24);
    const uint8_t  g = static_cast<uint8_t>(line.rgba >> 16);
    const uint8_t  b = static_cast<uint8_t>(line.rgba >> 8);
    const uint8_t  a = static_cast<uint8_t>((line.rgba & 0xFF) >> 1); // the GS's 0x80 is opaque
    auto vertex = [](float x, float y, uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca) {
        return gfx::Vertex2D{x, y, 0.0f, 0.0f, 0.0f, {cr, cg, cb, ca}};
    };
    auto quad = [&](std::vector<gfx::Vertex2D> &out, float x0, float y0, float x1, float y1, uint8_t cr, uint8_t cg,
                    uint8_t cb, uint8_t ca) {
        out.push_back(vertex(x0, y0, cr, cg, cb, ca));
        out.push_back(vertex(x1, y0, cr, cg, cb, ca));
        out.push_back(vertex(x1, y1, cr, cg, cb, ca));
        out.push_back(vertex(x0, y1, cr, cg, cb, ca));
    };

    const float width = static_cast<float>(line.text.size()) * kOverlayAdvance * s;
    std::vector<gfx::Vertex2D> backdrop;
    if (line.backdrop) {
        quad(backdrop, line.x - s, line.y - s, line.x + width + s, line.y + (kOverlayGlyphHeight + 1) * s, 0, 0, 0, 0x40);
    }

    std::vector<gfx::Vertex2D> glyphs;
    float pen = line.x;
    for (char c : line.text) {
        if (const std::uint8_t *rows = OverlayGlyph(c)) {
            for (int row = 0; row < kOverlayGlyphHeight; row++) {
                for (int column = 0; column < kOverlayGlyphWidth;) {
                    if ((rows[row] & (0x10 >> column)) == 0) {
                        column++;
                        continue;
                    }
                    int end = column;
                    while (end < kOverlayGlyphWidth && (rows[row] & (0x10 >> end)) != 0) {
                        end++;
                    }
                    float y0 = line.y + static_cast<float>(row) * s;
                    quad(glyphs, pen + static_cast<float>(column) * s, y0, pen + static_cast<float>(end) * s, y0 + s, r, g, b, a);
                    column = end;
                }
            }
        }
        pen += kOverlayAdvance * s;
    }

    gfx::DrawState blended;
    blended.blend = true;
    if (!backdrop.empty()) {
        gfx::Draw2D(gfx::Primitive::Quads, backdrop, {}, blended);
    }
    if (!glyphs.empty()) {
        gfx::Draw2D(gfx::Primitive::Quads, glyphs, {}, blended);
    }
}

} // namespace

void ModTextSet(std::vector<ModTextLine> lines, std::vector<ModRect> rects) {
    if (lines == g_lines && rects == g_rects) {
        return;
    }
    g_lines = std::move(lines);
    g_rects = std::move(rects);
    g_version++;
}

unsigned ModTextVersion() { return g_version; }

bool ModTextAny() { return !g_lines.empty() || !g_rects.empty(); }

gfx::DisplayListRef ModsOverlayRecord(std::string_view fps_text) {
    gfx::BeginRecording();
    if (!gfx::Recording()) {
        return nullptr;
    }
    if (!fps_text.empty()) {
        int pixel = OverlayPixelSize(gfx::GetLogicalMapping(gfx::kMainTarget));
        OverlayDrawText(fps_text, gfx::GetUiMapping(gfx::kMainTarget), pixel, pixel * kOverlayPadding, pixel * kOverlayPadding);
    }
    for (const ModRect &r : g_rects) {
        if (r.w <= 0.0f || r.h <= 0.0f) {
            continue;
        }
        std::vector<gfx::Vertex2D> quad = {
            gfx::Vertex2D{r.x, r.y, 0.0f, 0.0f, 0.0f, {static_cast<uint8_t>(r.rgba >> 24), static_cast<uint8_t>(r.rgba >> 16), static_cast<uint8_t>(r.rgba >> 8), static_cast<uint8_t>((r.rgba & 0xFF) >> 1)}},
            gfx::Vertex2D{r.x + r.w, r.y, 0.0f, 0.0f, 0.0f, {static_cast<uint8_t>(r.rgba >> 24), static_cast<uint8_t>(r.rgba >> 16), static_cast<uint8_t>(r.rgba >> 8), static_cast<uint8_t>((r.rgba & 0xFF) >> 1)}},
            gfx::Vertex2D{r.x + r.w, r.y + r.h, 0.0f, 0.0f, 0.0f, {static_cast<uint8_t>(r.rgba >> 24), static_cast<uint8_t>(r.rgba >> 16), static_cast<uint8_t>(r.rgba >> 8), static_cast<uint8_t>((r.rgba & 0xFF) >> 1)}},
            gfx::Vertex2D{r.x, r.y + r.h, 0.0f, 0.0f, 0.0f, {static_cast<uint8_t>(r.rgba >> 24), static_cast<uint8_t>(r.rgba >> 16), static_cast<uint8_t>(r.rgba >> 8), static_cast<uint8_t>((r.rgba & 0xFF) >> 1)}},
        };
        gfx::DrawState blended;
        blended.blend = true;
        gfx::Draw2D(gfx::Primitive::Quads, quad, {}, blended);
    }
    for (const ModTextLine &line : g_lines) {
        DrawLine(line);
    }
    return gfx::EndRecording();
}
