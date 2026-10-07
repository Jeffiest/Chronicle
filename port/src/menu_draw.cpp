#include "menu_draw.hpp"

#include <cstdint>

#include "gameutil.hpp"
#include "menu_inventory.hpp"
#include "mglib.hpp"
#include "snd.hpp"
#include "name_mouse.hpp"
#include "rect.hpp"

// Retail rounds up to 64 bytes through int, which only holds an address below 2 GiB.
PC_OVERRIDE u_long128 *MenuCalcBufAlignment(u_long128 *buffer) {
    auto address = reinterpret_cast<std::uintptr_t>(buffer);
    return reinterpret_cast<u_long128 *>((address + 63) & ~std::uintptr_t{63});
}

// Retail draws the board frame's top strip 19 rows tall from 20 texels. The dropped texel row lies
// where the grid's first row covers it, so at the retail size the strip's opaque border ends
// where the grid begins. A window that scales the picture shows what the squeeze left between
// them: the border ends at y=128.6 and the grid, clipped to start at 129, leaves a sliver of the
// scene across the top of the grid. Drawing the strip a row to a texel ends the border on 129.
PC_OVERRIDE void PersonalBoardDrawWaku(int x, int y, CTexture *texture, int alpha) {
    DrawMenu2DSprite(texture, CRect_i_(x, y + 1, 0x14, 0xBF), CRect_i_(0, 0, 0x14, 0xC0), alpha);
    DrawMenu2DSprite(texture, CRect_i_(x + 0x14, y + 1, 0xC8, 0x14), CRect_i_(0x14, 0, 0xC8, 0x14), alpha);
    DrawMenu2DSprite(texture, CRect_i_(x + 0xDC, y + 1, 0x24, 0xBF), CRect_i_(0xDC, 0, 0x24, 0xC0), alpha);
    DrawMenu2DSprite(texture, CRect_i_(x + 0x14, y + 0xA1, 0xC8, 0x1F), CRect_i_(0x14, 0xA0, 0xC8, 0x20), alpha);
}

// The Register Name screen's hand follows the mouse while the pointer is in use.
PC_OVERRIDE void DrawMenu2DSprite(CTexture *texture, CRect_i_ screen, CRect_i_ texel, unsigned char r, unsigned char g,
                                  unsigned char b, int alpha) {
    NameMouseHand(texture, screen, texel, r == 0 && g == 0 && b == 0);
    set2DSprite(GetVif1Packet(), texture, screen, texel, r, g, b, alpha);
}
