#include "item_icon3d.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <memory>
#include <vector>

#include <algorithm>
#include <cmath>
#include <filesystem>

#include "btitem.hpp"
#include "collision.hpp"
#include "btmisc.hpp"
#include "dataread.hpp"
#include "dun/gameloop.hpp"
#include "frame.hpp"
#include "gfx/gfx.hpp"
#include "itemdata.hpp"
#include "memcard.hpp"
#include "mds.hpp"
#include "menu_draw.hpp"
#include "menu_save.hpp"
#include "menuetc.hpp"
#include "mglib.hpp"
#include "rect.hpp"
#include "renderinfo.hpp"
#include "texture.hpp"
#include "texture_port.hpp"

namespace {

constexpr int         kIconSize = 32;
constexpr int         kCrop = 128;  // scratch pixels turned into one icon (4x4 each)
constexpr float       kFill = 92.0f; // pixels across the model inside the crop
constexpr int         kBlock = 0x47; // a texture block nothing else loads into
constexpr std::size_t kModelBytes = 8u << 20;
constexpr std::size_t kImageBytes = 4u << 20;
constexpr std::size_t kArenaBytes = 8u << 20;
constexpr const char *kScratch = "item icon 3d scratch";

bool EnvOn(const char *name) {
    const char *value = std::getenv(name);
    return value != nullptr && value[0] != 0 && value[0] != '0';
}

bool Debug() {
    static const bool on = EnvOn("DC_ITEM_ICONS_3D_DEBUG");
    return on;
}

struct Icon {
    std::unique_ptr<CTexture> texture;
    bool                      failed = false;
};

std::map<int, Icon> g_icons;

u_long Tex0(unsigned tbp, int tbw, int psm, int tw, int th) {
    return (u_long) tbp | ((u_long) tbw << 14) | ((u_long) psm << 20) | ((u_long) tw << 26) | ((u_long) th << 30) |
           ((u_long) 1 << 34);
}

// Buffers the files are read into and the model is built in; kept for every icon, since only one is drawn at a time.
struct Work {
    std::vector<uint8_t> storage;
    uint8_t             *model = nullptr;
    uint8_t             *image = nullptr;
    uint8_t             *arena_base = nullptr;

    Work() {
        storage.resize(kModelBytes + kImageBytes + kArenaBytes + 192);
        auto align = [](uint8_t *p) { return reinterpret_cast<uint8_t *>((reinterpret_cast<uintptr_t>(p) + 63) & ~uintptr_t{63}); };
        model = align(storage.data());
        image = align(model + kModelBytes);
        arena_base = align(image + kImageBytes);
    }
};

Work &GetWork() {
    static Work work;
    return work;
}

CFrame *g_frame = nullptr;
std::string g_last_name;

// Drawn by MenuPolygonDraw, which has set the menu camera's position and the lights.
void DrawModel() {
    MenuWorldTrans(&MenuCamera);
    MGDraw(g_frame);
}

// The model's file names, or false where the item has none of its own (an empty slot of the table falls back to the
// water bottle in retail, and that is not this item's picture).
bool ModelPaths(int item_no, char *model_path, char *texture_path) {
    if (item_no < ITEM_ATTACH_START) {
        item_no = TransWepNo(item_no);
    }
    if (item_no < ITEM_ATTACH_START) {
        return false;
    }
    if (item_no < ITEM_WEAPON_START) {
        const char *name = ITEM_NAME_TBL_NEW[item_no - ITEM_ATTACH_START];
        if (name == nullptr || name[0] == 0) {
            return false;
        }
    }
    BtGetItemNamePath(model_path, texture_path, item_no);
    return true;
}

bool Render(int item_no, CTexture &out) {
    char model_path[64];
    char texture_path[76];
    if (!ModelPaths(item_no, model_path, texture_path)) {
        if (Debug()) {
            std::fprintf(stderr, "item icon 3d: item %d has no model\n", item_no);
        }
        return false;
    }
    Work &work = GetWork();
    int   size = 0;
    if (Debug()) {
        std::fprintf(stderr, "item icon 3d: item %d reads %s and %s\n", item_no, model_path, texture_path);
    }
    if (!LoadFile2(model_path, work.model, &size, 0) || size <= 0 || static_cast<std::size_t>(size) + 2048 > kModelBytes) {
        return false;
    }
    if (!LoadFile2(texture_path, work.image, &size, 0) || size <= 0 || static_cast<std::size_t>(size) + 2048 > kImageBytes) {
        return false;
    }

    char name[48];
    std::snprintf(name, sizeof name, "item icon 3d %d", item_no);
    unsigned key = PortCreatePlaceholder(name, kIconSize, kIconSize, 4, PortTextureOwner::Other, nullptr);
    gfx::TextureHandle target = gfx::FindNamedRenderTarget(name);
    gfx::TextureHandle scratch = gfx::NamedRenderTarget(kScratch, 640, 480, true);
    if (key == 0 || target == gfx::kNullTexture || scratch == gfx::kNullTexture) {
        if (Debug()) {
            std::fprintf(stderr, "item icon 3d: item %d: no target (key %u)\n", item_no, key);
        }
        return false;
    }

    SetTempTexture(kBlock, reinterpret_cast<char *>(work.image));
    CDataAlloc2<1> arena(-1);
    arena.base = work.arena_base;
    arena.limit = static_cast<int>(kArenaBytes >> 4);
    arena.Reset();
    g_frame = reinterpret_cast<CFrame *>(LoadMDSFile(reinterpret_cast<u_int *>(work.model), &arena, 0, nullptr, nullptr));
    if (g_frame == nullptr) {
        TexManager.DeleteTextureBlock(kBlock);
        return false;
    }
    // Frame the model: centre its bound on the origin, turn it so its long side runs corner to corner, and scale it
    // so the bound's sphere fills kFill pixels of the scratch target.
    CBoxVu0 box;
    g_frame->GetBoundBox(&box, 1);
    float ext[3], mid[4];
    for (int i = 0; i < 3; i++) {
        ext[i] = box.max[i] - box.min[i];
        mid[i] = (box.max[i] + box.min[i]) * 0.5f;
    }
    mid[3] = 1.0f;
    float radius = 0.5f * std::sqrt(ext[0] * ext[0] + ext[1] * ext[1] + ext[2] * ext[2]);
    if (!(radius > 0.0001f)) {
        radius = 1.0f;
    }
    // Long axis: x or y long becomes a diagonal in the picture, z long (pointing at the camera) is laid down first.
    float rx = 0.35f, ry = 0.0f, rz = 0.0f;
    if (ext[2] > ext[0] && ext[2] > ext[1]) {
        rx = 1.2f;
        rz = 0.785f;
    } else {
        rz = 0.785f;
    }
    g_frame->SetRotation(rx, ry, rz);
    g_frame->SetScale(1.0f, 1.0f, 1.0f);
    float origin[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    g_frame->SetPosition(origin);
    float rotated[4];
    g_frame->GetWorldPosition(rotated, mid);

    RenderInfo saved = mgRenderInfo;
    const gfx::TextureHandle previous = gfx::CurrentRenderTarget();
    gfx::SetRenderTarget(scratch);
    const uint8_t none[4] = {};

    // Pixels per world unit at the origin, from one trial draw, which also puts the camera in place for the projection.
    static float pixels_per_unit = 0.0f;
    if (pixels_per_unit <= 0.0f) {
        gfx::Clear(true, none, true, 1.0f);
        MenuPolygonDraw(0x80, DrawModel);
        float a[4] = {0, 0, 0, 1}, c[4] = {10.0f, 0, 0, 1};
        int   pa[4], pc[4];
        MGRotTransPers2D(pa, a, 0);
        MGRotTransPers2D(pc, c, 0);
        pixels_per_unit = std::fabs(static_cast<float>(pc[0] - pa[0])) / 10.0f;
        if (pixels_per_unit <= 0.0f) {
            pixels_per_unit = 3.0f;
        }
    }
    const float scale = (kFill * 0.5f) / (pixels_per_unit * radius);
    g_frame->SetScale(scale, scale, scale);
    float position[4] = {-rotated[0] * scale, -rotated[1] * scale, -rotated[2] * scale, 1.0f};
    g_frame->SetPosition(position);
    const uint8_t red[4] = {0x80, 0, 0, 0x80};
    gfx::Clear(true, EnvOn("DC_ITEM_ICONS_3D_RED") ? red : none, true, 1.0f);
    MenuPolygonDraw(0x80, DrawModel);
    int centre[2] = {320, 240};
    Get3DPosTo2DPos(g_frame, centre);
    // Supersample down in exact halves: kCrop -> kCrop/2 -> the icon.
    gfx::TextureHandle half = gfx::NamedRenderTarget("item icon 3d half", kCrop / 2, kCrop / 2, true);
    const gfx::Rect from = {320 - kCrop / 2, 240 - kCrop / 2, kCrop, kCrop};
    if (half != gfx::kNullTexture) {
        gfx::SetRenderTarget(previous);
        gfx::BlitTexture(scratch, from, half, {0, 0, kCrop / 2, kCrop / 2}, gfx::Filter::Linear);
        gfx::BlitTexture(half, {0, 0, kCrop / 2, kCrop / 2}, target, {0, 0, kIconSize, kIconSize}, gfx::Filter::Linear);
    } else {
        gfx::SetRenderTarget(previous);
        gfx::BlitTexture(scratch, from, target, {0, 0, kIconSize, kIconSize}, gfx::Filter::Linear);
    }
    mgRenderInfo = saved;
    TexManager.DeleteTextureBlock(kBlock);
    g_frame = nullptr;
    g_last_name = name;
    if (Debug()) {
        std::fprintf(stderr, "item icon 3d: item %d %s radius %.2f scale %.3f ppu %.2f centre %d,%d\n", item_no, model_path,
                     radius, scale, pixels_per_unit, centre[0], centre[1]);
    }

    out.block = -1;
    out.width = kIconSize;
    out.height = kIconSize;
    out.bpp = 4;
    std::snprintf(out.name, sizeof out.name, "%s", name);
    out.tex0 = Tex0(key, kIconSize / 64 + 1, SCE_GS_PSMCT32, 5, 5);
    return true;
}

} // namespace

bool ItemIcons3DEnabled() {
    static const bool on = EnvOn("DC_ITEM_ICONS_3D");
    return on;
}

CTexture *ItemIcon3DTexture(int item_no) {
    if (!ItemIcons3DEnabled()) {
        return nullptr;
    }
    if (!gfx::InFrame()) {
        if (Debug()) {
            static int said = 0;
            if (said++ < 5) {
                std::fprintf(stderr, "item icon 3d: item %d asked outside a frame, sheet icon for now\n", item_no);
            }
        }
        return nullptr; // drawing is dropped outside a frame; ask again next time
    }
    Icon &icon = g_icons[item_no];
    if (icon.failed) {
        return nullptr;
    }
    if (!icon.texture) {
        auto texture = std::make_unique<CTexture>();
        if (!Render(item_no, *texture)) {
            icon.failed = true;
            return nullptr;
        }
        icon.texture = std::move(texture);
    }
    return icon.texture.get();
}

void ItemIcons3DDrawSheet() {
    const char *page = std::getenv("DC_ITEM_ICONS_3D_SHEET");
    if (page == nullptr || !ItemIcons3DEnabled()) {
        return;
    }
    const int first = ITEM_ATTACH_START + std::atoi(page) * 280;
    for (int i = 0; i < 280; i++) {
        CTexture *texture = ItemIcon3DTexture(first + i);
        if (texture == nullptr) {
            continue;
        }
        const int x = (i % 20) * kIconSize;
        const int y = (i / 20) * kIconSize + 16;
        DrawMenu2DSprite(texture, CRect_i_(x, y, kIconSize, kIconSize), CRect_i_(0, 0, kIconSize, kIconSize), 0x80);
    }
}

// Retail's lookup of the sheet cell an item's icon is in, with the model's own icon in front of it.
PORT_OVERRIDE CTexture *RetCTex(short item_no, int &u, int &v) {
    if (Debug()) {
        static bool said = false;
        if (!said) {
            said = true;
            std::fprintf(stderr, "item icon 3d: RetCTex first asked for item %d, enabled=%d\n", item_no, ItemIcons3DEnabled());
        }
    }
    COM_ITEM_INFO *info = GetCommonItemInfo(item_no);

    if (info == nullptr) {
        return nullptr;
    }

    int icon = info->icon_index;

    if (icon < 0) {
        return nullptr;
    }

    if (std::getenv("DC_ITEM_ICONS_3D_SHEET") != nullptr) {
        ItemIcons3DDrawSheet(); // every ask, so the last one is on top of the menu panels
    }

    if (CTexture *model = ItemIcon3DTexture(item_no)) {
        u = 0;
        v = 0;
        return model;
    }

    u = ((icon + 8) % 8) << 5;
    v = (icon >> 3) << 5;

    switch (info->kind) {
        case ITEMKIND_ITEM:
            return ItemIcon;
        case ITEMKIND_ATTACH:
        case ITEMKIND_WEAPON:
            return WepIcon;
    }

    return nullptr;
}

// Debug: with DC_ITEM_ICONS_3D_DUMP=<dir>, the scratch render and the newest icon are written there. Called outside a frame.
void ItemIcons3DDumpDebug() {
    const char *dir = std::getenv("DC_ITEM_ICONS_3D_DUMP");
    if (dir == nullptr || g_last_name.empty()) {
        return;
    }
    auto write = [&](const char *target_name, const char *file) {
        gfx::TextureHandle texture = gfx::FindNamedRenderTarget(target_name);
        std::vector<uint8_t> rgba;
        uint32_t width = 0, height = 0;
        if (texture != gfx::kNullTexture && gfx::ReadbackTexture(texture, rgba, width, height)) {
            gfx::WritePng(std::filesystem::path(dir) / file, rgba.data(), width, height);
        }
    };
    write(kScratch, "scratch.png");
    write("item icon 3d half", "half.png");
    write(g_last_name.c_str(), "icon.png");
}
