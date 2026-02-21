#include "Window.h"
#include "Imgui.h"
#include <imgui_impl_sdl3.h>
#include <imgui_impl_dx12.h>

#include <stdexcept>
#include <iostream>

// thanks grok

Window::Window(const std::string& title, uint32_t w, uint32_t h) {
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0) {
        throw std::runtime_error("SDL_InitSubSystem failed: " + std::string(SDL_GetError()));
    }

    Uint32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    // If you know you'll use Vulkan later, add SDL_WINDOW_VULKAN here (or set via properties)

    m_window = SDL_CreateWindow(title.c_str(), w, h, flags);
    if (!m_window) {
        throw std::runtime_error("SDL_CreateWindow failed: " + std::string(SDL_GetError()));
    }
}

Window::~Window() {
    if (m_window) {
        SDL_DestroyWindow(m_window);
    }
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    // In a full app, SDL_Quit() at program exit if other subsystems are used
}

uint32_t Window::getWidth() const {
    int logicalW = 0;
    SDL_GetWindowSize(m_window, &logicalW, nullptr);
    return static_cast<uint32_t>(logicalW);
}

uint32_t Window::getHeight() const {
    int logicalH = 0;
    SDL_GetWindowSize(m_window, nullptr, &logicalH);
    return static_cast<uint32_t>(logicalH);
}

uint32_t Window::getPixelWidth() const {
    int pixelW = 0;
    SDL_GetWindowSizeInPixels(m_window, &pixelW, nullptr);
    return static_cast<uint32_t>(pixelW);
}

uint32_t Window::getPixelHeight() const {
    int pixelH = 0;
    SDL_GetWindowSizeInPixels(m_window, nullptr, &pixelH);
    return static_cast<uint32_t>(pixelH);
}

bool Window::shouldClose() const {
    return m_shouldClose;
}

void Window::pollEvents(SDL_Event& event) {
    m_resizedFlag = false;
   
    if (event.type == SDL_EVENT_QUIT) {
        m_shouldClose = true;
    }
    else if (event.type == SDL_EVENT_WINDOW_RESIZED ||
        event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
        m_resizedFlag = true;
    }

}

bool Window::wasResized() {
    return m_resizedFlag;
}

void Window::acknowledgeResize() {
    m_resizedFlag = false;
}

void Window::setTitle(const std::string& title) {
    SDL_SetWindowTitle(m_window, title.c_str());
}

void* Window::getNativeHandle() const {
    if (!m_window) return nullptr;

    SDL_PropertiesID props = SDL_GetWindowProperties(m_window);
    if (props == 0) return nullptr;

#if defined(SDL_PLATFORM_WINDOWS)
    return SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#elif defined(SDL_PLATFORM_MACOS)
    return SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
#elif defined(SDL_PLATFORM_LINUX) && defined(SDL_VIDEO_DRIVER_WAYLAND)
    return SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
#elif defined(SDL_PLATFORM_LINUX) && defined(SDL_VIDEO_DRIVER_X11)
    return (void*)(uintptr_t)SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
#else
    return nullptr;
#endif
    // Add more platforms as needed (e.g., iOS: SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER)
}