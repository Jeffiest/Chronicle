#include "soundtrack.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <mutex>
#include <string>

#include "platform/config.hpp"
#include "platform/paths.hpp"
#include "trace.hpp"

namespace audio {

namespace {

struct Entry {
    std::filesystem::path           file;
    float                           gain = 1.0f;
    bool                            loop = true;
    double                          start = 0.0;
    bool                            has_loop_start = false;
    double                          loop_start = 0.0;
    double                          loop_end = 0.0;
    bool                            failed = false;
    std::shared_ptr<const PcmTrack> loaded;
};

std::mutex                   g_mutex;
bool                         g_read = false;
std::filesystem::path        g_folder;
std::map<std::string, Entry> g_entries;

void ReadMapping() {
    g_read = true;
    g_entries.clear();
    g_folder = PathsSaveRoot() / "soundtrack";
    std::ifstream file(g_folder / "soundtrack.json");
    audio::Trace("soundtrack: reading %s (%s)", (g_folder / "soundtrack.json").string().c_str(), file ? "found" : "missing");
    if (!file) {
        return;
    }
    nlohmann::json root = nlohmann::json::parse(file, nullptr, false, true);
    if (root.is_discarded() || !root.is_object() || !root.contains("tracks") || !root["tracks"].is_object()) {
        std::fprintf(stderr, "soundtrack: %s is not a soundtrack.json\n", (g_folder / "soundtrack.json").string().c_str());
        return;
    }
    for (const auto &[name, value] : root["tracks"].items()) {
        if (!value.is_object() || !value.contains("file") || !value["file"].is_string()) {
            std::fprintf(stderr, "soundtrack: %s needs a \"file\"\n", name.c_str());
            continue;
        }
        Entry entry;
        entry.file = g_folder / std::filesystem::u8path(value["file"].get<std::string>());
        entry.gain = value.value("gain", 1.0f);
        entry.loop = value.value("loop", true);
        entry.start = value.value("start", 0.0);
        entry.has_loop_start = value.contains("loop_start");
        entry.loop_start = value.value("loop_start", 0.0);
        entry.loop_end = value.value("loop_end", 0.0);
        g_entries[name] = std::move(entry);
    }
}

} // namespace

std::shared_ptr<const PcmTrack> SoundtrackFor(std::string_view sequence) {
    if (!ConfigGet().soundtrack) {
        return nullptr;
    }
    std::lock_guard lock(g_mutex);
    if (!g_read) {
        ReadMapping();
    }
    auto found = g_entries.find(std::string(sequence));
    if (found == g_entries.end()) {
        return nullptr;
    }
    Entry &entry = found->second;
    if (entry.loaded == nullptr && !entry.failed) {
        std::string error;
        auto        track = LoadPcmTrack(entry.file, error);
        if (track == nullptr) {
            std::fprintf(stderr, "soundtrack: %s: %s\n", entry.file.string().c_str(), error.c_str());
            entry.failed = true;
            return nullptr;
        }
        track->gain = entry.gain;
        track->loop = entry.loop;
        track->start = std::min<std::int64_t>(static_cast<std::int64_t>(entry.start * track->rate), track->Frames() - 1);
        // Without a loop_start of its own the song repeats from where it started.
        track->loop_start = entry.has_loop_start ? static_cast<std::int64_t>(entry.loop_start * track->rate) : track->start;
        track->loop_end = static_cast<std::int64_t>(entry.loop_end * track->rate);
        entry.loaded = std::move(track);
    }
    return entry.loaded;
}

void SoundtrackReload() {
    std::lock_guard lock(g_mutex);
    g_read = false;
}

} // namespace audio
