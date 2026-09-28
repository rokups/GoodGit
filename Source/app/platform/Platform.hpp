// SDL3 window, SDL_GPU renderer and the Dear ImGui backends.
//
// Every frame is rendered into an offscreen texture and then blitted to the swapchain. The
// offscreen texture makes screenshots possible (test engine capture) and lets the app run
// fully headless (SDL "offscreen" video driver, no swapchain) for CI.
#pragma once

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
    bool pollEvents();
    bool quitRequested() const { return m_quit; }
    // Paths dropped on the window since the last call, one list per drop (in the order given).
    std::vector<std::vector<std::string>> takeDrops() { return std::exchange(m_drops, {}); }
    void requestQuit() { m_quit = true; }
    void cancelQuit() { m_quit = false; }

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

private:
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
};

} // namespace ggui
