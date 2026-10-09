#include "platform/hires_pack.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <nlohmann/json.hpp>

#include "platform/imagefile.hpp"
#include "platform/paths.hpp"

namespace platform {

namespace {

using Json = nlohmann::json;

std::vector<uint8_t> DownsampleBox2x(int src_w, int src_h, const uint8_t *src) {
    int dst_w = std::max(1, src_w / 2);
    int dst_h = std::max(1, src_h / 2);
    std::vector<uint8_t> dst(static_cast<size_t>(dst_w) * dst_h * 4);

    for (int y = 0; y < dst_h; ++y) {
        int sy0 = y * 2;
        int sy1 = std::min(src_h - 1, sy0 + 1);
        for (int x = 0; x < dst_w; ++x) {
            int sx0 = x * 2;
            int sx1 = std::min(src_w - 1, sx0 + 1);

            const uint8_t *p00 = &src[(sy0 * src_w + sx0) * 4];
            const uint8_t *p10 = &src[(sy0 * src_w + sx1) * 4];
            const uint8_t *p01 = &src[(sy1 * src_w + sx0) * 4];
            const uint8_t *p11 = &src[(sy1 * src_w + sx1) * 4];

            uint8_t *out = &dst[(y * dst_w + x) * 4];
            for (int c = 0; c < 4; ++c) {
                uint32_t sum = static_cast<uint32_t>(p00[c]) + p10[c] + p01[c] + p11[c] + 2;
                out[c] = static_cast<uint8_t>(sum / 4);
            }
        }
    }
    return dst;
}

std::filesystem::path g_custom_pack_path;
std::unique_ptr<HiresPack> g_active_pack;

} // namespace

bool HiresPack::Load(const std::filesystem::path &pack_dir) {
    Unload();
    m_pack_dir = pack_dir;
    std::filesystem::path index_file = pack_dir / "index.json";
    std::ifstream stream(index_file, std::ios::binary);
    if (!stream) {
        return false;
    }

    try {
        Json root = Json::parse(stream, nullptr, true, true);
        if (!root.is_object()) {
            return false;
        }

        m_version = root.value("version", 1);
        // Packs may describe scale per entry. Their top-level scale is then a label
        // (for example "per-texture"), not a number.
        m_scale = root.contains("scale") && root["scale"].is_number_integer() ? root["scale"].get<int>() : 1;
        std::string scale_label = root.contains("scale") && root["scale"].is_string()
                                      ? root["scale"].get<std::string>()
                                      : std::to_string(m_scale) + "x";

        if (root.contains("textures") && root["textures"].is_object()) {
            for (const auto &[key, val] : root["textures"].items()) {
                if (!val.is_object()) {
                    continue;
                }
                HiresTextureEntry entry;
                std::string rel_file = val.value("file", "");
                entry.file_path = pack_dir / PathsFromUtf8(rel_file);
                entry.width = val.value("w", 0);
                entry.height = val.value("h", 0);
                entry.scale = val.value("scale", m_scale);
                m_textures[key] = std::move(entry);
            }
        }
        m_loaded = true;
        std::fprintf(stderr, "hires: loaded pack from %s (%zu textures, scale %s)\n",
                     PathsDisplay(pack_dir).c_str(), m_textures.size(), scale_label.c_str());
        return true;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "hires: failed to parse %s: %s\n", PathsDisplay(index_file).c_str(), e.what());
        return false;
    }
}

void HiresPack::Unload() {
    m_textures.clear();
    m_loaded = false;
    m_scale = 1;
    m_version = 1;
    m_pack_dir.clear();
}

const HiresTextureEntry *HiresPack::FindEntry(std::string_view key) const {
    if (!m_loaded) {
        return nullptr;
    }
    auto it = m_textures.find(std::string(key));
    if (it != m_textures.end()) {
        return &it->second;
    }
    return nullptr;
}

std::shared_ptr<DecodedHiresTexture> HiresPack::LoadTexture(std::string_view key, bool generate_mips, uint32_t max_mips) {
    const HiresTextureEntry *entry = FindEntry(key);
    if (!entry) {
        return nullptr;
    }

    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
    if (!imagefile::LoadPng(entry->file_path, width, height, rgba)) {
        std::fprintf(stderr, "hires: could not read png %s for key %.*s\n",
                     PathsDisplay(entry->file_path).c_str(), static_cast<int>(key.size()), key.data());
        return nullptr;
    }

    auto decoded = std::make_shared<DecodedHiresTexture>();
    decoded->width = width;
    decoded->height = height;
    decoded->levels.push_back(std::move(rgba));

    if (generate_mips) {
        int cur_w = width;
        int cur_h = height;
        while ((cur_w > 1 || cur_h > 1) && (max_mips == 0 || decoded->levels.size() < max_mips)) {
            decoded->levels.push_back(DownsampleBox2x(cur_w, cur_h, decoded->levels.back().data()));
            cur_w = std::max(1, cur_w / 2);
            cur_h = std::max(1, cur_h / 2);
        }
    }

    return decoded;
}

void HiresSetPackPath(const std::filesystem::path &path) {
    g_custom_pack_path = path;
    if (g_active_pack) {
        g_active_pack->Unload();
        g_active_pack.reset();
    }
}

HiresPack &HiresActivePack() {
    if (!g_active_pack) {
        g_active_pack = std::make_unique<HiresPack>();
        std::filesystem::path pack_path = g_custom_pack_path;
        if (pack_path.empty()) {
            pack_path = PathsDataRoot() / "hires";
        }
        std::error_code ec;
        if (std::filesystem::is_directory(pack_path, ec)) {
            g_active_pack->Load(pack_path);
        }
    }
    return *g_active_pack;
}

bool HiresHasPack() {
    return HiresActivePack().IsLoaded();
}

} // namespace platform
