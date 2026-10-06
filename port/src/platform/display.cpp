#include "display.hpp"

#include <utility>

namespace {

DisplayWindow                   g_ops;
int                             g_override_width = 0;
int                             g_override_height = 0;
bool                            g_override_headless = false;
std::optional<WindowModeResult> g_report;
bool                            g_reconciling = false;
DisplayWarning                  g_warning = DisplayWarning::None;
// The mode the fields describe.
WindowMode g_shown;
// A fullscreen change SDL took and the window has not been seen to make: true to enter.
std::optional<bool> g_outstanding;
// The fields of the latest change of them, and the mode they ask of the window.
Config              g_intent;
WindowMode          g_intent_mode;
std::optional<bool> g_pump_save;

void CopyFields(Config &config, const Config &from) {
    config.window_width = from.window_width;
    config.window_height = from.window_height;
    config.fullscreen = from.fullscreen;
}

// Fullscreen keeps the size asked for, which is the window's for later.
void Describe(Config &config, const WindowMode &mode) {
    config.fullscreen = mode.fullscreen;
    if (!mode.fullscreen) {
        config.window_width = mode.width;
        config.window_height = mode.height;
    }
}

// Makes the fields what the window already is, without asking anything of it.
bool Reconcile(const Config &config) {
    struct Scope {
        Scope() { g_reconciling = true; }

        ~Scope() { g_reconciling = false; }
    } scope;

    return ConfigChange(config);
}

} // namespace

void DisplayUseWindow(const DisplayWindow &window) {
    g_ops = window;
    g_report.reset();
    g_warning = DisplayWarning::None;
    g_outstanding.reset();
    g_pump_save.reset();
}

void DisplaySetOverrides(int width, int height, bool headless) {
    g_override_width = width;
    g_override_height = height;
    g_override_headless = headless;
}

WindowConfig DisplayWindowConfig(const Config &config) {
    WindowConfig window;
    window.width = g_override_width > 0 ? g_override_width : config.window_width;
    window.height = g_override_height > 0 ? g_override_height : config.window_height;
    window.fullscreen = config.fullscreen;
    window.headless = g_override_headless;
    return window;
}

void DisplayChanged(const Config &before, const Config &after) {
    if (g_reconciling || (after.window_width == before.window_width && after.window_height == before.window_height &&
                          after.fullscreen == before.fullscreen)) {
        return;
    }
    g_report = g_ops.set_mode(DisplayWindowConfig(after));
}

bool DisplayApply(const Config &config) {
    Config before = ConfigGet();
    g_report.reset();
    bool                            saved = ConfigChange(config);
    std::optional<WindowModeResult> report = std::exchange(g_report, std::nullopt);
    if (!report) {
        return saved;
    }
    // Only a fullscreen request SDL took replaces one still outstanding; a size or no change at all
    // leaves it to finish.
    if (report->fullscreen_request) {
        g_outstanding = report->fullscreen_request;
    }
    if (g_outstanding == report->observed.fullscreen) {
        g_outstanding.reset();
    }
    g_intent = ConfigGet();
    g_intent_mode = report->requested;
    g_shown = report->observed;
    if (report->Applied()) {
        g_warning = DisplayWarning::None;
        return saved;
    }
    Config fields = ConfigGet();
    if (report->observed == report->before) {
        g_warning = DisplayWarning::Kept;
        CopyFields(fields, before);
    } else {
        g_warning = DisplayWarning::Changed;
        Describe(fields, report->observed);
    }
    return Reconcile(fields);
}

void DisplayPump() {
    WindowMode observed;
    if (!g_ops.take_event() || (g_warning == DisplayWarning::None && !g_outstanding) ||
        !g_ops.current_mode(observed)) {
        return;
    }
    if (g_outstanding == observed.fullscreen) {
        g_outstanding.reset();
    }
    if (observed == g_shown) {
        return;
    }
    g_shown = observed;
    Config fields = ConfigGet();
    CopyFields(fields, g_intent);
    if (observed == g_intent_mode) {
        g_warning = DisplayWarning::None;
    } else {
        g_warning = DisplayWarning::Changed;
        Describe(fields, observed);
    }
    g_pump_save = Reconcile(fields);
}

std::optional<bool> DisplayTakePumpSave() {
    return std::exchange(g_pump_save, std::nullopt);
}

DisplayWarning DisplayGetWarning() {
    return g_warning;
}

WindowMode DisplayShownMode() {
    return g_shown;
}
