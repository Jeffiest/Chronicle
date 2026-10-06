#include "window.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

#include "gfx/gfx.hpp"

namespace {

SDL_Window                              *g_window = nullptr;
std::vector<void (*)(const SDL_Event &)> g_hooks;

[[noreturn]] void Fatal(const char *what) {
    std::fprintf(stderr, "%s: %s\n", what, SDL_GetError());
    std::exit(1);
}

void Check(bool ok, const char *what) {
    if (!ok) {
        std::fprintf(stderr, "window: %s: %s\n", what, SDL_GetError());
    }
}

struct Placement {
    int  width;
    int  height;
    bool fullscreen;
};

Placement Place(const WindowConfig &config, SDL_DisplayID display) {
    Placement placement = {config.width, config.height, config.fullscreen && !config.headless};
    if (placement.width > 0 && placement.height > 0) {
        return placement;
    }
    const SDL_DisplayMode *desktop = config.headless ? nullptr : SDL_GetDesktopDisplayMode(display);
    // A window cannot be given the whole monitor: the desktop keeps its panels. Fullscreen can.
    if (placement.width <= 0 && placement.height <= 0 && desktop != nullptr) {
        placement.fullscreen = true;
    }
    if (placement.width <= 0) {
        placement.width = desktop != nullptr ? desktop->w : 1280;
    }
    if (placement.height <= 0) {
        placement.height = desktop != nullptr ? desktop->h : 960;
    }
    return placement;
}

} // namespace

void WindowInit(const WindowConfig &config) {
    SDL_SetAppMetadata("Dark Cloud", nullptr, "dcdecomp.darkcloud");
    if (config.headless) {
#ifdef _WIN32
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, config.vulkan ? "windows" : "offscreen");
#else
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
#endif
        SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        Fatal("SDL_Init");
    }
    // SDL3 backs a Vulkan window on macOS with a CAMetalLayer (VK_EXT_metal_surface) by itself.
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (config.vulkan) {
        flags |= SDL_WINDOW_VULKAN;
#ifdef _WIN32
        if (config.headless) {
            flags |= SDL_WINDOW_HIDDEN;
        }
#endif
    }
    Placement placement = Place(config, SDL_GetPrimaryDisplay());
    if (placement.fullscreen) {
        flags |= SDL_WINDOW_FULLSCREEN;
    }
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "Dark Cloud");
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, placement.width);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, placement.height);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER, static_cast<Sint64>(flags));
    // Without a Vulkan surface, macOS would give the window OpenGL, which the offscreen driver cannot load.
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_EXTERNAL_GRAPHICS_CONTEXT_BOOLEAN, !config.vulkan);
    g_window = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
    if (g_window == nullptr) {
        Fatal("SDL_CreateWindow");
    }
}

void WindowSetMode(const WindowConfig &config) {
    if (g_window == nullptr || config.headless) {
        return;
    }
    SDL_DisplayID display = SDL_GetDisplayForWindow(g_window);
    Placement     placement = Place(config, display);
    // Some platforms change a window asynchronously: SDL_SyncWindow waits for the change, so the
    // pixel-size event that resizes the renderer, as for a window resized by hand, is in the queue.
    Check(SDL_SetWindowFullscreen(g_window, placement.fullscreen), "SDL_SetWindowFullscreen");
    Check(SDL_SyncWindow(g_window), "SDL_SyncWindow");
    if (placement.fullscreen) {
        return;
    }
    // A maximized window keeps its size and place.
    if (SDL_GetWindowFlags(g_window) & SDL_WINDOW_MAXIMIZED) {
        Check(SDL_RestoreWindow(g_window), "SDL_RestoreWindow");
        Check(SDL_SyncWindow(g_window), "SDL_SyncWindow");
    }
    Check(SDL_SetWindowSize(g_window, placement.width, placement.height), "SDL_SetWindowSize");
    Check(SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED_DISPLAY(display),
                                SDL_WINDOWPOS_CENTERED_DISPLAY(display)),
          "SDL_SetWindowPosition");
    Check(SDL_SyncWindow(g_window), "SDL_SyncWindow");
}

void WindowShutdown() {
    SDL_DestroyWindow(g_window);
    g_window = nullptr;
    g_hooks.clear();
    SDL_Quit();
}

SDL_Window *WindowHandle() { return g_window; }

bool WindowPollEvents() {
    bool      running = true;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        for (auto hook : g_hooks) {
            hook(event);
        }
        switch (event.type) {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                running = false;
                break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                gfx::RendererResize();
                break;
            default:
                break;
        }
    }
    return running;
}

void WindowAddEventHook(void (*hook)(const SDL_Event &event)) { g_hooks.push_back(hook); }
