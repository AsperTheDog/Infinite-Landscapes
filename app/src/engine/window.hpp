#pragma once

#include <span>
#include <string_view>
#include <SDL3/SDL_video.h>
#include <vulkan/vulkan_core.h>

#include "signal.hpp"


class Window
{
public:
	struct Size
	{
		uint32_t width;
		uint32_t height;

		[[nodiscard]] VkExtent2D toVkExtent2D() const { return VkExtent2D{ width, height }; }
	};

	Window() = default;

	void init(Size p_Size, std::string_view p_Name, bool p_Maximize);
	void destroy(VkInstance p_Instance);

	void pollEvents();

	SDL_Window* operator*() const { return m_Window; }

	[[nodiscard]] Size getSize() const;
	[[nodiscard]] VkSurfaceKHR getSurface() const { return m_Surface; }

	void createSurface(VkInstance p_Instance);

	[[nodiscard]] bool shouldClose() const { return m_ShouldClose; }
	[[nodiscard]] bool isMinimized() const { return m_IsMinimized; }
	[[nodiscard]] bool isMouseCaptured() const { return m_MouseCaptured; }

	Signal<Size>& getOnPixelResize() { return m_OnPixelResize; }
	Signal<Size>& getOnResize() { return m_OnResize; }

	[[nodiscard]] std::span<const char* const> getRequiredInstanceExtensions() const;

	Signal<float, float>& getOnMouseMoved() { return m_OnMouseMoved; }
	Signal<uint32_t>& getOnKeyPressed() { return m_OnKeyPressed; }
	Signal<uint32_t>& getOnKeyReleased() { return m_OnKeyReleased; }
	Signal<uint32_t>& getOnMouseButtonPressed() { return m_OnMouseButtonPressed; }
	Signal<uint32_t>& getOnMouseButtonReleased() { return m_OnMouseButtonReleased; }
	Signal<float>& getOnMouseScrolled() { return m_OnMouseScrolled; }
	Signal<float>& getOnEventsProcessed() { return m_OnEventsProcessed; }
	Signal<bool>& getOnMouseCaptureChanged() { return m_OnMouseCaptureChanged; }

	void toggleMouseCaptured();

private:
	SDL_Window* m_Window = nullptr;
	VkSurfaceKHR m_Surface = VK_NULL_HANDLE;

	Signal<Size> m_OnPixelResize;
	Signal<Size> m_OnResize;
	Signal<float, float> m_OnMouseMoved;			// relX, relY, isMouseCaptured
	Signal<uint32_t> m_OnKeyPressed;				// key, isMouseCaptured
	Signal<uint32_t> m_OnKeyReleased;				// key, isMouseCaptured
	Signal<uint32_t> m_OnMouseButtonPressed;		// button, isMouseCaptured
	Signal<uint32_t> m_OnMouseButtonReleased;		// button, isMouseCaptured
	Signal<float> m_OnMouseScrolled;				// y, isMouseCaptured
	Signal<float> m_OnEventsProcessed;				// delta
	Signal<bool> m_OnMouseCaptureChanged;			// isMouseCaptured
	
	float m_PrevDelta = 0.f;
	float m_Delta = 0.f;

	bool m_MouseCaptured = false;
	bool m_ShouldClose = false;
	bool m_IsMinimized = false;

	inline static bool s_SDLInitialized = false;
};

