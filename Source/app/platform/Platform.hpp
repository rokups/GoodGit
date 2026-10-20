// SDL3 window, SDL_GPU renderer and the Dear ImGui backends.
//
// Every frame is rendered into an offscreen texture and then blitted to the swapchain. The
// offscreen texture makes screenshots possible (test engine capture) and lets the app run
// fully headless (SDL "offscreen" video driver, no swapchain) for CI.
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

struct SDL_Window;
struct SDL_GPUDevice;
struct SDL_GPUTexture;
struct SDL_GPUFence;
union SDL_Event;

namespace ggui {

struct PlatformOptions {
    std::string title = "ggui";
    int width = 1600;
    int height = 1000;
    bool headless = false; // offscreen video driver, no swapchain
    bool vsync = true;
    // The loop may sleep in pollEvents() when the UI is idle. Off for tests, smoke and headless runs.
    bool idleWait = false;
};

class Platform {
public:
    Platform() = default;
    ~Platform();
    Platform(const Platform&) = delete;
    Platform& operator=(const Platform&) = delete;

    // Creates the window, GPU device and the ImGui context + backends.
    bool init(const PlatformOptions& options, std::string& error);
    void shutdown();

    // Polls events. Returns false when the user asked to quit.
    // When `waitMs` is more than 0, waits up to `waitMs` milliseconds for the first event.
    bool pollEvents(int waitMs = 0);
    // The last pollEvents() call handled at least one event from the window system (input).
    bool eventsSeen() const { return m_eventsSeen; }
    // The last pollEvents() call handled a wake event and no other event.
    bool woken() const { return m_woken && !m_eventsSeen; }
    // The number of wake events handled since init. Only the UI thread reads it.
    std::uint64_t wakesHandled() const { return m_wakesHandled; }
    // Wakes a sleeping pollEvents() call. Any thread can call it.
    void wake();
    // A wake event is in the queue and pollEvents() did not read it yet.
    bool wakePending() const { return m_wakePending.load(); }
    bool quitRequested() const { return m_quit; }
    // Paths dropped on the window since the last call, one list per drop (in the order given).
    std::vector<std::vector<std::string>> takeDrops() { return std::exchange(m_drops, {}); }
    void requestQuit() { m_quit = true; }
    void cancelQuit() { m_quit = false; }
    // The window has input focus (from the focus events; true until the first loss).
    bool focused() const { return m_focused; }
    bool minimized() const { return m_minimized; }
    // The window is hidden (not shown by the window system).
    bool hidden() const { return m_hidden; }

    void beginFrame();
    void endFrame(); // renders ImGui draw data and presents

    // RGBA8 pixels of the last rendered frame, rows top to bottom.
    bool readPixels(int x, int y, int w, int h, std::uint32_t* out);
    void framebufferSize(int& w, int& h) const;

    SDL_Window* window() const { return m_window; }
    bool headless() const { return m_headless; }
    void setTitle(const std::string& title);
    // Brings the window to the front (a request from git needs the user's attention).
    void raise();
    // Flashes the window until it has focus (a compositor may refuse raise()).
    void attention();

private:
    void handleEvent(const SDL_Event& event);
    bool ensureOffscreen(std::uint32_t w, std::uint32_t h);

    SDL_Window* m_window = nullptr;
    SDL_GPUDevice* m_device = nullptr;
    SDL_GPUTexture* m_offscreen = nullptr;
    // Headless: the last frame's fence. Without a swapchain nothing else stops frames piling up
    // on the GPU (a slow software device runs out of descriptor heaps, and SDL's D3D12 crashes).
    SDL_GPUFence* m_frameFence = nullptr;
    std::uint32_t m_offW = 0;
    std::uint32_t m_offH = 0;
    int m_format = 0; // SDL_GPUTextureFormat
    bool m_headless = false;
    bool m_quit = false;
    std::vector<std::vector<std::string>> m_drops;
    std::vector<std::string> m_dropping; // between SDL_EVENT_DROP_BEGIN and _COMPLETE
    bool m_inDrop = false;
    bool m_imguiReady = false;
    bool m_minimized = false;
    bool m_hidden = false;
    bool m_focused = true;
    bool m_eventsSeen = false;
    bool m_woken = false;
    std::uint64_t m_wakesHandled = 0;
    std::uint32_t m_wakeEvent = 0; // SDL event type from SDL_RegisterEvents
    std::atomic<bool> m_wakePending{false};
};

} // namespace ggui
