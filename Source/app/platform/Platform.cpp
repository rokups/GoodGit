#include "platform/Platform.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstring>

namespace ggui {

Platform::~Platform() { shutdown(); }

bool Platform::init(const PlatformOptions& options, std::string& error)
{
    m_headless = options.headless;
    if (m_headless)
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    SDL_SetHint(SDL_HINT_APP_ID, "ggui");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        error = std::string("SDL_Init failed: ") + SDL_GetError();
        return false;
    }

    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    m_window = SDL_CreateWindow(options.title.c_str(), options.width, options.height, flags);
    if (!m_window) {
        error = std::string("SDL_CreateWindow failed: ") + SDL_GetError();
        return false;
    }

    m_device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL
            | SDL_GPU_SHADERFORMAT_METALLIB,
        false, nullptr);
    if (!m_device) {
        error = std::string("SDL_CreateGPUDevice failed: ") + SDL_GetError();
        return false;
    }

    if (!m_headless) {
        if (!SDL_ClaimWindowForGPUDevice(m_device, m_window)) {
            error = std::string("SDL_ClaimWindowForGPUDevice failed: ") + SDL_GetError();
            return false;
        }
        SDL_GPUPresentMode mode = options.vsync ? SDL_GPU_PRESENTMODE_VSYNC : SDL_GPU_PRESENTMODE_IMMEDIATE;
        if (!SDL_WindowSupportsGPUPresentMode(m_device, m_window, mode))
            mode = SDL_GPU_PRESENTMODE_VSYNC;
        SDL_SetGPUSwapchainParameters(m_device, m_window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode);
        m_format = SDL_GetGPUSwapchainTextureFormat(m_device, m_window);
        SDL_SetWindowPosition(m_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        SDL_ShowWindow(m_window);
    } else {
        m_format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
    ImGui_ImplSDL3_InitForSDLGPU(m_window);
    ImGui_ImplSDLGPU3_InitInfo info = {};
    info.Device = m_device;
    info.ColorTargetFormat = static_cast<SDL_GPUTextureFormat>(m_format);
    info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
    ImGui_ImplSDLGPU3_Init(&info);
    m_imguiReady = true;
    spdlog::info("GPU driver: {}, headless: {}", SDL_GetGPUDeviceDriver(m_device), m_headless);
    return true;
}

void Platform::shutdown()
{
    if (m_device)
        SDL_WaitForGPUIdle(m_device);
    if (m_imguiReady) {
        ImGui_ImplSDL3_Shutdown();
        ImGui_ImplSDLGPU3_Shutdown();
        ImGui::DestroyContext();
        m_imguiReady = false;
    }
    if (m_offscreen) {
        SDL_ReleaseGPUTexture(m_device, m_offscreen);
        m_offscreen = nullptr;
    }
    if (m_device) {
        if (!m_headless && m_window)
            SDL_ReleaseWindowFromGPUDevice(m_device, m_window);
        SDL_DestroyGPUDevice(m_device);
        m_device = nullptr;
    }
    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
        SDL_Quit();
    }
}

bool Platform::pollEvents()
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);
        if (event.type == SDL_EVENT_QUIT)
            m_quit = true;
        if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(m_window))
            m_quit = true;
    }
    m_minimized = (SDL_GetWindowFlags(m_window) & SDL_WINDOW_MINIMIZED) != 0;
    return !m_quit;
}

void Platform::beginFrame()
{
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void Platform::framebufferSize(int& w, int& h) const { SDL_GetWindowSizeInPixels(m_window, &w, &h); }

bool Platform::ensureOffscreen(std::uint32_t w, std::uint32_t h)
{
    if (m_offscreen && m_offW == w && m_offH == h)
        return true;
    if (m_offscreen)
        SDL_ReleaseGPUTexture(m_device, m_offscreen);
    SDL_GPUTextureCreateInfo ci = {};
    ci.type = SDL_GPU_TEXTURETYPE_2D;
    ci.format = static_cast<SDL_GPUTextureFormat>(m_format);
    ci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ci.width = w;
    ci.height = h;
    ci.layer_count_or_depth = 1;
    ci.num_levels = 1;
    ci.sample_count = SDL_GPU_SAMPLECOUNT_1;
    m_offscreen = SDL_CreateGPUTexture(m_device, &ci);
    m_offW = w;
    m_offH = h;
    return m_offscreen != nullptr;
}

void Platform::endFrame()
{
    ImGui::Render();
    ImDrawData* drawData = ImGui::GetDrawData();
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(m_device);
    SDL_GPUTexture* swapchain = nullptr;
    std::uint32_t swW = 0;
    std::uint32_t swH = 0;
    if (!m_headless)
        SDL_WaitAndAcquireGPUSwapchainTexture(cmd, m_window, &swapchain, &swW, &swH);

    int fbW = 0;
    int fbH = 0;
    framebufferSize(fbW, fbH);
    const std::uint32_t w = swapchain ? swW : static_cast<std::uint32_t>(fbW > 0 ? fbW : 1);
    const std::uint32_t h = swapchain ? swH : static_cast<std::uint32_t>(fbH > 0 ? fbH : 1);
    const bool drawable = !m_minimized && drawData->DisplaySize.x > 0 && drawData->DisplaySize.y > 0;
    if (drawable && ensureOffscreen(w, h)) {
        ImGui_ImplSDLGPU3_PrepareDrawData(drawData, cmd);
        SDL_GPUColorTargetInfo target = {};
        target.texture = m_offscreen;
        target.clear_color = SDL_FColor{0.08f, 0.08f, 0.09f, 1.0f};
        target.load_op = SDL_GPU_LOADOP_CLEAR;
        target.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &target, 1, nullptr);
        ImGui_ImplSDLGPU3_RenderDrawData(drawData, cmd, pass);
        SDL_EndGPURenderPass(pass);
        if (swapchain) {
            SDL_GPUBlitInfo blit = {};
            blit.source.texture = m_offscreen;
            blit.source.w = w;
            blit.source.h = h;
            blit.destination.texture = swapchain;
            blit.destination.w = w;
            blit.destination.h = h;
            blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
            blit.filter = SDL_GPU_FILTER_NEAREST;
            SDL_BlitGPUTexture(cmd, &blit);
        }
    }
    SDL_SubmitGPUCommandBuffer(cmd);
}

bool Platform::readPixels(int x, int y, int w, int h, std::uint32_t* out)
{
    if (!m_offscreen || w <= 0 || h <= 0)
        return false;
    // ImGui coordinates are in points; the offscreen texture is in pixels.
    const float scale = ImGui::GetIO().DisplayFramebufferScale.x;
    const auto px = static_cast<std::uint32_t>(x * scale);
    const auto py = static_cast<std::uint32_t>(y * scale);
    auto pw = static_cast<std::uint32_t>(w * scale);
    auto ph = static_cast<std::uint32_t>(h * scale);
    if (px >= m_offW || py >= m_offH)
        return false;
    pw = std::min(pw, m_offW - px);
    ph = std::min(ph, m_offH - py);

    SDL_GPUTransferBufferCreateInfo tci = {};
    tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    tci.size = pw * ph * 4;
    SDL_GPUTransferBuffer* buffer = SDL_CreateGPUTransferBuffer(m_device, &tci);
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(m_device);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion region = {};
    region.texture = m_offscreen;
    region.x = px;
    region.y = py;
    region.w = pw;
    region.h = ph;
    region.d = 1;
    SDL_GPUTextureTransferInfo dst = {};
    dst.transfer_buffer = buffer;
    SDL_DownloadFromGPUTexture(copy, &region, &dst);
    SDL_EndGPUCopyPass(copy);
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    SDL_WaitForGPUFences(m_device, true, &fence, 1);
    SDL_ReleaseGPUFence(m_device, fence);

    const auto* src = static_cast<const std::uint8_t*>(SDL_MapGPUTransferBuffer(m_device, buffer, false));
    const bool bgra = m_format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM
        || m_format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB;
    // The capture tool expects exactly w*h pixels in points; sample the pixel image.
    for (int row = 0; row < h; ++row) {
        const auto sy = std::min<std::uint32_t>(static_cast<std::uint32_t>(row * scale), ph - 1);
        for (int col = 0; col < w; ++col) {
            const auto sx = std::min<std::uint32_t>(static_cast<std::uint32_t>(col * scale), pw - 1);
            const std::uint8_t* p = src + (sy * pw + sx) * 4;
            const std::uint8_t r = bgra ? p[2] : p[0];
            const std::uint8_t b = bgra ? p[0] : p[2];
            out[row * w + col] = static_cast<std::uint32_t>(r) | (static_cast<std::uint32_t>(p[1]) << 8)
                | (static_cast<std::uint32_t>(b) << 16) | (0xFFu << 24);
        }
    }
    SDL_UnmapGPUTransferBuffer(m_device, buffer);
    SDL_ReleaseGPUTransferBuffer(m_device, buffer);
    return true;
}

void Platform::setTitle(const std::string& title) { SDL_SetWindowTitle(m_window, title.c_str()); }

void Platform::raise()
{
    if (!m_window || m_headless)
        return;
    SDL_RestoreWindow(m_window);
    SDL_RaiseWindow(m_window);
}

} // namespace ggui
