#pragma once
#include <volk.h>
#include <glm/glm.hpp>

#include "window.hpp"

class Engine
{
public:
	void init(bool p_DebugEnabled);
	void run();
	void destroy();

private:
	void querySwapchainProperties();
	void recreateSwapchain(Window::Size p_Size);
	void initImgui();
	void imguiDraw(uint32_t p_FrameIndex);

	Window m_Window{};

	VkInstance m_Instance = VK_NULL_HANDLE;
	VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;
	VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
	VkDevice m_Device = VK_NULL_HANDLE;
	VolkDeviceTable m_DeviceTable{};
	uint32_t m_QueueFamilyIndex;
	VkQueue m_Queue;

	VkSwapchainKHR m_Swapchain = VK_NULL_HANDLE;
	uint32_t m_FramesInFlight = 2;
	VkSurfaceFormatKHR m_SwapchainFormat{};
	std::vector<VkImage> m_SwapchainImages;
	std::vector<VkImageView> m_SwapchainImageViews;
	std::vector<VkSemaphore> m_RenderFinishedSemaphores;
	VkExtent2D m_SwapchainExtent{};

	struct FrameData {
		VkCommandPool commandPool = VK_NULL_HANDLE;
		VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

		VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
		VkFence inFlightFence = VK_NULL_HANDLE;
	};
	std::vector<FrameData> m_Frames;

	VkPipeline m_MeshPipeline = VK_NULL_HANDLE;
	VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;

	VkDescriptorPool m_ImguiDescriptorPool = VK_NULL_HANDLE;
	glm::vec4 m_ColorMultiplier{1.f};

	uint32_t m_CurrentFrameIndex = 0;
};
