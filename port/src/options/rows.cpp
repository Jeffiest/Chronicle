#include "options/rows.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "menu_option.hpp"
#include "platform/window.hpp"

namespace options {

namespace {

constexpr int kMapOff = 3;

int Two(const Config &) {
    return 2;
}

// A game option whose first choice is First.
template <bool ConfigGameOptions::*Field, bool First>
int GetOption(const Config &config) {
    return config.options.*Field == First ? 0 : 1;
}

template <bool ConfigGameOptions::*Field, bool First>
void SetOption(Config &config, int choice) {
    config.options.*Field = choice == 0 ? First : !First;
}

template <bool ConfigGameOptions::*Field, bool First>
Row GameRow(const char *key, const char *label, const char *names, int game_help) {
    return {.key = key,
            .label = label,
            .game_help = game_help,
            .count = Two,
            .get = GetOption<Field, First>,
            .set = SetOption<Field, First>,
            .names = names};
}

template <bool Config::*Field>
int GetOnOff(const Config &config) {
    return config.*Field ? 0 : 1;
}

template <bool Config::*Field>
void SetOnOff(Config &config, int choice) {
    config.*Field = choice == 0;
}

template <bool Config::*Field>
Row OnOffRow(const char *key, const char *label, const char *help) {
    return {.key = key,
            .label = label,
            .help = help,
            .count = Two,
            .get = GetOnOff<Field>,
            .set = SetOnOff<Field>,
            .names = "On|Off"};
}

template <bool Config::*Field>
Row NamedRow(const char *key, const char *label, const char *help, const char *names) {
    return {.key = key,
            .label = label,
            .help = help,
            .count = Two,
            .get = [](const Config &config) { return config.*Field ? 1 : 0; },
            .set = [](Config &config, int choice) { config.*Field = choice == 1; },
            .names = names};
}

Row SettingRow(const char *key, const char *label, const char *help, int (*count)(const Config &),
               int (*get)(const Config &), void (*set)(Config &, int), std::string (*text)(const Config &),
               const char *names = nullptr, void (*restore)(Config &, const Config &) = nullptr) {
    return {.key = key,
            .label = label,
            .help = help,
            .count = count,
            .get = get,
            .set = set,
            .text = text,
            .names = names,
            .restore = restore};
}

std::string ChoiceName(const char *names, int choice) {
    std::string_view rest = names;
    for (int i = 0; i < choice; ++i) {
        rest = rest.substr(rest.find('|') + 1);
    }
    return std::string(rest.substr(0, rest.find('|')));
}

int Nearest(std::span<const double> choices, double value) {
    int best = 0;
    for (int i = 1; i < static_cast<int>(choices.size()); ++i) {
        if (std::abs(choices[i] - value) < std::abs(choices[best] - value)) {
            best = i;
        }
    }
    return best;
}

std::string Hundredths(float value) {
    return value > 10.0f ? std::format("{:.2g}", value) : std::format("{:.2f}", value);
}

constexpr OptionResolution kWindowSizes[] = {
    {1024, 768 },
    {1280, 720 },
    {1280, 960 },
    {1366, 768 },
    {1440, 1080},
    {1600, 900 },
    {1920, 1080},
    {2560, 1440},
    {3840, 2160},
};

std::vector<OptionResolution> g_sizes;

bool Desktop(const Config &config) {
    return config.window_width == 0 && config.window_height == 0;
}

int MapCount(const Config &) {
    return 4;
}

int MapChoice(const Config &config) {
    return config.options.map == 0 ? kMapOff : config.options.map - 1;
}

void SetMap(Config &config, int choice) {
    config.options.map = choice == kMapOff ? 0 : choice + 1;
}

int ResolutionCount(const Config &) {
    return static_cast<int>(g_sizes.size());
}

int ResolutionChoice(const Config &config) {
    double area = static_cast<double>(config.window_width) * config.window_height;
    int    best = 0;
    for (int i = 0; i < static_cast<int>(g_sizes.size()); ++i) {
        const OptionResolution &size = g_sizes[i];
        if (size.width == config.window_width && size.height == config.window_height) {
            return i;
        }
        double here = static_cast<double>(size.width) * size.height;
        double there = static_cast<double>(g_sizes[best].width) * g_sizes[best].height;
        if (std::abs(here - area) < std::abs(there - area)) {
            best = i;
        }
    }
    return best;
}

void SetResolution(Config &config, int choice) {
    config.window_width = g_sizes[choice].width;
    config.window_height = g_sizes[choice].height;
}

std::string ResolutionText(const Config &config) {
    return Desktop(config) ? "Desktop" : std::format("{} x {}", config.window_width, config.window_height);
}

int Fullscreen(const Config &config) {
    return config.fullscreen || Desktop(config) ? 1 : 0;
}

// A window left from Desktop takes the largest size that fits beside the desktop's panels.
void SetFullscreen(Config &config, int choice) {
    config.fullscreen = choice == 1;
    if (choice == 1 || !Desktop(config)) {
        return;
    }
    int              width = 0;
    int              height = 0;
    bool             known = WindowDisplaySize(width, height, true);
    OptionResolution window = known ? kWindowSizes[0] : OptionResolution{1280, 960};
    for (const OptionResolution &size : kWindowSizes) {
        if (known && size.width <= width && size.height <= height &&
            size.width * size.height > window.width * window.height) {
            window = size;
        }
    }
    config.window_width = window.width;
    config.window_height = window.height;
}

void RestoreFullscreen(Config &config, const Config &defaults) {
    config.fullscreen = defaults.fullscreen;
}

constexpr ConfigPresentMode kPresentModes[] = {ConfigPresentMode::Fifo, ConfigPresentMode::Mailbox,
                                               ConfigPresentMode::Immediate};

int PresentModeCount(const Config &) {
    return static_cast<int>(std::size(kPresentModes));
}

int PresentModeChoice(const Config &config) {
    return static_cast<int>(std::ranges::find(kPresentModes, config.present_mode) - std::begin(kPresentModes));
}

void SetPresentMode(Config &config, int choice) {
    config.present_mode = kPresentModes[choice];
}

constexpr ConfigFpsDetail kFpsDetails[] = {ConfigFpsDetail::Fps, ConfigFpsDetail::Ticks, ConfigFpsDetail::All};

int FpsDetailCount(const Config &) {
    return static_cast<int>(std::size(kFpsDetails));
}

int FpsDetailChoice(const Config &config) {
    return static_cast<int>(std::ranges::find(kFpsDetails, config.fps_detail) - std::begin(kFpsDetails));
}

void SetFpsDetail(Config &config, int choice) {
    config.fps_detail = kFpsDetails[choice];
}

constexpr double kMaxFps[] = {0, 30, 60, 75, 90, 120, 144, 165, 240};

int MaxFpsCount(const Config &) {
    return static_cast<int>(std::size(kMaxFps));
}

int MaxFpsChoice(const Config &config) {
    return Nearest(kMaxFps, config.max_fps);
}

void SetMaxFps(Config &config, int choice) {
    config.max_fps = kMaxFps[choice];
}

std::string MaxFpsText(const Config &config) {
    return config.max_fps > 0.0 ? std::format("{}", config.max_fps) : "Unlimited";
}

int Aspect(const Config &config) {
    return config.aspect == ConfigAspect::FourThree ? 1 : 0;
}

void SetAspect(Config &config, int choice) {
    config.aspect = choice == 1 ? ConfigAspect::FourThree : ConfigAspect::Auto;
}

// 50% to 200% in tenths.
int UiScaleCount(const Config &) {
    return 16;
}

int UiScaleChoice(const Config &config) {
    return std::clamp(static_cast<int>(std::lround(config.ui_scale * 10.0f)) - 5, 0, 15);
}

void SetUiScale(Config &config, int choice) {
    config.ui_scale = (choice + 5) / 10.0f;
}

std::string UiScaleText(const Config &config) {
    return std::format("{:.0f}%", config.ui_scale * 100.0f);
}

// 0% to 100% in fives.
int VolumeCount(const Config &) {
    return 21;
}

int VolumeChoice(const Config &config) {
    return static_cast<int>(std::lround(config.master_volume * 20.0f));
}

void SetVolume(Config &config, int choice) {
    config.master_volume = choice / 20.0f;
}

std::string VolumeText(const Config &config) {
    return std::format("{:.0f}%", config.master_volume * 100.0f);
}

// Hundredths up to 0.99, then tenths up to 10.
int MouseSensitivityCount(const Config &) {
    return 190;
}

int MouseSensitivityChoice(const Config &config) {
    float value = std::clamp(config.mouse_sensitivity, 0.01f, 10.0f);
    if (value < 0.995f) {
        return std::clamp(static_cast<int>(std::lround(value * 100.0f)) - 1, 0, 98);
    }
    return std::clamp(99 + static_cast<int>(std::lround((value - 1.0f) * 10.0f)), 99, 189);
}

void SetMouseSensitivity(Config &config, int choice) {
    config.mouse_sensitivity = choice < 99 ? (choice + 1) / 100.0f : 1.0f + (choice - 99) / 10.0f;
}

std::string MouseSensitivityText(const Config &config) {
    return Hundredths(config.mouse_sensitivity);
}

void RestoreMouseSensitivity(Config &config, const Config &defaults) {
    config.mouse_sensitivity = defaults.mouse_sensitivity;
}

constexpr const char *kZoomResetBindings[] = {"Mouse3", "Mouse4", "Mouse5", "Home", ""};
constexpr const char *kZoomResetNames[] = {"Middle Mouse", "Mouse4", "Mouse5", "Home", "Disabled"};

const std::vector<std::string> &ZoomResetBindings(const Config &config) {
    static const std::vector<std::string> defaults = {"Mouse3"};
    auto                                  binding = std::ranges::find(config.key_bindings, "zoom_reset", &ConfigKeyBinding::action);
    return binding == config.key_bindings.end() ? defaults : binding->keys;
}

int ZoomResetChoice(const Config &config) {
    const auto &keys = ZoomResetBindings(config);
    if (keys.empty()) {
        return 4;
    }
    for (int choice = 0; choice < 4; ++choice) {
        if (keys.size() == 1 && keys.front() == kZoomResetBindings[choice]) {
            return choice;
        }
    }
    return 5; // The file's custom binding remains a selectable choice until explicitly changed.
}

int ZoomResetCount(const Config &config) {
    return ZoomResetChoice(config) == 5 ? 6 : 5;
}

void SetZoomReset(Config &config, int choice) {
    if (choice < 0 || choice >= 5) {
        return;
    }
    auto                     binding = std::ranges::find(config.key_bindings, "zoom_reset", &ConfigKeyBinding::action);
    std::vector<std::string> keys;
    if (choice < 4) {
        keys.push_back(kZoomResetBindings[choice]);
    }
    if (binding == config.key_bindings.end()) {
        config.key_bindings.push_back({"zoom_reset", std::move(keys)});
    } else {
        binding->keys = std::move(keys);
    }
}

std::string ZoomResetText(const Config &config) {
    int choice = ZoomResetChoice(config);
    if (choice < 5) {
        return kZoomResetNames[choice];
    }
    std::string text;
    for (const auto &key : ZoomResetBindings(config)) {
        if (!text.empty()) {
            text += ", ";
        }
        text += key;
    }
    return text;
}

void RestoreZoomReset(Config &config, const Config &defaults) {
    auto binding = std::ranges::find(config.key_bindings, "zoom_reset", &ConfigKeyBinding::action);
    if (binding != config.key_bindings.end()) {
        config.key_bindings.erase(binding);
    }
    auto default_binding = std::ranges::find(defaults.key_bindings, "zoom_reset", &ConfigKeyBinding::action);
    if (default_binding != defaults.key_bindings.end()) {
        config.key_bindings.push_back(*default_binding);
    }
}

// 0.50 to 2.50 in twentieths.
int StickSensitivityCount(const Config &) {
    return 41;
}

int StickSensitivityChoice(const Config &config) {
    return static_cast<int>(std::lround((std::clamp(config.stick_sensitivity, 0.5f, 2.5f) - 0.5f) * 20.0f));
}

void SetStickSensitivity(Config &config, int choice) {
    config.stick_sensitivity = 0.5f + choice / 20.0f;
}

std::string StickSensitivityText(const Config &config) {
    return Hundredths(config.stick_sensitivity);
}

void RestoreStickSensitivity(Config &config, const Config &defaults) {
    config.stick_sensitivity = defaults.stick_sensitivity;
}

int GyroCount(const Config &) {
    return 4;
}

int GyroChoice(const Config &config) {
    return static_cast<int>(config.gyro);
}

void SetGyro(Config &config, int choice) {
    config.gyro = static_cast<ConfigGyro>(choice);
}

// 0.10 to 2.00 in twentieths.
int GyroSensitivityCount(const Config &) {
    return 39;
}

int GyroSensitivityChoice(const Config &config) {
    return static_cast<int>(std::lround((std::clamp(config.gyro_sensitivity, 0.1f, 2.0f) - 0.1f) * 20.0f));
}

void SetGyroSensitivity(Config &config, int choice) {
    config.gyro_sensitivity = 0.1f + choice / 20.0f;
}

std::string GyroSensitivityText(const Config &config) {
    return Hundredths(config.gyro_sensitivity);
}

void RestoreGyroSensitivity(Config &config, const Config &defaults) {
    config.gyro_sensitivity = defaults.gyro_sensitivity;
}

const Row kGameRows[] = {
    GameRow<&ConfigGameOptions::save_cursor_position, true>("game.save_cursor_position", "Save Cursor Position",
                                                            "On|Off", 0x15E),
    GameRow<&ConfigGameOptions::fast_messages, false>("game.message_speed", "Message Speed", "Normal|Fast", 0x160),
    GameRow<&ConfigGameOptions::clock, true>("game.clock", "Clock", "On|Off", 0x162),
    GameRow<&ConfigGameOptions::fast_time, false>("game.time_speed", "Time Speed", "Normal|Fast", 0x163),
    Row{.key = "game.map",
        .label = "Dungeon Map",
        .game_help = 0x164,
        .count = MapCount,
        .get = MapChoice,
        .set = SetMap,
        .names = "1|2|3|Off"},
    GameRow<&ConfigGameOptions::enemy_damage, true>("game.enemy_damage", "Enemy Damage", "On|Off", 0x165),
    GameRow<&ConfigGameOptions::player_damage, true>("game.player_damage", "Party Damage", "On|Off", 0x166),
    GameRow<&ConfigGameOptions::enemy_hp, true>("game.enemy_hp", "Enemy HP", "On|Off", 0x167),
    GameRow<&ConfigGameOptions::names, true>("game.names", "Names", "On|Off", 0x168),
    OnOffRow<&Config::discord_rich_presence>("discord.rich_presence", "Enable Discord",
                                             "\"Discord Rich Presence\"\nShows what you are\nplaying on Discord."),
};

const Row kDisplayRows[] = {
    SettingRow("video.fullscreen", "Window Mode", "\"Window Mode\"\nPlay in a window or\non the whole screen.", Two,
               Fullscreen, SetFullscreen, nullptr, "Windowed|Fullscreen", RestoreFullscreen),
    SettingRow("video.width", "Resolution",
               "\"Resolution\"\nThe window's size.\nFullscreen and Desktop\nuse the whole monitor.", ResolutionCount,
               ResolutionChoice, SetResolution, ResolutionText),
    SettingRow("video.present_mode", "V-Sync", "\"V-Sync\"\nOn: no tearing.\nFast: no tearing, less\ndelay. Off: may tear.",
               PresentModeCount, PresentModeChoice, SetPresentMode, nullptr, "On|Fast|Off"),
    SettingRow("video.max_fps", "Frame Limit", "\"Frame Limit\"\nThe most frames drawn\nin a second.", MaxFpsCount,
               MaxFpsChoice, SetMaxFps, MaxFpsText),
    SettingRow("video.aspect", "Aspect Ratio",
               "\"Aspect Ratio\"\nWide: the world fills\nthe window. 4:3: the\nPS2's picture.", Two, Aspect, SetAspect,
               nullptr, "Wide|4:3"),
    SettingRow("video.ui_scale", "Interface Size",
               "\"Interface Size\"\nThe size of the HUD\nand menus, from when\nOptions closes.", UiScaleCount,
               UiScaleChoice, SetUiScale, UiScaleText),
    OnOffRow<&Config::interpolation>("video.interpolation", "Smooth Motion",
                                     "\"Smooth Motion\"\nDraws frames between\nthe game's steps."),
    OnOffRow<&Config::show_fps>("video.show_fps", "FPS Counter", "\"FPS Counter\"\nShows the frame rate\nin the corner."),
    SettingRow("video.fps_detail", "FPS Info", "\"FPS Info\"\nWhat the counter shows:\nthe frame rate alone,\nwith ticks, or all.",
               FpsDetailCount, FpsDetailChoice, SetFpsDetail, nullptr, "FPS|FPS+Ticks|All"),
    GameRow<&ConfigGameOptions::soft_focus, true>("video.soft_focus", "Soft Focus", "On|Off", 0x169),
};

const Row kAudioRows[] = {
    SettingRow("audio.master_volume", "Volume", "\"Volume\"\nHow loud the game is.", VolumeCount, VolumeChoice,
               SetVolume, VolumeText),
    GameRow<&ConfigGameOptions::stereo, true>("audio.sound", "Sound", "Stereo|Mono", 0x161),
    NamedRow<&Config::soundtrack>("audio.soundtrack", "Soundtrack",
                                  "\"Soundtrack\"\nPS2: the game's music.\nCustom: your own\nrecordings, from the\nnext song.",
                                  "PS2|Custom"),
    OnOffRow<&Config::surround>("audio.surround", "Surround",
                                "\"Surround\"\nSpreads the sound to\n5.1 speakers."),
};

const Row kControlRows[] = {
    GameRow<&ConfigGameOptions::vibration, true>("input.vibration", "Vibration", "On|Off", 0x15F),
    SettingRow("input.mouse_sensitivity", "Mouse Sensitivity", "\"Mouse Sensitivity\"\nHow fast the mouse\nturns the camera.",
               MouseSensitivityCount, MouseSensitivityChoice, SetMouseSensitivity, MouseSensitivityText, nullptr,
               RestoreMouseSensitivity),
    OnOffRow<&Config::mouse_invert_y>("input.mouse_invert_y", "Invert Mouse Y",
                                      "\"Invert Mouse Y\"\nMoving the mouse up\nlooks down."),
    OnOffRow<&Config::mouse_zoom>("input.mouse_zoom", "Mouse Wheel Zoom",
                                  "\"Mouse Wheel Zoom\"\nScroll to move closer\nor farther from your\ncharacter."),
    SettingRow("input.bindings.zoom_reset", "Reset Zoom", "\"Reset Zoom\"\nRestores the normal\ncamera distance.",
               ZoomResetCount, ZoomResetChoice, SetZoomReset, ZoomResetText, nullptr, RestoreZoomReset),
    SettingRow("input.stick_sensitivity", "Stick Sensitivity",
               "\"Stick Sensitivity\"\nHow far a gamepad's\nstick has to tilt.", StickSensitivityCount,
               StickSensitivityChoice, SetStickSensitivity, StickSensitivityText, nullptr, RestoreStickSensitivity),
    OnOffRow<&Config::stick_invert_x>("input.stick_invert_x", "Invert Stick X",
                                      "\"Invert Stick X\"\nFlips the camera's\nleft and right."),
    OnOffRow<&Config::stick_invert_y>("input.stick_invert_y", "Invert Stick Y",
                                      "\"Invert Stick Y\"\nFlips the camera's\nup and down."),
    SettingRow("input.gyro", "Gyro", "\"Gyro\"\nWhen tilting the pad\nturns the camera.", GyroCount, GyroChoice, SetGyro,
               nullptr, "Off|Always|First Person|While Held"),
    SettingRow("input.gyro_sensitivity", "Gyro Sensitivity", "\"Gyro Sensitivity\"\nHow fast tilting\nturns the camera.",
               GyroSensitivityCount, GyroSensitivityChoice, SetGyroSensitivity, GyroSensitivityText, nullptr,
               RestoreGyroSensitivity),
    OnOffRow<&Config::gyro_invert_x>("input.gyro_invert_x", "Invert Gyro X",
                                     "\"Invert Gyro X\"\nFlips the gyro's\nleft and right."),
    OnOffRow<&Config::gyro_invert_y>("input.gyro_invert_y", "Invert Gyro Y",
                                     "\"Invert Gyro Y\"\nFlips the gyro's\nup and down."),
};

const Row kAccessibilityRows[] = {
    OnOffRow<&Config::qte_always_win>("game.qte_always_win", "Always Win QTEs",
                                      "\"Always Win QTEs\"\nButton prompts always\nend in a perfect."),
};

} // namespace

std::span<const Page> Pages() {
    static const std::vector<Page> pages = {
        {"Game",          "\"Game\"\nHow the game plays.",                           kGameRows         },
        {"Display",       "\"Display\"\nThe window and picture.",                    kDisplayRows      },
        {"Audio",         "\"Audio\"\nSound and music.",                             kAudioRows        },
        {"Controls",      "\"Controls\"\nMouse, gamepad and gyro.",                  kControlRows      },
        {"Accessibility", "\"Accessibility\"\nHelp with harder parts\nof the game.", kAccessibilityRows},
    };
    return pages;
}

std::string RowValue(const Row &row, const Config &config) {
    std::string value = row.text != nullptr ? row.text(config) : ChoiceName(row.names, row.get(config));
    if (ConfigAppliesOnRestart(row.key)) {
        value += " *";
    }
    return value;
}

bool StepRow(const Row &row, Config &config, int direction, bool wrap) {
    int count = row.count(config);
    int choice = row.get(config);
    int next = wrap ? (choice + direction + count) % count : std::clamp(choice + direction, 0, count - 1);
    if (next == choice) {
        return false;
    }
    row.set(config, next);
    return true;
}

void ResetPage(const Page &page, Config &config) {
    const Config defaults;
    for (const Row &row : page.rows) {
        if (row.restore != nullptr) {
            row.restore(config, defaults);
        } else {
            row.set(config, row.get(defaults));
        }
    }
}

void ListResolutions() {
    const Config &config = ConfigGet();
    int           width = 0;
    int           height = 0;
    bool          known = WindowDisplaySize(width, height, !config.fullscreen && !Desktop(config));
    g_sizes = OptionResolutionList(WindowDisplayModes(), config.window_width, config.window_height, known, width,
                                   height);
}

} // namespace options

int OptionZoomResetChoice(const Config &config) {
    return options::ZoomResetChoice(config);
}

int OptionZoomResetCount(const Config &config) {
    return options::ZoomResetCount(config);
}

std::string OptionZoomResetText(const Config &config) {
    return options::ZoomResetText(config);
}

void OptionSetZoomReset(Config &config, int choice) {
    options::SetZoomReset(config, choice);
}

void OptionRestoreZoomReset(Config &config, const Config &defaults) {
    options::RestoreZoomReset(config, defaults);
}
