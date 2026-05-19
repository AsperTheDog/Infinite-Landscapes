#include "window.hpp"

#include <cassert>
#include <stdexcept>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_Vulkan.h>

#include "backends/imgui_impl_sdl3.h"
#include "spdlog/spdlog.h"

void Window::init(const Size p_Size, const std::string_view p_Name, const bool p_Maximize)
{
	if (!s_SDLInitialized)
	{
		if (!SDL_Init(SDL_INIT_VIDEO))
			throw std::runtime_error("Failed to initialize SDL: " + std::string(SDL_GetError()));
		s_SDLInitialized = true;
		spdlog::info("Initialized SDL");
	}

	m_Window = SDL_CreateWindow(p_Name.data(), p_Size.width, p_Size.height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | (p_Maximize ? SDL_WINDOW_MAXIMIZED : 0));
	if (!m_Window)
		throw std::runtime_error("Failed to create SDL window: " + std::string(SDL_GetError()));
	spdlog::info("Created window '{}' with size {}x{}", p_Name, p_Size.width, p_Size.height);
}

void Window::destroy(const VkInstance p_Instance)
{
	if (m_Surface != VK_NULL_HANDLE)
	{
		SDL_Vulkan_DestroySurface(p_Instance, m_Surface, nullptr);
		spdlog::debug("Destroyed Vulkan surface (ID: {})", fmt::ptr(m_Surface));
		m_Surface = VK_NULL_HANDLE;
	}
	if (m_Window)
	{
		SDL_DestroyWindow(m_Window);
		spdlog::debug("Destroyed window (ID: {})", fmt::ptr(m_Window));
		m_Window = nullptr;
	}
}

void Window::pollEvents()
{
	assert(m_Window);

	SDL_Event l_Event;
	while (SDL_PollEvent(&l_Event))
	{
		ImGui_ImplSDL3_ProcessEvent(&l_Event);
		switch (l_Event.type)
		{
		case SDL_EVENT_QUIT:
			{
				m_ShouldClose = true;
			}
			break;
		case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
			{
				int32_t l_PxW = 0, l_PxH = 0;
				SDL_GetWindowSizeInPixels(m_Window, &l_PxW, &l_PxH);
				if (l_PxW > 0 && l_PxH > 0)
					m_OnPixelResize.emit({.width = static_cast<uint32_t>(l_PxW), .height = static_cast<uint32_t>(l_PxH) });
			}
			break;
		case SDL_EVENT_WINDOW_RESIZED:
			{
				if (l_Event.window.data1 > 0 && l_Event.window.data2 > 0)
				{
					m_OnResize.emit(Size{.width = static_cast<uint32_t>(l_Event.window.data1), .height = static_cast<uint32_t>(l_Event.window.data2) });
					m_IsMinimized = false;
				}
			}
			break;
		case SDL_EVENT_WINDOW_MINIMIZED:
			{
				m_IsMinimized = true;
			}
			break;
		case SDL_EVENT_WINDOW_RESTORED:
			{
				m_IsMinimized = false;
			}
			break;
		case SDL_EVENT_MOUSE_MOTION:
			{
				m_OnMouseMoved.emit(l_Event.motion.xrel, l_Event.motion.yrel);
			}
			break;
		case SDL_EVENT_KEY_DOWN:
			{
				m_OnKeyPressed.emit(l_Event.key.key);
			}
			break;
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
			{
				m_OnMouseButtonPressed.emit(l_Event.button.button);
			}
			break;
		case SDL_EVENT_MOUSE_BUTTON_UP:
			{
				m_OnMouseButtonReleased.emit(l_Event.button.button);
			}
			break;
		case SDL_EVENT_MOUSE_WHEEL:
			{
				m_OnMouseScrolled.emit(l_Event.wheel.y);
			}
			break;
		case SDL_EVENT_KEY_UP:
			{
				m_OnKeyReleased.emit(l_Event.key.key);
			}
			break;
		default: ;
		}
	}
	const uint64_t l_Now = SDL_GetTicks();
	m_Delta = (static_cast<float>(l_Now) - m_PrevDelta) * 0.001f;
	m_PrevDelta = static_cast<float>(l_Now);
	m_OnEventsProcessed.emit(m_Delta);
}

Window::Size Window::getSize() const
{
	int32_t l_Width, l_Height;
	SDL_GetWindowSize(m_Window, &l_Width, &l_Height);
	return {.width = static_cast<uint32_t>(l_Width), .height = static_cast<uint32_t>(l_Height) };
}

void Window::createSurface(const VkInstance p_Instance)
{
	assert(m_Window);
	if (!SDL_Vulkan_CreateSurface(m_Window, p_Instance, nullptr, &m_Surface))
		throw std::runtime_error("Failed to create Vulkan surface with SDL: " + std::string(SDL_GetError()));

	spdlog::info("Created Vulkan surface (ID: {})", fmt::ptr(m_Surface));
}

std::span<const char* const> Window::getRequiredInstanceExtensions() const
{
	assert(m_Window);

	uint32_t l_ExtensionCount;
	const char* const* l_Exts = SDL_Vulkan_GetInstanceExtensions(&l_ExtensionCount);
	if (!l_Exts)
		throw std::runtime_error("Failed to get required Vulkan extensions from SDL: " + std::string(SDL_GetError()));
	return std::span{ l_Exts, l_ExtensionCount };
}
