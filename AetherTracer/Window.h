#pragma once

#include "InputManager.h"

#include <SDL3/SDL.h>
#include <cstdint>
#include <string>

// thanks grok

class Window {
public:
    Window(const std::string& title = "Renderer", int initialWidth = 1200, int initialHeight = 1200);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Core interface — used by almost all renderers
    uint32_t getWidth() const;   // logical size
    uint32_t getHeight() const;

    uint32_t getPixelWidth() const;   // backbuffer pixel size (DPI-aware)
    uint32_t getPixelHeight() const;

    bool shouldClose() const;
    void pollEvents(SDL_Event &event);

    bool wasResized();            // call after pollEvents
    void acknowledgeResize();

    void setTitle(const std::string& title);

    // For APIs that want the native handle (DX12, Metal, raw Vulkan vkCreateWin32SurfaceKHR, etc.)
    void* getNativeHandle() const;

    // For Vulkan when using SDL's helper (most people do this)
    SDL_Window* getSDLHandle() const { return m_window; }

    void setRelativeMouse(bool& state) {
        SDL_SetWindowRelativeMouseMode(m_window, state);
    }

private:
    SDL_Window* m_window = nullptr;
    bool m_resizedFlag = false;
    bool m_shouldClose = false;
};