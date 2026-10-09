#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace platform {

struct HiresTextureEntry {
    std::filesystem::path file_path;
    int width = 0;
    int height = 0;
    int scale = 1;
};

struct DecodedHiresTexture {
    int width = 0;
    int height = 0;
    std::vector<std::vector<uint8_t>> levels; // Level 0 is base RGBA8, optional mipmaps
};

class HiresPack {
public:
    HiresPack() = default;

    bool Load(const std::filesystem::path &pack_dir);
    void Unload();

    bool IsLoaded() const { return m_loaded; }
    int Scale() const { return m_scale; }
    size_t TextureCount() const { return m_textures.size(); }

    const HiresTextureEntry *FindEntry(std::string_view key) const;

    // Loads and decodes the replacement texture PNG. If generate_mips is true, generates mip levels down to 1x1 or up to max_mips.
    std::shared_ptr<DecodedHiresTexture> LoadTexture(std::string_view key, bool generate_mips = false, uint32_t max_mips = 0);

private:
    std::filesystem::path m_pack_dir;
    int m_version = 1;
    int m_scale = 1;
    bool m_loaded = false;
    std::unordered_map<std::string, HiresTextureEntry> m_textures;
};

// Global active pack management
void HiresSetPackPath(const std::filesystem::path &path);
HiresPack &HiresActivePack();
bool HiresHasPack();

} // namespace platform
