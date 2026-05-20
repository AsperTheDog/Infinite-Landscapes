#pragma once
#include <volk.h>
#include <glm/glm.hpp>

#include "camera.hpp"
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
	void createDepthResources(VkExtent2D p_Extent);
	void destroyDepthResources();
	uint32_t findMemoryType(uint32_t p_TypeFilter, VkMemoryPropertyFlags p_Properties) const;
	void initImgui();
	void imguiDraw(uint32_t p_FrameIndex);
	void toggleImgui();

	struct StorageBuffer
	{
		VkBuffer buffer = VK_NULL_HANDLE;
		VkDeviceMemory memory = VK_NULL_HANDLE;
		VkDeviceAddress address = 0;
		VkDeviceSize size = 0;
	};

	void createStorageBuffer(VkDeviceSize p_Size, VkBufferUsageFlags p_ExtraUsage, StorageBuffer& p_Out) const;
	void destroyStorageBuffer(StorageBuffer& p_Buffer) const;
	VkShaderModule createShaderModule(const std::vector<uint32_t>& p_Spirv) const;
	VkPipeline createComputePipeline(VkShaderModule p_Module, VkPipelineLayout p_Layout) const;

	Window m_Window{};
	Camera m_Camera{glm::vec3{0.f, -100.f, 0.f}, glm::vec3{0.0f, 0.0f, -1.0f}};

	VkInstance m_Instance = VK_NULL_HANDLE;
	VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;
	VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
	VkDevice m_Device = VK_NULL_HANDLE;
	VolkDeviceTable m_DeviceTable{};
	VkQueue m_Queue = VK_NULL_HANDLE;
	uint32_t m_QueueFamilyIndex = UINT32_MAX;

	uint32_t m_FramesInFlight = 2;
	VkSwapchainKHR m_Swapchain = VK_NULL_HANDLE;
	VkSurfaceFormatKHR m_SwapchainFormat{};
	std::vector<VkImage> m_SwapchainImages;
	std::vector<VkImageView> m_SwapchainImageViews;
	std::vector<VkSemaphore> m_RenderFinishedSemaphores;
	VkExtent2D m_SwapchainExtent{};

	static constexpr VkFormat s_DepthFormat = VK_FORMAT_D32_SFLOAT;
	VkImage m_DepthImage = VK_NULL_HANDLE;
	VkDeviceMemory m_DepthMemory = VK_NULL_HANDLE;
	VkImageView m_DepthImageView = VK_NULL_HANDLE;

	struct FrameData {
		VkCommandPool commandPool = VK_NULL_HANDLE;
		VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

		VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
		VkFence inFlightFence = VK_NULL_HANDLE;

		StorageBuffer nodeBufferA{};
		StorageBuffer nodeBufferB{};
		StorageBuffer leafBuffer{};
		StorageBuffer hashBuffer{};
		StorageBuffer indirectBuffer{};
	};
	std::vector<FrameData> m_Frames;

	VkPipeline m_MeshPipeline = VK_NULL_HANDLE;
	VkPipelineLayout m_GraphicsPipelineLayout = VK_NULL_HANDLE;

	VkPipelineLayout m_ComputePipelineLayout = VK_NULL_HANDLE;
	VkPipeline m_RefinePipeline = VK_NULL_HANDLE;
	VkPipeline m_PrepareIndirectPipeline = VK_NULL_HANDLE;
	VkPipeline m_HashBuildPipeline = VK_NULL_HANDLE;
	VkPipeline m_NeighborLookupPipeline = VK_NULL_HANDLE;

	static constexpr uint32_t s_MaxNodes  = 65536;
	static constexpr uint32_t s_MaxLeaves = 65536;
	static constexpr uint32_t s_HashSize  = 131072;

	bool m_ImguiActive = true;
	VkDescriptorPool m_ImguiDescriptorPool = VK_NULL_HANDLE;
	float m_ImguiRootSize = 16000.0f;
	uint32_t m_ImguiMaxDepth = 12;
	float m_ImguiMeshletPixelTarget = 200.0f;
	bool m_ImguiWireframe = false;
	bool m_ImguiEdgeSnap = true;


	uint32_t m_CurrentFrameIndex = 0;
};
