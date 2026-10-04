// First-run settings screen for the Windows build.
//
// Shown once, after the game data has been extracted (or when the game is started with --setup), and writes
// config.json from the answers. Each question offers only values that work. Anything else in config.json
// (key bindings, mouse settings, view distances) is kept as it was.

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "config.hpp"
#include "paths.hpp"

namespace {

bool g_force = false;

struct Option {
    const wchar_t *label;
    int            value;
};

const Option kDisplay[] = {{L"Fullscreen", 1}, {L"Windowed", 0}};

// width * 10000 + height. A window with no size would be made fullscreen by the game, so every entry is a real size.
const Option kSize[] = {{L"1280 x 720", 1280 * 10000 + 720},
                        {L"1600 x 900", 1600 * 10000 + 900},
                        {L"1920 x 1080", 1920 * 10000 + 1080},
                        {L"2560 x 1440", 2560 * 10000 + 1440}};

const Option kPacing[] = {{L"Mailbox - smooth, no tearing (recommended)", 1},
                          {L"Immediate - lowest delay, may show tearing", 2},
                          {L"VSync (FIFO) - can make the game stutter on some PCs", 0}};

const Option kCap[] = {{L"60", 60}, {L"90", 90},   {L"120", 120},         {L"144", 144},
                       {L"165", 165}, {L"240", 240}, {L"Unlimited (uses the most power)", 0}};

// Game logic updates per second; the PAL game ran at 50, the NTSC game at 60.
const Option kSpeed[] = {{L"Original PAL speed (50)", 50},
                         {L"Faster - NTSC speed (60)", 60},
                         {L"Fast (75)", 75},
                         {L"Very fast (100)", 100}};

const Option kVolume[] = {{L"100%", 100}, {L"75%", 75}, {L"50%", 50}, {L"25%", 25}};

const Option kAspect[] = {{L"Fill the window (widescreen)", 0}, {L"Keep the original 4:3 shape", 1}};

constexpr int kComboDisplay = 101;
constexpr int kComboSize = 102;
constexpr int kComboPacing = 103;
constexpr int kComboCap = 104;
constexpr int kComboVolume = 105;
constexpr int kComboAspect = 106;
constexpr int kCheckSmooth = 107;
constexpr int kCheckFps = 108;
constexpr int kCheckDebug = 109;
constexpr int kComboSpeed = 110;
constexpr int kButtonStart = 1;
constexpr int kButtonDefaults = 2;

struct Dialog {
    HWND   window = nullptr;
    HWND   combo[7] = {};
    HWND   smooth = nullptr;
    HWND   fps = nullptr;
    HWND   debug = nullptr;
    bool   done = false;
    bool   defaults = false;
    int    speed_start = 0;
    bool   speed_custom = false;
    Config config;
};

Dialog g_dialog;

template <size_t N> HWND AddCombo(HWND parent, HFONT font, int id, int y, const Option (&options)[N], int selected) {
    HWND box = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                               250, y - 3, 330, 220, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                               GetModuleHandleW(nullptr), nullptr);
    SendMessageW(box, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    for (size_t i = 0; i < N; i++) {
        SendMessageW(box, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(options[i].label));
    }
    SendMessageW(box, CB_SETCURSEL, static_cast<WPARAM>(selected), 0);
    return box;
}

void AddLabel(HWND parent, HFONT font, int y, const wchar_t *text) {
    HWND label = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE, 20, y, 220, 22, parent, nullptr,
                                 GetModuleHandleW(nullptr), nullptr);
    SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

HWND AddCheck(HWND parent, HFONT font, int id, int y, const wchar_t *text, bool on) {
    HWND box = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 20, y, 560, 24,
                               parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr),
                               nullptr);
    SendMessageW(box, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SendMessageW(box, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
    return box;
}

template <size_t N> int IndexOf(const Option (&options)[N], int value, int fallback) {
    for (size_t i = 0; i < N; i++) {
        if (options[i].value == value) {
            return static_cast<int>(i);
        }
    }
    return fallback;
}

int Selected(HWND box) {
    int index = static_cast<int>(SendMessageW(box, CB_GETCURSEL, 0, 0));
    return index < 0 ? 0 : index;
}

void UpdateSizeEnabled() {
    bool windowed = kDisplay[Selected(g_dialog.combo[0])].value == 0;
    EnableWindow(g_dialog.combo[1], windowed ? TRUE : FALSE);
}

LRESULT CALLBACK Proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    switch (message) {
        case WM_COMMAND: {
            int id = LOWORD(w);
            if (id == kButtonStart || id == kButtonDefaults) {
                g_dialog.defaults = id == kButtonDefaults;
                g_dialog.done = true;
                return 0;
            }
            if (id == kComboDisplay && HIWORD(w) == CBN_SELCHANGE) {
                UpdateSizeEnabled();
            }
            return 0;
        }
        case WM_CLOSE:
            g_dialog.defaults = true;
            g_dialog.done = true;
            return 0;
        default:
            return DefWindowProcW(window, message, w, l);
    }
}

// Runs the dialog; returns the settings to save.
Config Ask(const Config &current) {
    g_dialog = Dialog{};
    g_dialog.config = current;

    WNDCLASSW cls = {};
    cls.lpfnWndProc = Proc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    cls.lpszClassName = L"DarkCloudSetup";
    RegisterClassW(&cls);

    NONCLIENTMETRICSW metrics = {};
    metrics.cbSize = sizeof(metrics);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
    HFONT font = CreateFontIndirectW(&metrics.lfMessageFont);

    const int width = 620;
    const int height = 486;
    RECT      frame = {0, 0, width, height};
    DWORD     style = WS_CAPTION | WS_SYSMENU;
    AdjustWindowRect(&frame, style, FALSE);
    int x = (GetSystemMetrics(SM_CXSCREEN) - (frame.right - frame.left)) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - (frame.bottom - frame.top)) / 3;
    HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME, cls.lpszClassName, L"Dark Cloud - settings", style, x, y,
                                  frame.right - frame.left, frame.bottom - frame.top, nullptr, nullptr, cls.hInstance,
                                  nullptr);
    g_dialog.window = window;

    HWND intro = CreateWindowExW(0, L"STATIC",
                                 L"Choose how the game should run. Every choice here can be changed later.", WS_CHILD | WS_VISIBLE,
                                 20, 14, 560, 22, window, nullptr, cls.hInstance, nullptr);
    SendMessageW(intro, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    int size = current.window_width * 10000 + current.window_height;
    int cap = static_cast<int>(current.max_fps + 0.5);
    int pacing = current.present_mode == ConfigPresentMode::Mailbox     ? 1
                 : current.present_mode == ConfigPresentMode::Immediate ? 2
                                                                        : 0;
    int volume = static_cast<int>(current.master_volume * 100.0f + 0.5f);
    int row = 52;
    const int step = 36;

    AddLabel(window, font, row, L"Screen");
    g_dialog.combo[0] = AddCombo(window, font, kComboDisplay, row, kDisplay, current.fullscreen ? 0 : 1);
    row += step;
    AddLabel(window, font, row, L"Window size (windowed only)");
    g_dialog.combo[1] = AddCombo(window, font, kComboSize, row, kSize, IndexOf(kSize, size, 0));
    row += step;
    AddLabel(window, font, row, L"Frame pacing");
    g_dialog.combo[2] = AddCombo(window, font, kComboPacing, row, kPacing, IndexOf(kPacing, pacing, 0));
    row += step;
    AddLabel(window, font, row, L"Frame rate limit");
    g_dialog.combo[3] = AddCombo(window, font, kComboCap, row, kCap, IndexOf(kCap, cap, 6));
    row += step;
    int  speed = static_cast<int>(current.tick_rate + 0.5);
    bool custom_speed = IndexOf(kSpeed, speed, -1) < 0 || static_cast<double>(speed) != current.tick_rate;
    AddLabel(window, font, row, L"Game speed (updates per second)");
    g_dialog.combo[6] = AddCombo(window, font, kComboSpeed, row, kSpeed, IndexOf(kSpeed, speed, 0));
    g_dialog.speed_start = Selected(g_dialog.combo[6]);
    g_dialog.speed_custom = custom_speed;
    row += step;
    AddLabel(window, font, row, L"Picture shape");
    g_dialog.combo[4] = AddCombo(window, font, kComboAspect, row, kAspect,
                                 current.aspect == ConfigAspect::FourThree ? 1 : 0);
    row += step;
    AddLabel(window, font, row, L"Volume");
    g_dialog.combo[5] = AddCombo(window, font, kComboVolume, row, kVolume, IndexOf(kVolume, volume, 0));
    row += step;
    g_dialog.smooth = AddCheck(window, font, kCheckSmooth, row, L"Smooth motion between game updates (recommended)",
                               current.interpolation);
    row += 28;
    g_dialog.fps = AddCheck(window, font, kCheckFps, row, L"Show the frame counter", current.show_fps);
    row += 28;
    g_dialog.debug = AddCheck(window, font, kCheckDebug, row,
                              L"Developer mode (debug menus and shortcuts; leave off for normal play)", current.debug_mode);
    row += 34;

    HWND note = CreateWindowExW(0, L"STATIC",
                                L"To open this screen again, start the game with  --setup.  Advanced options are in config.json "
                                L"in the save folder.",
                                WS_CHILD | WS_VISIBLE, 20, row, 560, 36, window, nullptr, cls.hInstance, nullptr);
    SendMessageW(note, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    HWND start = CreateWindowExW(0, L"BUTTON", L"Save and start", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                 width - 170, height - 46, 150, 30, window,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(kButtonStart)), cls.hInstance, nullptr);
    HWND defaults = CreateWindowExW(0, L"BUTTON", L"Use defaults", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                    width - 330, height - 46, 150, 30, window,
                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kButtonDefaults)), cls.hInstance,
                                    nullptr);
    SendMessageW(start, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SendMessageW(defaults, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    UpdateSizeEnabled();

    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);

    MSG msg;
    while (!g_dialog.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    Config result;
    if (g_dialog.defaults) {
        result = Config{};
    } else {
        result = current;
        result.fullscreen = kDisplay[Selected(g_dialog.combo[0])].value == 1;
        int chosen = kSize[Selected(g_dialog.combo[1])].value;
        result.window_width = chosen / 10000;
        result.window_height = chosen % 10000;
        int mode = kPacing[Selected(g_dialog.combo[2])].value;
        result.present_mode = mode == 1   ? ConfigPresentMode::Mailbox
                              : mode == 2 ? ConfigPresentMode::Immediate
                                          : ConfigPresentMode::Fifo;
        result.max_fps = kCap[Selected(g_dialog.combo[3])].value;
        // A speed typed into config.json that the list does not offer is kept unless the person picks another.
        if (!g_dialog.speed_custom || Selected(g_dialog.combo[6]) != g_dialog.speed_start) {
            result.tick_rate = kSpeed[Selected(g_dialog.combo[6])].value;
        }
        result.aspect = kAspect[Selected(g_dialog.combo[4])].value == 1 ? ConfigAspect::FourThree : ConfigAspect::Auto;
        result.master_volume = static_cast<float>(kVolume[Selected(g_dialog.combo[5])].value) / 100.0f;
        result.interpolation = SendMessageW(g_dialog.smooth, BM_GETCHECK, 0, 0) == BST_CHECKED;
        result.show_fps = SendMessageW(g_dialog.fps, BM_GETCHECK, 0, 0) == BST_CHECKED;
        result.debug_mode = SendMessageW(g_dialog.debug, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }
    DestroyWindow(window);
    DeleteObject(font);
    UnregisterClassW(cls.lpszClassName, cls.hInstance);
    return result;
}

} // namespace

// Removes --setup from the arguments and remembers it.
int SetupConsumeArgs(int argc, const char **argv) {
    int kept = 1;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--setup") == 0) {
            g_force = true;
        } else {
            argv[kept++] = argv[i];
        }
    }
    return kept;
}

void SetupIfNeeded(bool headless) {
    if (headless) {
        return;
    }
    const char *skip = std::getenv("DC_NO_SETUP");
    if (skip != nullptr && skip[0] != '\0' && skip[0] != '0') {
        return;
    }
    std::filesystem::path marker = PathsSaveRoot() / "setup_done.txt";
    std::error_code       error;
    if (!g_force && std::filesystem::exists(marker, error)) {
        return;
    }
    ConfigLoad();
    Config chosen = Ask(ConfigGet());
    std::filesystem::path config_path = PathsSaveRoot() / "config.json";
    {
        std::ofstream file(config_path, std::ios::binary | std::ios::trunc);
        file << ConfigSerialize(chosen);
    }
    ConfigLoad();
    std::ofstream(marker) << "settings screen shown; start the game with --setup to open it again\n";
}
