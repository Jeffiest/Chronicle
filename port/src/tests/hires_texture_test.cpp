#include <array>
#include <cstdint>

#include <gtest/gtest.h>

#include "platform/md5.hpp"
#include "texture_port.hpp"

TEST(HiresTexture, KeyUsesDimensionsAndRawRgba) {
    const std::array<uint8_t, 16> rgba = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    EXPECT_EQ(platform::ComputeTextureKey(2, 2, rgba.data()), "2ebfc4eaf57110760e0626fa9cbd8c34");
}

TEST(HiresTexture, ResolverTracksRuntimeToggleAndModifiedPalette) {
    constexpr gfx::TextureHandle original = 17;
    constexpr gfx::TextureHandle hires = 29;
    EXPECT_EQ(PortResolveHiresTexture(original, hires, true, false), hires);
    EXPECT_EQ(PortResolveHiresTexture(original, hires, false, false), original);
    EXPECT_EQ(PortResolveHiresTexture(original, hires, true, true), original);
}
