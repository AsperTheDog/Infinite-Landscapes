#include "engine.hpp"

#include <array>
#include <volk.h>
#include <spdlog/spdlog.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include <SDL3/SDL_keycode.h>

#include "shader.hpp"
#include "vk_base.hpp"

constexpr bool g_AssertOnError = true;

struct ComputePC {
    alignas(16) glm::mat4 viewProjMatrix;
    alignas(16) glm::mat4 frustumViewProjMatrix;
    alignas(16) glm::vec4 cameraRight;
    alignas(16) glm::vec4 cameraUp;
    alignas(8) glm::vec2 viewportSize;
    alignas(8) glm::vec2 rootOrigin;
    alignas(4) float rootSize;
    alignas(4) float meshletPixelTarget;
    alignas(4) uint32_t maxDepth;
    alignas(4) uint32_t currentDepth;
    alignas(4) uint32_t hashSize;
    alignas(8) VkDeviceAddress workIn;
    alignas(8) VkDeviceAddress workOut;
    alignas(8) VkDeviceAddress leafBuf;
    alignas(8) VkDeviceAddress hashBuf;
    alignas(8) VkDeviceAddress indirect;
};

struct GraphicsPC {
    alignas(16) glm::mat4 viewProjMatrix;
    alignas(8) VkDeviceAddress leafBuf;
    alignas(4) float rootSize;
    alignas(4) uint32_t maxDepth;
    alignas(4) uint32_t edgeSnapEnabled;
    alignas(4) float scale;
    alignas(4) float height;
    alignas(4) float normalDist;
};

static VkResult createDebugUtilsMessengerEXT(const VkInstance p_Instance, const VkDebugUtilsMessengerCreateInfoEXT* p_CreateInfo, const VkAllocationCallbacks* p_Allocator, VkDebugUtilsMessengerEXT* p_DebugMessenger)
{
    const auto l_Func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(p_Instance, "vkCreateDebugUtilsMessengerEXT"));
    if (l_Func != nullptr)
    {
        return l_Func(p_Instance, p_CreateInfo, p_Allocator, p_DebugMessenger);
    }
    return VK_ERROR_EXTENSION_NOT_PRESENT;
}

static void destroyDebugUtilsMessengerEXT(const VkInstance p_Instance, const VkDebugUtilsMessengerEXT p_DebugMessenger, const VkAllocationCallbacks* p_Allocator)
{
    const auto l_Func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(p_Instance, "vkDestroyDebugUtilsMessengerEXT"));
    if (l_Func != nullptr)
    {
        l_Func(p_Instance, p_DebugMessenger, p_Allocator);
    }
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(const VkDebugUtilsMessageSeverityFlagBitsEXT p_MessageSeverity, const VkDebugUtilsMessageTypeFlagsEXT p_MessageType, const VkDebugUtilsMessengerCallbackDataEXT* p_CallbackData, void* p_UserData)
{
    if (p_MessageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
    {
        spdlog::info("validation layer: {}", p_CallbackData->pMessage);
    }
    else if (p_MessageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    {
        spdlog::warn("validation layer: {}", p_CallbackData->pMessage);
    }
    else if (p_MessageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    {
        spdlog::error("validation layer ({}): \n{}", string_VkDebugUtilsMessageTypeFlagsEXT(p_MessageType), p_CallbackData->pMessage);
        assert(!g_AssertOnError);
    }

    return VK_FALSE;
}

static std::optional<VkPhysicalDevice> choosePhysicalDevice(const VkInstance p_Instance, VkSurfaceKHR p_Surface)
{
    std::vector<VkPhysicalDevice> l_PhysicalDevices;
    {
        uint32_t l_PhysicalDeviceCount = 0;
        VULKAN_TRY(vkEnumeratePhysicalDevices(p_Instance, &l_PhysicalDeviceCount, nullptr));
        if (l_PhysicalDeviceCount == 0)
        {
            spdlog::error("Failed to find any physical devices with vulkan support");
            return std::nullopt;
        }
        l_PhysicalDevices.resize(l_PhysicalDeviceCount);
        VULKAN_TRY(vkEnumeratePhysicalDevices(p_Instance, &l_PhysicalDeviceCount, l_PhysicalDevices.data()));
    }

    for (const VkPhysicalDevice& l_PhysicalDevice : l_PhysicalDevices)
    {
        VkPhysicalDeviceProperties l_Properties;
        vkGetPhysicalDeviceProperties(l_PhysicalDevice, &l_Properties);

		spdlog::debug("Evaluating physical device: {} (ID: {})", l_Properties.deviceName, fmt::ptr(l_PhysicalDevice));

		if (l_Properties.apiVersion < VK_API_VERSION_1_3)
		{
			spdlog::debug("- Physical device does not support Vulkan 1.3 (supports {}.{}.{}), skipping", VK_VERSION_MAJOR(l_Properties.apiVersion), VK_VERSION_MINOR(l_Properties.apiVersion), VK_VERSION_PATCH(l_Properties.apiVersion));
			continue;
		}

        uint32_t l_ExtensionCount;
        vkEnumerateDeviceExtensionProperties(l_PhysicalDevice, nullptr, &l_ExtensionCount, nullptr);
		if (l_ExtensionCount == 0)
		{
			spdlog::debug("- Physical device does not support any extensions, skipping", l_Properties.deviceName);
			continue;
		}
		std::vector< VkExtensionProperties> l_AvailableExtensions(l_ExtensionCount);
        vkEnumerateDeviceExtensionProperties(l_PhysicalDevice, nullptr, &l_ExtensionCount, l_AvailableExtensions.data());

		bool l_MeshShaderSupport = false;
        bool l_SwapchainSupport = false;
		bool l_ExtendedDynamicStateSupport = false;
        for (const auto& [extensionName, _] : l_AvailableExtensions)
        {
            if (strcmp(extensionName, VK_EXT_MESH_SHADER_EXTENSION_NAME) == 0)
            {
                l_MeshShaderSupport = true;
            }
			if (strcmp(extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
			{
				l_SwapchainSupport = true;
			}
            if (strcmp(extensionName, VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME) == 0)
            {
                l_ExtendedDynamicStateSupport = true;
            }
			if (l_MeshShaderSupport && l_SwapchainSupport && l_ExtendedDynamicStateSupport)
			{
				break;
			}
        }

		if (!l_MeshShaderSupport)
		{
			spdlog::debug("- Physical device (ID: {}) does not support mesh shader extension, skipping", fmt::ptr(l_PhysicalDevice));
			continue;
		}
		if (!l_SwapchainSupport)
		{
			spdlog::debug("- Physical device (ID: {}) does not support swapchain extension, skipping", fmt::ptr(l_PhysicalDevice));
			continue;
		}
		if (!l_ExtendedDynamicStateSupport)
		{
			spdlog::debug("- Physical device (ID: {}) does not support extended dynamic state extension, skipping", fmt::ptr(l_PhysicalDevice));
            continue;
		}

        VkPhysicalDeviceMeshShaderFeaturesEXT l_MeshFeatures{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT };
        VkPhysicalDeviceVulkan13Features l_Features13{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
        l_Features13.pNext = &l_MeshFeatures;
		VkPhysicalDeviceVulkan12Features l_Features12{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
		l_Features12.pNext = &l_Features13;
		VkPhysicalDeviceExtendedDynamicState3FeaturesEXT l_ExtendedDynamicState3Features{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT };
		l_ExtendedDynamicState3Features.pNext = &l_Features12;

        VkPhysicalDeviceFeatures2 l_Features2{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
        l_Features2.pNext = &l_ExtendedDynamicState3Features;
        vkGetPhysicalDeviceFeatures2(l_PhysicalDevice, &l_Features2);

        bool l_SupportsMeshShaders = l_MeshFeatures.meshShader && l_MeshFeatures.taskShader;
		bool l_SupportsFeatures13 = l_Features13.dynamicRendering && l_Features13.synchronization2;
		bool l_SupportsBufferAddress = l_Features12.bufferDeviceAddress;
		bool l_SupportDynamicPolygonMode = l_ExtendedDynamicState3Features.extendedDynamicState3PolygonMode;
		bool l_SupportNonSolid = l_Features2.features.fillModeNonSolid;
        if (!l_SupportsFeatures13 || !l_SupportsMeshShaders || !l_SupportNonSolid || !l_SupportDynamicPolygonMode || !l_SupportsBufferAddress)
        {
			spdlog::debug("- Physical device (ID: {}) does not support required features (mesh shader support: {}, dynamic rendering support: {}, synchronization2 support: {}, fillModeNonSolid support: {}, dynamic polygon mode support: {}, buffer device address support: {}), skipping", 
                fmt::ptr(l_PhysicalDevice), 
                l_SupportsMeshShaders, 
                l_Features13.dynamicRendering, 
                l_Features13.synchronization2, 
                l_SupportNonSolid, 
                l_SupportDynamicPolygonMode,
                l_SupportsBufferAddress);
	        continue;
        }

		uint32_t l_QueueFamilyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(l_PhysicalDevice, &l_QueueFamilyCount, nullptr);
		std::vector<VkQueueFamilyProperties> l_QueueFamilies(l_QueueFamilyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(l_PhysicalDevice, &l_QueueFamilyCount, l_QueueFamilies.data());
		bool l_SupportsQueue = false;
		for (uint32_t i = 0; i < l_QueueFamilyCount; i++)
		{
			VkBool32 l_PresentSupport = false;
			vkGetPhysicalDeviceSurfaceSupportKHR(l_PhysicalDevice, i, p_Surface, &l_PresentSupport);
			if ((l_QueueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && (l_QueueFamilies[i].queueFlags & VK_QUEUE_TRANSFER_BIT) && l_PresentSupport)
			{
                l_SupportsQueue = true;
                break;
			}
		}

        if (!l_SupportsQueue)
        {
            spdlog::debug("  Physical device (ID: {}) does not support graphics, presentation and transfer on the same queue family, skipping", fmt::ptr(l_PhysicalDevice));
            continue;
        }

        spdlog::debug("Selected physical device: {}", l_Properties.deviceName);
        spdlog::debug("- Type: {}", string_VkPhysicalDeviceType(l_Properties.deviceType));
        spdlog::debug("- API version: {}.{}.{}", VK_VERSION_MAJOR(l_Properties.apiVersion), VK_VERSION_MINOR(l_Properties.apiVersion), VK_VERSION_PATCH(l_Properties.apiVersion));
        spdlog::debug("- VendorID: {}", l_Properties.vendorID);
		return l_PhysicalDevice;
    }
    return std::nullopt;
}

void Engine::querySwapchainProperties()
{
    VkSurfaceFormatKHR l_Format{};

    uint32_t l_FormatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysicalDevice, m_Window.getSurface(), &l_FormatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> l_Formats(l_FormatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysicalDevice, m_Window.getSurface(), &l_FormatCount, l_Formats.data());

    for (const VkSurfaceFormatKHR& l_Candidate : l_Formats)
    {
        if (l_Candidate.format == VK_FORMAT_B8G8R8A8_SRGB && l_Candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            l_Format = l_Candidate;
            break;
        }
    }
    if (l_Format.format == VK_FORMAT_UNDEFINED)
    {
        spdlog::warn("Preferred swapchain format not found, using first available format: {}", string_VkFormat(l_Formats[0].format));
        l_Format = l_Formats[0];
    }

    spdlog::debug("Selected swapchain format: {} with color space {}", string_VkFormat(l_Format.format), string_VkColorSpaceKHR(l_Format.colorSpace));
    m_SwapchainFormat = l_Format;

    VkSurfaceCapabilitiesKHR l_Capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_PhysicalDevice, m_Window.getSurface(), &l_Capabilities);
    uint32_t l_ImageCount = m_FramesInFlight + 1;
    if (l_ImageCount < l_Capabilities.minImageCount)
    {
        spdlog::warn("Frames in flight ({}) is incompatible with minimum supported images by swapchain ({}), increasing frames in flight to {}", m_FramesInFlight, l_Capabilities.minImageCount, l_Capabilities.minImageCount - 1);
        l_ImageCount = l_Capabilities.minImageCount;
    }
    if (l_Capabilities.maxImageCount > 0 && l_ImageCount > l_Capabilities.maxImageCount)
    {
        spdlog::warn("Frames in flight ({}) is incompatible with maximum supported images by swapchain ({}), reducing frames in flight to {}", m_FramesInFlight, l_Capabilities.maxImageCount, l_Capabilities.maxImageCount - 1);
        l_ImageCount = l_Capabilities.maxImageCount;
    }
    m_FramesInFlight = l_ImageCount - 1;
    spdlog::debug("Using {} frames in flight. Swapchain restrictions were min {} images, max {} images", m_FramesInFlight, l_Capabilities.minImageCount, (l_Capabilities.maxImageCount == 0 ? "unlimited" : std::to_string(l_Capabilities.maxImageCount)));
}

void Engine::recreateSwapchain(Window::Size p_Size)
{
    spdlog::info("{}reating swapchain with size {}x{}", m_Swapchain != VK_NULL_HANDLE ? "Rec" : "C", p_Size.width, p_Size.height);

    m_DeviceTable.vkDeviceWaitIdle(m_Device);

    for (const VkImageView& l_ImageView : m_SwapchainImageViews)
    {
		if (l_ImageView != VK_NULL_HANDLE)
		{
			m_DeviceTable.vkDestroyImageView(m_Device, l_ImageView, nullptr);
			spdlog::debug("Destroyed swapchain image view (ID: {})", fmt::ptr(l_ImageView));
		}
    }
    m_SwapchainImageViews.clear();
    m_SwapchainImages.clear();

    const VkExtent2D l_Extent = p_Size.toVkExtent2D();

    VkSurfaceCapabilitiesKHR l_Capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_PhysicalDevice, m_Window.getSurface(), &l_Capabilities);

    VkSwapchainCreateInfoKHR l_SwapchainCreateInfo{};
    l_SwapchainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    l_SwapchainCreateInfo.surface = m_Window.getSurface();
    l_SwapchainCreateInfo.minImageCount = m_FramesInFlight + 1;
    l_SwapchainCreateInfo.imageFormat = m_SwapchainFormat.format;
    l_SwapchainCreateInfo.imageColorSpace = m_SwapchainFormat.colorSpace;
    l_SwapchainCreateInfo.imageExtent = l_Extent;
    l_SwapchainCreateInfo.imageArrayLayers = 1;
    l_SwapchainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    l_SwapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    l_SwapchainCreateInfo.preTransform = l_Capabilities.currentTransform;
    l_SwapchainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    l_SwapchainCreateInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    l_SwapchainCreateInfo.clipped = VK_TRUE;
    l_SwapchainCreateInfo.oldSwapchain = m_Swapchain;

    const VkSwapchainKHR l_OldSwapchain = m_Swapchain;
    VULKAN_TRY(m_DeviceTable.vkCreateSwapchainKHR(m_Device, &l_SwapchainCreateInfo, nullptr, &m_Swapchain));
    spdlog::debug("Created swapchain (ID: {}) with format {} and color space {}", fmt::ptr(m_Swapchain), string_VkFormat(m_SwapchainFormat.format), string_VkColorSpaceKHR(m_SwapchainFormat.colorSpace));

    if (l_OldSwapchain != VK_NULL_HANDLE)
    {
        m_DeviceTable.vkDestroySwapchainKHR(m_Device, l_OldSwapchain, nullptr);
        spdlog::debug("Destroyed old swapchain (ID: {})", fmt::ptr(l_OldSwapchain));
    }

    uint32_t l_ImageCount;
    VULKAN_TRY(m_DeviceTable.vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &l_ImageCount, nullptr));

	m_SwapchainExtent = l_Extent;

    m_SwapchainImages.resize(l_ImageCount);
	m_SwapchainImageViews.resize(l_ImageCount);
    VULKAN_TRY(m_DeviceTable.vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &l_ImageCount, m_SwapchainImages.data()));
    for (uint32_t l_Index = 0; l_Index < l_ImageCount; l_Index++)
    {
		VkImageViewCreateInfo l_ImageViewCreateInfo{.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		l_ImageViewCreateInfo.image = m_SwapchainImages[l_Index];
		l_ImageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		l_ImageViewCreateInfo.format = m_SwapchainFormat.format;
		l_ImageViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		l_ImageViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		l_ImageViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		l_ImageViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		l_ImageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		l_ImageViewCreateInfo.subresourceRange.baseMipLevel = 0;
		l_ImageViewCreateInfo.subresourceRange.levelCount = 1;
		l_ImageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
		l_ImageViewCreateInfo.subresourceRange.layerCount = 1;
		VULKAN_TRY(m_DeviceTable.vkCreateImageView(m_Device, &l_ImageViewCreateInfo, nullptr, &m_SwapchainImageViews[l_Index]));
		spdlog::debug("Created image view (ID: {}) for swapchain image (ID: {})", fmt::ptr(m_SwapchainImageViews[l_Index]), fmt::ptr(m_SwapchainImages[l_Index]));
    }

    destroyDepthResources();
    createDepthResources(l_Extent);
}

uint32_t Engine::findMemoryType(const uint32_t p_TypeFilter, const VkMemoryPropertyFlags p_Properties) const
{
    VkPhysicalDeviceMemoryProperties l_MemProps;
    vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &l_MemProps);
    for (uint32_t i = 0; i < l_MemProps.memoryTypeCount; ++i)
    {
        if ((p_TypeFilter & (1u << i)) && (l_MemProps.memoryTypes[i].propertyFlags & p_Properties) == p_Properties)
        {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable memory type for depth image");
}

void Engine::createDepthResources(const VkExtent2D p_Extent)
{
    const VkImageCreateInfo l_ImageInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = s_DepthFormat,
        .extent = {.width = p_Extent.width, .height = p_Extent.height, .depth = 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };
    VULKAN_TRY(m_DeviceTable.vkCreateImage(m_Device, &l_ImageInfo, nullptr, &m_DepthImage));

    VkMemoryRequirements l_MemReqs;
    m_DeviceTable.vkGetImageMemoryRequirements(m_Device, m_DepthImage, &l_MemReqs);

    VkMemoryAllocateInfo l_AllocInfo{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = l_MemReqs.size,
        .memoryTypeIndex = findMemoryType(l_MemReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    };
    VULKAN_TRY(m_DeviceTable.vkAllocateMemory(m_Device, &l_AllocInfo, nullptr, &m_DepthMemory));
    VULKAN_TRY(m_DeviceTable.vkBindImageMemory(m_Device, m_DepthImage, m_DepthMemory, 0));

    VkImageViewCreateInfo l_ViewInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = m_DepthImage,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = s_DepthFormat,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };
    VULKAN_TRY(m_DeviceTable.vkCreateImageView(m_Device, &l_ViewInfo, nullptr, &m_DepthImageView));
    spdlog::debug("Created depth image (ID: {}) and view (ID: {})", fmt::ptr(m_DepthImage), fmt::ptr(m_DepthImageView));
}

Engine::StorageBuffer Engine::createStorageBuffer(const VkDeviceSize p_Size, const VkBufferUsageFlags p_ExtraUsage) const
{
	StorageBuffer l_Out{};
    l_Out.size = p_Size;
    const VkBufferCreateInfo l_BufInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = p_Size,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | p_ExtraUsage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    VULKAN_TRY(m_DeviceTable.vkCreateBuffer(m_Device, &l_BufInfo, nullptr, &l_Out.buffer));

    VkMemoryRequirements l_Reqs;
    m_DeviceTable.vkGetBufferMemoryRequirements(m_Device, l_Out.buffer, &l_Reqs);

    VkMemoryAllocateFlagsInfo l_AllocFlags{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
        .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT
    };
    const VkMemoryAllocateInfo l_AllocInfo{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &l_AllocFlags,
        .allocationSize = l_Reqs.size,
        .memoryTypeIndex = findMemoryType(l_Reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    };
    VULKAN_TRY(m_DeviceTable.vkAllocateMemory(m_Device, &l_AllocInfo, nullptr, &l_Out.memory));
    VULKAN_TRY(m_DeviceTable.vkBindBufferMemory(m_Device, l_Out.buffer, l_Out.memory, 0));

    const VkBufferDeviceAddressInfo l_AddrInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = l_Out.buffer
    };
    l_Out.address = m_DeviceTable.vkGetBufferDeviceAddress(m_Device, &l_AddrInfo);
    spdlog::debug("Created storage buffer (ID: {}, size: {} bytes, addr: 0x{:x})", fmt::ptr(l_Out.buffer), l_Out.size, l_Out.address);
    return l_Out;
}

VkShaderModule Engine::createShaderModule(const std::vector<uint32_t>& p_Spirv) const
{
    const VkShaderModuleCreateInfo l_Info{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = p_Spirv.size() * sizeof(uint32_t),
        .pCode = p_Spirv.data()
    };
    VkShaderModule l_Module;
    VULKAN_TRY(m_DeviceTable.vkCreateShaderModule(m_Device, &l_Info, nullptr, &l_Module));
	spdlog::debug("Created shader module (ID: {}) with size {} bytes", fmt::ptr(l_Module), l_Info.codeSize);
    return l_Module;
}

VkPipeline Engine::createComputePipeline(const VkShaderModule p_Module, const VkPipelineLayout p_Layout) const
{
    const VkPipelineShaderStageCreateInfo l_Stage{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = p_Module,
        .pName = "main"
    };
    const VkComputePipelineCreateInfo l_Info{
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = l_Stage,
        .layout = p_Layout
    };
    VkPipeline l_Pipeline;
    VULKAN_TRY(m_DeviceTable.vkCreateComputePipelines(m_Device, VK_NULL_HANDLE, 1, &l_Info, nullptr, &l_Pipeline));
    spdlog::debug("Created compute pipeline (ID: {})", fmt::ptr(l_Pipeline));
    return l_Pipeline;
}

void Engine::initImgui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    const float l_MainScale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    ImGuiStyle& l_Style = ImGui::GetStyle();
    l_Style.ScaleAllSizes(l_MainScale);
    l_Style.FontScaleDpi = l_MainScale;

    VkDescriptorPoolSize l_PoolSizes[] = {
    	{.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE},
		{.type = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = IMGUI_IMPL_VULKAN_MINIMUM_SAMPLER_POOL_SIZE},
    };

    VkDescriptorPoolCreateInfo l_PoolInfo{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    l_PoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    l_PoolInfo.maxSets = 0;
    for (const VkDescriptorPoolSize& l_PoolSize : l_PoolSizes)
        l_PoolInfo.maxSets += l_PoolSize.descriptorCount;
    l_PoolInfo.poolSizeCount = static_cast<uint32_t>(IM_COUNTOF(l_PoolSizes));
    l_PoolInfo.pPoolSizes = l_PoolSizes;
    VULKAN_TRY(vkCreateDescriptorPool(m_Device, &l_PoolInfo, nullptr, &m_ImguiDescriptorPool));

    VkPipelineRenderingCreateInfo l_PipelineRenderingInfo{ .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    l_PipelineRenderingInfo.pNext = nullptr;
    l_PipelineRenderingInfo.colorAttachmentCount = 1;
    l_PipelineRenderingInfo.pColorAttachmentFormats = &m_SwapchainFormat.format;
    l_PipelineRenderingInfo.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
    l_PipelineRenderingInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

    ImGui_ImplSDL3_InitForVulkan(*m_Window);
    ImGui_ImplVulkan_InitInfo l_InitInfo{};
    l_InitInfo.Instance = m_Instance;
    l_InitInfo.PhysicalDevice = m_PhysicalDevice;
    l_InitInfo.Device = m_Device;
    l_InitInfo.QueueFamily = m_QueueFamilyIndex;
    l_InitInfo.Queue = m_Queue;
    l_InitInfo.PipelineCache = VK_NULL_HANDLE;
    l_InitInfo.DescriptorPool = m_ImguiDescriptorPool;
    l_InitInfo.MinImageCount = 2;
    l_InitInfo.ImageCount = m_FramesInFlight + 1;
    l_InitInfo.Allocator = VK_NULL_HANDLE;
    l_InitInfo.UseDynamicRendering = true;
    l_InitInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    l_InitInfo.PipelineInfoMain.PipelineRenderingCreateInfo = l_PipelineRenderingInfo;
    ImGui_ImplVulkan_Init(&l_InitInfo);
}

void Engine::init(const bool p_DebugEnabled)
{
    try
    {
        m_Window.init(Window::Size{ .width = 1920, .height = 1080 }, "Infinite Landscapes", true);
    }
	catch (const std::exception& e)
	{
		spdlog::error("Failed to initialize window: {}", e.what());
        throw std::runtime_error("Failed to initialize window");
	}

    if (volkInitialize() != VK_SUCCESS)
    {
		spdlog::error("Failed to initialize volk");
        throw std::runtime_error("Failed to initialize volk");
    }
	spdlog::info("Initialized volk");

    //Initialize instance
    {
        std::span<const char* const> l_ExtensionArray;
        try
        {
            l_ExtensionArray = m_Window.getRequiredInstanceExtensions();
        }
		catch (const std::exception& e)
		{
			spdlog::error("Failed to get required instance extensions: {}", e.what());
			throw std::runtime_error("Failed to get required instance extensions");
		}
        std::vector<const char*> l_Extensions(l_ExtensionArray.begin(), l_ExtensionArray.end());

		VkInstanceCreateInfo l_InstanceCreateInfo{ .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
        l_InstanceCreateInfo.flags = 0;

        const char* l_Layers[] = { "VK_LAYER_KHRONOS_validation" };
        VkDebugUtilsMessengerCreateInfoEXT l_DebugCreateInfo{ .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
    	if (p_DebugEnabled)
        {
            l_Extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

            l_InstanceCreateInfo.enabledLayerCount = 1;
            l_InstanceCreateInfo.ppEnabledLayerNames = l_Layers;

            l_DebugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            l_DebugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            l_DebugCreateInfo.pfnUserCallback = debugCallback;
            l_InstanceCreateInfo.pNext = &l_DebugCreateInfo;
        }

        l_InstanceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(l_Extensions.size());
        l_InstanceCreateInfo.ppEnabledExtensionNames = l_Extensions.data();

        VkApplicationInfo l_AppInfo{ .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO };
        l_AppInfo.apiVersion = VK_API_VERSION_1_3;
        l_AppInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
        l_AppInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
        l_AppInfo.pApplicationName = "Infinite Landscapes";
        l_AppInfo.pEngineName = "No Engine";
		l_InstanceCreateInfo.pApplicationInfo = &l_AppInfo;

        VULKAN_TRY(vkCreateInstance(&l_InstanceCreateInfo, nullptr, &m_Instance));
		spdlog::info("Created Vulkan instance (ID: {})", fmt::ptr(m_Instance));

        volkLoadInstance(m_Instance);
        spdlog::info("Loaded instance functions with volk");

        m_Window.createSurface(m_Instance);
    }

	// Initialize debug messenger
    if (p_DebugEnabled)
	{
		VkDebugUtilsMessengerCreateInfoEXT l_CreateInfo{ .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
        l_CreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		if (spdlog::get_level() <= spdlog::level::trace)
		{
			l_CreateInfo.messageSeverity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT;
            l_CreateInfo.messageSeverity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
		}
    	l_CreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        l_CreateInfo.pfnUserCallback = debugCallback;

        VULKAN_TRY(createDebugUtilsMessengerEXT(m_Instance, &l_CreateInfo, nullptr, &m_DebugMessenger));
		spdlog::info("Created debug messenger (ID: {}) for instance (ID: {})", fmt::ptr(m_DebugMessenger), fmt::ptr(m_Instance));
    }

	// Initialize physical and logical device
    {
	    std::optional<VkPhysicalDevice> l_PhysicalDevice = choosePhysicalDevice(m_Instance, m_Window.getSurface());
		if (!l_PhysicalDevice)
		{
			spdlog::error("Failed to find a suitable physical device");
			throw std::runtime_error("Failed to find a suitable physical device");
		}

		m_PhysicalDevice = *l_PhysicalDevice;

		VkDeviceCreateInfo l_DeviceCreateInfo{ .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
		uint32_t l_QueueFamilyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &l_QueueFamilyCount, nullptr);
		std::vector<VkQueueFamilyProperties> l_QueueFamilies(l_QueueFamilyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &l_QueueFamilyCount, l_QueueFamilies.data());

        VkDeviceQueueCreateInfo l_QueueCreateInfo{};
        l_DeviceCreateInfo.queueCreateInfoCount = 0;
		for (uint32_t i = 0; i < l_QueueFamilyCount; i++)
		{
			VkBool32 l_PresentSupport = false;
			vkGetPhysicalDeviceSurfaceSupportKHR(m_PhysicalDevice, i, m_Window.getSurface(), &l_PresentSupport);
			if ((l_QueueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && (l_QueueFamilies[i].queueFlags & VK_QUEUE_TRANSFER_BIT) && l_PresentSupport)
			{
				float l_QueuePriority = 1.0f;
				l_QueueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
                l_QueueCreateInfo.queueFamilyIndex = i;
                l_QueueCreateInfo.queueCount = 1;
                l_QueueCreateInfo.pQueuePriorities = &l_QueuePriority;
				l_DeviceCreateInfo.queueCreateInfoCount += 1;
				spdlog::debug("Selected queue family {} for graphics, presentation and transfer", i);
                break;
			}
		}
		m_QueueFamilyIndex = l_QueueCreateInfo.queueFamilyIndex;
		l_DeviceCreateInfo.pQueueCreateInfos = &l_QueueCreateInfo;

		std::array<const char*, 3> l_DeviceExtensions = { VK_EXT_MESH_SHADER_EXTENSION_NAME, VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME };
		l_DeviceCreateInfo.ppEnabledExtensionNames = l_DeviceExtensions.data();
        l_DeviceCreateInfo.enabledExtensionCount = 3;

        VkPhysicalDeviceMeshShaderFeaturesEXT l_MeshFeatures{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT };
        l_MeshFeatures.taskShader = VK_TRUE;
        l_MeshFeatures.meshShader = VK_TRUE;
        VkPhysicalDeviceVulkan12Features l_Features12{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
        l_Features12.bufferDeviceAddress = VK_TRUE;
        l_Features12.pNext = &l_MeshFeatures;
        VkPhysicalDeviceVulkan13Features l_Features13{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
        l_Features13.dynamicRendering = VK_TRUE;
		l_Features13.synchronization2 = VK_TRUE;
        l_Features13.pNext = &l_Features12;
		VkPhysicalDeviceExtendedDynamicState3FeaturesEXT l_ExtendedDynamicState3Features{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT };
		l_ExtendedDynamicState3Features.extendedDynamicState3PolygonMode = VK_TRUE;
		l_ExtendedDynamicState3Features.pNext = &l_Features13;
        VkPhysicalDeviceFeatures2 l_DeviceFeatures{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
        l_DeviceFeatures.features.fillModeNonSolid = VK_TRUE;
        l_DeviceFeatures.pNext = &l_ExtendedDynamicState3Features;

        l_DeviceCreateInfo.pNext = &l_DeviceFeatures;

		VULKAN_TRY(vkCreateDevice(m_PhysicalDevice, &l_DeviceCreateInfo, nullptr, &m_Device));
		spdlog::info("Created logical device (ID: {}) from physical device (ID: {})", fmt::ptr(m_Device), fmt::ptr(m_PhysicalDevice));
    
		volkLoadDevice(m_Device);
		volkLoadDeviceTable(&m_DeviceTable, m_Device);
		spdlog::info("Loaded device functions with volk");

        vkGetDeviceQueue(m_Device, m_QueueFamilyIndex, 0, &m_Queue);
		spdlog::info("Retrieved queue (ID: {}) from device (ID: {}) with queue family index {}", fmt::ptr(m_Queue), fmt::ptr(m_Device), m_QueueFamilyIndex);
    }

	// Initialize swapchain
    {
        querySwapchainProperties();

        m_Window.getOnPixelResize().connect(this, &Engine::recreateSwapchain);

        m_RenderFinishedSemaphores.resize(m_FramesInFlight + 1);
        for (uint32_t i = 0; i < m_FramesInFlight + 1; i++)
        {
            VkSemaphoreCreateInfo l_SemaphoreCreateInfo{};
            l_SemaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            VULKAN_TRY(m_DeviceTable.vkCreateSemaphore(m_Device, &l_SemaphoreCreateInfo, nullptr, &m_RenderFinishedSemaphores[i]));
            spdlog::info("Created semaphore (ID: {}) for swapchain image {}", fmt::ptr(m_RenderFinishedSemaphores[i]), i);
        }
    }

    // Initialize frame objects
    {
        m_Frames.resize(m_FramesInFlight);
		for (FrameData& l_Frame : m_Frames)
		{
			VkCommandPoolCreateInfo l_CommandPoolCreateInfo{ .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
			l_CommandPoolCreateInfo.queueFamilyIndex = m_QueueFamilyIndex;
			l_CommandPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
			VULKAN_TRY(m_DeviceTable.vkCreateCommandPool(m_Device, &l_CommandPoolCreateInfo, nullptr, &l_Frame.commandPool));
			spdlog::info("Created command pool (ID: {}) for frame", fmt::ptr(l_Frame.commandPool));

			VkCommandBufferAllocateInfo l_CommandBufferAllocateInfo{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
			l_CommandBufferAllocateInfo.commandPool = l_Frame.commandPool;
			l_CommandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			l_CommandBufferAllocateInfo.commandBufferCount = 1;
			VULKAN_TRY(m_DeviceTable.vkAllocateCommandBuffers(m_Device, &l_CommandBufferAllocateInfo, &l_Frame.commandBuffer));
			spdlog::info("Allocated command buffer (ID: {}) from command pool (ID: {}) for frame", fmt::ptr(l_Frame.commandBuffer), fmt::ptr(l_Frame.commandPool));
			
			VkSemaphoreCreateInfo l_SemaphoreCreateInfo{};
			l_SemaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
			VULKAN_TRY(m_DeviceTable.vkCreateSemaphore(m_Device, &l_SemaphoreCreateInfo, nullptr, &l_Frame.imageAvailableSemaphore));
			spdlog::info("Created semaphore (ID: {}) for frame image availability", fmt::ptr(l_Frame.imageAvailableSemaphore));
			
			VkFenceCreateInfo l_FenceCreateInfo{ .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
			l_FenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
			VULKAN_TRY(m_DeviceTable.vkCreateFence(m_Device, &l_FenceCreateInfo, nullptr, &l_Frame.inFlightFence));
			spdlog::info("Created fence (ID: {}) for frame in-flight synchronization", fmt::ptr(l_Frame.inFlightFence));

			constexpr VkDeviceSize l_NodeBufSize = 16 + s_MaxNodes  * 16;
			constexpr VkDeviceSize l_LeafBufSize = 16 + s_MaxLeaves * 16;
			constexpr VkDeviceSize l_HashBufSize = s_HashSize * 16;
			constexpr VkDeviceSize l_IndirectSize = sizeof(uint32_t) * 3;
			l_Frame.nodeBufferA = createStorageBuffer(l_NodeBufSize, 0);
			l_Frame.nodeBufferB = createStorageBuffer(l_NodeBufSize, 0);
            l_Frame.leafBuffer = createStorageBuffer(l_LeafBufSize, 0);
			l_Frame.hashBuffer = createStorageBuffer(l_HashBufSize, 0);
			l_Frame.indirectBuffer = createStorageBuffer(l_IndirectSize, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
		}
    }

    // Pipeline
    {
		{
            VkPushConstantRange l_PushConstantRange{
			    .stageFlags = VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT,
			    .offset = 0,
			    .size = sizeof(GraphicsPC)
            };

            VkPipelineLayoutCreateInfo l_LayoutInfo{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .pushConstantRangeCount = 1,
                .pPushConstantRanges = &l_PushConstantRange
            };
            VULKAN_TRY(vkCreatePipelineLayout(m_Device, &l_LayoutInfo, nullptr, &m_GraphicsPipelineLayout));
			spdlog::info("Created graphics pipeline layout (ID: {})", fmt::ptr(m_GraphicsPipelineLayout));
		}

        std::vector<uint32_t> l_TaskSpirv;
        std::vector<uint32_t> l_MeshSpirv;
    	std::vector<uint32_t> l_FragSpirv;
        std::array<const char*, 4> l_Modules = { "mesh", "compute", "generation", "noise" };
        Shader l_ShaderBundle("src/shaders", l_Modules);
        try
        {
			l_TaskSpirv = l_ShaderBundle.getSpirv("taskMain", "mesh");
            l_MeshSpirv = l_ShaderBundle.getSpirv("meshMain", "mesh");
            l_FragSpirv = l_ShaderBundle.getSpirv("fragmentMain", "mesh");
			spdlog::info("Loaded shader SPIR-V for task, mesh, and fragment stages from shader bundle");
        }
		catch (const std::exception& e)
		{
			spdlog::error("Failed to load shader: {}", e.what());
			throw std::runtime_error("Failed to load shader");
		}

		VkShaderModuleCreateInfo l_TaskCreateInfo{
			.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
			.codeSize = l_TaskSpirv.size() * sizeof(uint32_t),
			.pCode = l_TaskSpirv.data()
		};
		VkShaderModule l_TaskModule;
		vkCreateShaderModule(m_Device, &l_TaskCreateInfo, nullptr, &l_TaskModule);

    	VkShaderModuleCreateInfo l_MeshCreateInfo{
		    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		    .codeSize = l_MeshSpirv.size() * sizeof(uint32_t),
		    .pCode = l_MeshSpirv.data()
        };
        VkShaderModule l_MeshModule;
        vkCreateShaderModule(m_Device, &l_MeshCreateInfo, nullptr, &l_MeshModule);

		VkShaderModuleCreateInfo l_FragCreateInfo{
			.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
			.codeSize = l_FragSpirv.size() * sizeof(uint32_t),
			.pCode = l_FragSpirv.data()
		};
		VkShaderModule l_FragModule;
		vkCreateShaderModule(m_Device, &l_FragCreateInfo, nullptr, &l_FragModule);

        std::array<VkPipelineShaderStageCreateInfo, 3> l_ShaderStages{};
        l_ShaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        l_ShaderStages[0].stage = VK_SHADER_STAGE_TASK_BIT_EXT;
        l_ShaderStages[0].module = l_TaskModule;
        l_ShaderStages[0].pName = "main";

        l_ShaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        l_ShaderStages[1].stage = VK_SHADER_STAGE_MESH_BIT_EXT;
        l_ShaderStages[1].module = l_MeshModule;
        l_ShaderStages[1].pName = "main";
        
        l_ShaderStages[2].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        l_ShaderStages[2].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        l_ShaderStages[2].module = l_FragModule;
        l_ShaderStages[2].pName = "main";

        VkPipelineRenderingCreateInfo l_RenderingCreateInfo{
		    .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
		    .colorAttachmentCount = 1,
		    .pColorAttachmentFormats = &m_SwapchainFormat.format,
            .depthAttachmentFormat = s_DepthFormat
        };

        VkPipelineDepthStencilStateCreateInfo l_DepthStencilState{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable = VK_TRUE,
            .depthWriteEnable = VK_TRUE,
            .depthCompareOp = VK_COMPARE_OP_LESS,
            .depthBoundsTestEnable = VK_FALSE,
            .stencilTestEnable = VK_FALSE
        };

		VkPipelineViewportStateCreateInfo l_ViewportState{ .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
		l_ViewportState.scissorCount = 1;
		l_ViewportState.viewportCount = 1;

        VkPipelineRasterizationStateCreateInfo l_RasterizationState{ .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        l_RasterizationState.depthClampEnable = VK_FALSE;
        l_RasterizationState.rasterizerDiscardEnable = VK_FALSE;
        l_RasterizationState.lineWidth = 1.0f;
        l_RasterizationState.cullMode = VK_CULL_MODE_NONE;
        l_RasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        l_RasterizationState.depthBiasEnable = VK_FALSE;

		VkPipelineMultisampleStateCreateInfo l_MultisampleState{ .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
        l_MultisampleState.sampleShadingEnable = VK_FALSE;
        l_MultisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState l_ColorBlendAttachment{};
        l_ColorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        l_ColorBlendAttachment.blendEnable = VK_FALSE; // No alpha blending for a basic setup

        VkPipelineColorBlendStateCreateInfo l_ColorBlendState{ .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        l_ColorBlendState.logicOpEnable = VK_FALSE;
        l_ColorBlendState.attachmentCount = 1;
        l_ColorBlendState.pAttachments = &l_ColorBlendAttachment;

        std::array<VkDynamicState, 3> l_DynamicStates = {
		    VK_DYNAMIC_STATE_VIEWPORT,
		    VK_DYNAMIC_STATE_SCISSOR,
            VK_DYNAMIC_STATE_POLYGON_MODE_EXT
        };

        VkPipelineDynamicStateCreateInfo l_DynamicState{ .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
        l_DynamicState.dynamicStateCount = static_cast<uint32_t>(l_DynamicStates.size());
        l_DynamicState.pDynamicStates = l_DynamicStates.data();

		VkGraphicsPipelineCreateInfo l_PipelineCreateInfo{ .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        l_PipelineCreateInfo.pNext = &l_RenderingCreateInfo;
        l_PipelineCreateInfo.stageCount = static_cast<uint32_t>(l_ShaderStages.size());
        l_PipelineCreateInfo.pStages = l_ShaderStages.data();
        
        l_PipelineCreateInfo.pVertexInputState = nullptr;
        l_PipelineCreateInfo.pInputAssemblyState = nullptr;
        
	    l_PipelineCreateInfo.pViewportState = &l_ViewportState;
	    l_PipelineCreateInfo.pRasterizationState = &l_RasterizationState;
	    l_PipelineCreateInfo.pMultisampleState = &l_MultisampleState;
	    l_PipelineCreateInfo.pColorBlendState = &l_ColorBlendState;
		l_PipelineCreateInfo.pDepthStencilState = &l_DepthStencilState;
		l_PipelineCreateInfo.pDynamicState = &l_DynamicState;
        
	    l_PipelineCreateInfo.layout = m_GraphicsPipelineLayout;
        l_PipelineCreateInfo.renderPass = VK_NULL_HANDLE;

        VULKAN_TRY(vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &l_PipelineCreateInfo, nullptr, &m_MeshPipeline));

        vkDestroyShaderModule(m_Device, l_TaskModule, nullptr);
		spdlog::debug("Destroyed shader module (ID: {}) for task shader stage", fmt::ptr(l_TaskModule));
		vkDestroyShaderModule(m_Device, l_MeshModule, nullptr);
		spdlog::debug("Destroyed shader module (ID: {}) for mesh shader stage", fmt::ptr(l_MeshModule));
		vkDestroyShaderModule(m_Device, l_FragModule, nullptr);
		spdlog::debug("Destroyed shader module (ID: {}) for fragment shader stage", fmt::ptr(l_FragModule));

		spdlog::info("Created graphics pipeline (ID: {}) with mesh shader and fragment shader", fmt::ptr(m_MeshPipeline));
    }

    // Compute pipelines
    {
        VkPushConstantRange l_PushRange{
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .offset = 0,
            .size = sizeof(ComputePC)
        };
        VkPipelineLayoutCreateInfo l_LayoutInfo{ .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        l_LayoutInfo.pushConstantRangeCount = 1;
    	l_LayoutInfo.pPushConstantRanges = &l_PushRange;

        VULKAN_TRY(vkCreatePipelineLayout(m_Device, &l_LayoutInfo, nullptr, &m_ComputePipelineLayout));
        spdlog::info("Created compute pipeline layout (ID: {})", fmt::ptr(m_ComputePipelineLayout));

		std::array<const char*, 1> l_Modules = { "compute" };
        Shader l_ComputeBundle("src/shaders", l_Modules);
        VkShaderModule l_RefineMod = createShaderModule(l_ComputeBundle.getSpirv("refineMain", "compute"));
        VkShaderModule l_PrepareIndirectMod = createShaderModule(l_ComputeBundle.getSpirv("prepareIndirectMain", "compute"));
        VkShaderModule l_HashBuildMod = createShaderModule(l_ComputeBundle.getSpirv("hashBuildMain", "compute"));
        VkShaderModule l_NeighborLookupMod = createShaderModule(l_ComputeBundle.getSpirv("neighborLookupMain", "compute"));

        m_RefinePipeline = createComputePipeline(l_RefineMod, m_ComputePipelineLayout);
        m_PrepareIndirectPipeline = createComputePipeline(l_PrepareIndirectMod, m_ComputePipelineLayout);
        m_HashBuildPipeline = createComputePipeline(l_HashBuildMod, m_ComputePipelineLayout);
        m_NeighborLookupPipeline = createComputePipeline(l_NeighborLookupMod, m_ComputePipelineLayout);

        vkDestroyShaderModule(m_Device, l_RefineMod, nullptr);
        vkDestroyShaderModule(m_Device, l_PrepareIndirectMod, nullptr);
        vkDestroyShaderModule(m_Device, l_HashBuildMod, nullptr);
        vkDestroyShaderModule(m_Device, l_NeighborLookupMod, nullptr);

        spdlog::info("Created 4 compute pipelines for quadtree refinement");
    }

    // Imgui
    {
        initImgui();
        spdlog::info("Initialized ImGui for Vulkan");
    }

    {
        m_Window.getOnMouseMoved().connect(&m_Camera, &Camera::mouseMoved);
        m_Window.getOnKeyPressed().connect(&m_Camera, &Camera::keyPressed);
        m_Window.getOnKeyReleased().connect(&m_Camera, &Camera::keyReleased);
        m_Window.getOnEventsProcessed().connect(&m_Camera, &Camera::updateEvents);
        m_Window.getOnMouseCaptureChanged().connect(&m_Camera, &Camera::setMouseCaptured);
        m_Window.getOnResize().connect(this, &Engine::recreateSwapchain);
        m_Window.getOnMouseScrolled().connect(&m_Camera, &Camera::mouseScrolled);

        m_Window.getOnKeyPressed().connect([this](const uint32_t p_Key)
        {
            if (p_Key == SDLK_O)
                toggleImgui();
			else if (p_Key == SDLK_Q)
				m_Window.toggleMouseCaptured();
        });
    }
}

void Engine::imguiDraw(const uint32_t p_FrameIndex)
{
	if (!m_ImguiActive)
		return;

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    // ImGui::ShowDemoWindow();

	{
    	ImGui::Begin("Performance Metrics", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    	const ImGuiIO& l_Io = ImGui::GetIO();
    	ImGui::Text("Application average: %.3f ms/frame", 1000.0f / l_Io.Framerate);
    	ImGui::Text("Frames Per Second: %.1f FPS", l_Io.Framerate);
    	ImGui::Separator();

    	static float l_FrameTimeHistory[50] = { 0.0f };
    	static int l_Slot = 0;
    	l_FrameTimeHistory[l_Slot] = 1000.0f / l_Io.Framerate;
    	l_Slot = (l_Slot + 1) % 50;

    	ImGui::PlotLines("Frame Time", l_FrameTimeHistory, 50, l_Slot, nullptr, 0.0f, 33.3f, ImVec2(0, 40));
    	ImGui::End();
	}

    {
        ImGui::Begin("Controls");

        ImGui::SliderFloat("Root Size", &m_ImguiRootSize, 512.0f, 32768.0f, "%.0f m");
        int l_MaxDepth = static_cast<int>(m_ImguiMaxDepth);
        if (ImGui::SliderInt("Max Depth", &l_MaxDepth, 4, 10))
        {
            m_ImguiMaxDepth = static_cast<uint32_t>(l_MaxDepth);
        }
        ImGui::Text("Smallest leaf: %.2f m", m_ImguiRootSize / static_cast<float>(1u << m_ImguiMaxDepth));
        ImGui::SliderFloat("Meshlet Pixel Target", &m_ImguiMeshletPixelTarget, 8.0f, 256.0f, "%.0f px");

		ImGui::Checkbox("Wireframe Mode", &m_ImguiWireframe);
        ImGui::Checkbox("Edge Snap", &m_ImguiEdgeSnap);

        bool l_FreezeFrustum = m_Camera.isFrustumFrozen();
        if (ImGui::Checkbox("Freeze Frustum", &l_FreezeFrustum))
        {
            m_Camera.setFreezeFrustum(l_FreezeFrustum);
        }

        ImGui::Separator();
		ImGui::DragFloat("Noise Scale", &m_ImguiScale, 1.f, 1.0f, 1000.f, "%.3f");
        ImGui::DragFloat("Noise Height", &m_ImguiHeight, 10.f, 10.0f, 1000.f, "%.3f");
        ImGui::DragFloat("Normal Distance", &m_ImguiNormalDist, 0.001f, 0.001f, 5.f, "%.3f");

		ImGui::End();
    }

	{
        ImGui::Begin("Camera data");
		ImGui::Text("Position: (%.2f, %.2f, %.2f)", m_Camera.getPosition().x, m_Camera.getPosition().y, m_Camera.getPosition().z);
		ImGui::Text("Direction: (%.2f, %.2f, %.2f)", m_Camera.getDir().x, m_Camera.getDir().y, m_Camera.getDir().z);
		ImGui::End();
	}

    ImGui::Render();
    ImDrawData* l_DrawData = ImGui::GetDrawData();
    ImGui_ImplVulkan_RenderDrawData(l_DrawData, m_Frames[p_FrameIndex].commandBuffer);
}

void Engine::toggleImgui()
{
	m_ImguiActive = !m_ImguiActive;
}

struct RootInit {
	uint32_t count;
	uint32_t pad0[3];
	float originX;
	float originZ;
	uint32_t depth;
	uint32_t pad1;
};

void Engine::run()
{
	while (!m_Window.shouldClose())
	{
		m_Window.pollEvents();
        if (m_Window.isMinimized())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        const uint32_t l_FrameIndex = m_CurrentFrameIndex % m_FramesInFlight;

        FrameData& l_Frame = m_Frames[l_FrameIndex];
        VULKAN_TRY(vkWaitForFences(m_Device, 1, &l_Frame.inFlightFence, VK_TRUE, UINT64_MAX));
        VULKAN_TRY(vkResetFences(m_Device, 1, &l_Frame.inFlightFence));

        uint32_t l_ImageIndex;
        VkResult l_SwapRes = vkAcquireNextImageKHR(m_Device, m_Swapchain, UINT64_MAX, l_Frame.imageAvailableSemaphore, VK_NULL_HANDLE, &l_ImageIndex);
        if (l_SwapRes == VK_ERROR_OUT_OF_DATE_KHR || l_SwapRes == VK_SUBOPTIMAL_KHR)
        {
            continue;
        }
        if (l_SwapRes != VK_SUCCESS)
        {
            spdlog::error("Failed to acquire swapchain image: {}", string_VkResult(l_SwapRes));
            throw std::runtime_error("Failed to acquire swapchain image");
        }

		{
            vkResetCommandPool(m_Device, l_Frame.commandPool, 0);
            VkCommandBufferBeginInfo l_BeginInfo{};
            l_BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            l_BeginInfo.flags = 0;
            vkBeginCommandBuffer(l_Frame.commandBuffer, &l_BeginInfo);

            const float l_SmallestLeaf = m_ImguiRootSize / static_cast<float>(1u << m_ImguiMaxDepth);

            constexpr uint32_t kStabilityLevels = 4;
            const float l_SnapStep = l_SmallestLeaf * static_cast<float>(1u << kStabilityLevels);
            const glm::vec3 l_CamPos = m_Camera.getPosition();
            const glm::vec2 l_CamXZ(l_CamPos.x, l_CamPos.z);
            const glm::vec2 l_RootOrigin = glm::floor(l_CamXZ / l_SnapStep) * l_SnapStep - glm::vec2(m_ImguiRootSize * 0.5f);

            RootInit l_RootInit{
	            .count = 1u,
	            .originX = l_RootOrigin.x,
	            .originZ = l_RootOrigin.y,
	            .depth = 0u
            };
            vkCmdUpdateBuffer(l_Frame.commandBuffer, l_Frame.nodeBufferA.buffer, 0, sizeof(l_RootInit), &l_RootInit);
            vkCmdFillBuffer(l_Frame.commandBuffer, l_Frame.nodeBufferB.buffer, 0, 4, 0u);
            vkCmdFillBuffer(l_Frame.commandBuffer, l_Frame.leafBuffer.buffer, 0, 4, 0u);
            vkCmdFillBuffer(l_Frame.commandBuffer, l_Frame.hashBuffer.buffer, 0, VK_WHOLE_SIZE, 0xFFFFFFFFu);

            VkMemoryBarrier2 l_TransferToCompute{ .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
            l_TransferToCompute.srcStageMask = VK_PIPELINE_STAGE_2_CLEAR_BIT | VK_PIPELINE_STAGE_2_COPY_BIT;
            l_TransferToCompute.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            l_TransferToCompute.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            l_TransferToCompute.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;

            VkDependencyInfo l_TransferToComputeDep{ .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
            l_TransferToComputeDep.memoryBarrierCount = 1;
            l_TransferToComputeDep.pMemoryBarriers = &l_TransferToCompute;

            vkCmdPipelineBarrier2(l_Frame.commandBuffer, &l_TransferToComputeDep);

            VkMemoryBarrier2 l_ComputeToCompute{ .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
            l_ComputeToCompute.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_CLEAR_BIT;
            l_ComputeToCompute.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT;
            l_ComputeToCompute.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_CLEAR_BIT;
            l_ComputeToCompute.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT;

            VkDependencyInfo l_ComputeToComputeDep{ .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
            l_ComputeToComputeDep.memoryBarrierCount = 1;
            l_ComputeToComputeDep.pMemoryBarriers = &l_ComputeToCompute;

            ComputePC l_CPC{
                .viewProjMatrix = m_Camera.getVPMatrix(),
                .frustumViewProjMatrix = m_Camera.getFrustumVPMatrix(),
                .cameraRight = glm::vec4(m_Camera.getRight(), 0.0f),
                .cameraUp = glm::vec4(m_Camera.getUp(), 0.0f),
                .viewportSize = glm::vec2(static_cast<float>(m_SwapchainExtent.width), static_cast<float>(m_SwapchainExtent.height)),
                .rootOrigin = l_RootOrigin,
                .rootSize = m_ImguiRootSize,
                .meshletPixelTarget = m_ImguiMeshletPixelTarget,
                .maxDepth = m_ImguiMaxDepth,
                .hashSize = s_HashSize,
                .leafBuf = l_Frame.leafBuffer.address,
                .hashBuf = l_Frame.hashBuffer.address,
                .indirect = l_Frame.indirectBuffer.address,
            };

            vkCmdBindPipeline(l_Frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_RefinePipeline);
            constexpr uint32_t l_RefineGroups = (s_MaxNodes + 63) / 64;

            for (uint32_t l_Depth = 0; l_Depth <= m_ImguiMaxDepth; ++l_Depth)
            {
                const bool l_Even = (l_Depth & 1u) == 0u;
                l_CPC.currentDepth = l_Depth;
                l_CPC.workIn = l_Even ? l_Frame.nodeBufferA.address : l_Frame.nodeBufferB.address;
                l_CPC.workOut = l_Even ? l_Frame.nodeBufferB.address : l_Frame.nodeBufferA.address;
                vkCmdPushConstants(l_Frame.commandBuffer, m_ComputePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(l_CPC), &l_CPC);
                vkCmdDispatch(l_Frame.commandBuffer, l_RefineGroups, 1, 1);

                vkCmdPipelineBarrier2(l_Frame.commandBuffer, &l_ComputeToComputeDep);
                vkCmdFillBuffer(l_Frame.commandBuffer, l_Even ? l_Frame.nodeBufferA.buffer : l_Frame.nodeBufferB.buffer, 0, 4, 0u);
                vkCmdPipelineBarrier2(l_Frame.commandBuffer, &l_ComputeToComputeDep);
            }

            vkCmdBindPipeline(l_Frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_PrepareIndirectPipeline);
            vkCmdPushConstants(l_Frame.commandBuffer, m_ComputePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(l_CPC), &l_CPC);
            vkCmdDispatch(l_Frame.commandBuffer, 1, 1, 1);
            vkCmdPipelineBarrier2(l_Frame.commandBuffer, &l_ComputeToComputeDep);

            constexpr uint32_t l_HashGroups = (s_MaxLeaves + 63) / 64;
            vkCmdBindPipeline(l_Frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_HashBuildPipeline);
            vkCmdDispatch(l_Frame.commandBuffer, l_HashGroups, 1, 1);
            vkCmdPipelineBarrier2(l_Frame.commandBuffer, &l_ComputeToComputeDep);

            vkCmdBindPipeline(l_Frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_NeighborLookupPipeline);
            vkCmdDispatch(l_Frame.commandBuffer, l_HashGroups, 1, 1);

            VkMemoryBarrier2 l_ComputeToGraphics{ .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
            l_ComputeToGraphics.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            l_ComputeToGraphics.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
            l_ComputeToGraphics.dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT;
            l_ComputeToGraphics.dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT;

            VkDependencyInfo l_ComputeToGraphicsDep{ .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
            l_ComputeToGraphicsDep.memoryBarrierCount = 1;
            l_ComputeToGraphicsDep.pMemoryBarriers = &l_ComputeToGraphics;

            vkCmdPipelineBarrier2(l_Frame.commandBuffer, &l_ComputeToGraphicsDep);

            VkImageMemoryBarrier2 l_ImageBarrier{ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
            l_ImageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
            l_ImageBarrier.srcAccessMask = 0;
            l_ImageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            l_ImageBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            l_ImageBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            l_ImageBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            l_ImageBarrier.image = m_SwapchainImages[l_ImageIndex];
            l_ImageBarrier.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1 };

            VkImageMemoryBarrier2 l_DepthBarrier{ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
            l_DepthBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
            l_DepthBarrier.srcAccessMask = 0;
            l_DepthBarrier.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            l_DepthBarrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            l_DepthBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            l_DepthBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            l_DepthBarrier.image = m_DepthImage;
            l_DepthBarrier.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1 };

            VkImageMemoryBarrier2 l_InitialBarriers[] = { l_ImageBarrier, l_DepthBarrier };
            VkDependencyInfo l_DepInfo{ .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
            l_DepInfo.imageMemoryBarrierCount = 2;
            l_DepInfo.pImageMemoryBarriers = l_InitialBarriers;

            vkCmdPipelineBarrier2(l_Frame.commandBuffer, &l_DepInfo);

            VkRenderingAttachmentInfo l_ColorAttachment{ .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
            l_ColorAttachment.imageView = m_SwapchainImageViews[l_ImageIndex];
            l_ColorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            l_ColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            l_ColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            l_ColorAttachment.clearValue = {.color = { 0.0f, 0.0f, 0.1f, 1.0f } };

            VkRenderingAttachmentInfo l_DepthAttachment{ .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
            l_DepthAttachment.imageView = m_DepthImageView;
            l_DepthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            l_DepthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            l_DepthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            l_DepthAttachment.clearValue = {.depthStencil = {.depth = 1.0f, .stencil = 0 } };

            VkRenderingInfo l_RenderingInfo{ .sType = VK_STRUCTURE_TYPE_RENDERING_INFO };
            l_RenderingInfo.renderArea = {.offset = {.x = 0, .y = 0}, .extent = {.width = m_Window.getSize().width, .height = m_Window.getSize().height } };
            l_RenderingInfo.layerCount = 1;
            l_RenderingInfo.colorAttachmentCount = 1;
            l_RenderingInfo.pColorAttachments = &l_ColorAttachment;
            l_RenderingInfo.pDepthAttachment = &l_DepthAttachment;

            vkCmdBeginRendering(l_Frame.commandBuffer, &l_RenderingInfo);

            VkViewport viewport;
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = static_cast<float>(m_SwapchainExtent.width);
            viewport.height = static_cast<float>(m_SwapchainExtent.height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;

            VkRect2D scissor;
            scissor.offset = {.x = 0, .y = 0 };
            scissor.extent = m_SwapchainExtent;

            GraphicsPC l_GPC{
                .viewProjMatrix = m_Camera.getVPMatrix(),
                .leafBuf = l_Frame.leafBuffer.address,
                .rootSize = m_ImguiRootSize,
                .maxDepth = m_ImguiMaxDepth,
                .edgeSnapEnabled = m_ImguiEdgeSnap ? 1u : 0u,
				.scale = m_ImguiScale,
				.height = m_ImguiHeight,
				.normalDist = m_ImguiNormalDist
            };

            vkCmdSetScissor(l_Frame.commandBuffer, 0, 1, &scissor);
			vkCmdSetViewport(l_Frame.commandBuffer, 0, 1, &viewport);

            if (m_ImguiWireframe)
                vkCmdSetPolygonModeEXT(l_Frame.commandBuffer, VK_POLYGON_MODE_LINE);
            else
				vkCmdSetPolygonModeEXT(l_Frame.commandBuffer, VK_POLYGON_MODE_FILL);

            vkCmdBindPipeline(l_Frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_MeshPipeline);
            vkCmdPushConstants(l_Frame.commandBuffer, m_GraphicsPipelineLayout, VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(l_GPC), &l_GPC);

            vkCmdDrawMeshTasksIndirectEXT(l_Frame.commandBuffer, l_Frame.indirectBuffer.buffer, 0, 1, sizeof(uint32_t) * 3);

            imguiDraw(l_FrameIndex);

            vkCmdEndRendering(l_Frame.commandBuffer);

            VkImageMemoryBarrier2 l_PresentBarrier{ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
            l_PresentBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            l_PresentBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            l_PresentBarrier.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
            l_PresentBarrier.dstAccessMask = 0;
            l_PresentBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            l_PresentBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            l_PresentBarrier.image = m_SwapchainImages[l_ImageIndex];
            l_PresentBarrier.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1 };
            
            VkDependencyInfo l_PresentDepInfo{ .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
            l_PresentDepInfo.imageMemoryBarrierCount = 1;
            l_PresentDepInfo.pImageMemoryBarriers = &l_PresentBarrier;

            vkCmdPipelineBarrier2(l_Frame.commandBuffer, &l_PresentDepInfo);

            vkEndCommandBuffer(l_Frame.commandBuffer);
            
			VkPipelineStageFlags l_WaitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

            VkSubmitInfo l_SubmitInfo{ .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO };
			l_SubmitInfo.waitSemaphoreCount = 1;
			l_SubmitInfo.pWaitSemaphores = &l_Frame.imageAvailableSemaphore;
			l_SubmitInfo.pWaitDstStageMask = &l_WaitStage;
			l_SubmitInfo.commandBufferCount = 1;
			l_SubmitInfo.pCommandBuffers = &l_Frame.commandBuffer;
			l_SubmitInfo.signalSemaphoreCount = 1;
			l_SubmitInfo.pSignalSemaphores = &m_RenderFinishedSemaphores[l_ImageIndex];

            VULKAN_TRY(vkQueueSubmit(m_Queue, 1, &l_SubmitInfo, l_Frame.inFlightFence));

            VkPresentInfoKHR l_PresentInfo{ .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
            l_PresentInfo.waitSemaphoreCount = 1;
            l_PresentInfo.pWaitSemaphores = &m_RenderFinishedSemaphores[l_ImageIndex];
            l_PresentInfo.swapchainCount = 1;
            l_PresentInfo.pSwapchains = &m_Swapchain;
            l_PresentInfo.pImageIndices = &l_ImageIndex;

            VULKAN_TRY(vkQueuePresentKHR(m_Queue, &l_PresentInfo));
		}

        m_CurrentFrameIndex++;
	}
}

void Engine::destroyDepthResources()
{
    if (m_DepthImageView != VK_NULL_HANDLE)
    {
        m_DeviceTable.vkDestroyImageView(m_Device, m_DepthImageView, nullptr);
        spdlog::debug("Destroyed depth image view (ID: {})", fmt::ptr(m_DepthImageView));
        m_DepthImageView = VK_NULL_HANDLE;
    }
    if (m_DepthImage != VK_NULL_HANDLE)
    {
        m_DeviceTable.vkDestroyImage(m_Device, m_DepthImage, nullptr);
        spdlog::debug("Destroyed depth image (ID: {})", fmt::ptr(m_DepthImage));
        m_DepthImage = VK_NULL_HANDLE;
    }
    if (m_DepthMemory != VK_NULL_HANDLE)
    {
        m_DeviceTable.vkFreeMemory(m_Device, m_DepthMemory, nullptr);
        spdlog::debug("Freed memory for depth image (ID: {})", fmt::ptr(m_DepthMemory));
        m_DepthMemory = VK_NULL_HANDLE;
    }
}

void Engine::destroyStorageBuffer(StorageBuffer& p_Buffer) const
{
    if (p_Buffer.buffer != VK_NULL_HANDLE)
    {
        m_DeviceTable.vkDestroyBuffer(m_Device, p_Buffer.buffer, nullptr);
        spdlog::debug("Destroyed storage buffer (ID: {})", fmt::ptr(p_Buffer.buffer));
        p_Buffer.buffer = VK_NULL_HANDLE;
    }
    if (p_Buffer.memory != VK_NULL_HANDLE)
    {
        m_DeviceTable.vkFreeMemory(m_Device, p_Buffer.memory, nullptr);
        spdlog::debug("Freed memory for storage buffer (ID: {})", fmt::ptr(p_Buffer.memory));
        p_Buffer.memory = VK_NULL_HANDLE;
    }
    p_Buffer.address = 0;
    p_Buffer.size = 0;
}

void Engine::destroy()
{
    VULKAN_TRY(vkDeviceWaitIdle(m_Device));

	spdlog::info("Destroying engine resources");

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    spdlog::debug("Imgui shutdown complete");

	if (m_GraphicsPipelineLayout != VK_NULL_HANDLE)
	{
		vkDestroyPipelineLayout(m_Device, m_GraphicsPipelineLayout, nullptr);
        spdlog::debug("Destroyed graphics pipeline layout (ID: {})", fmt::ptr(m_GraphicsPipelineLayout));
		m_GraphicsPipelineLayout = VK_NULL_HANDLE;
	}

	if (m_MeshPipeline != VK_NULL_HANDLE)
	{
		vkDestroyPipeline(m_Device, m_MeshPipeline, nullptr);
		spdlog::debug("Destroyed graphics pipeline (ID: {})", fmt::ptr(m_MeshPipeline));
		m_MeshPipeline = VK_NULL_HANDLE;
	}

	for (VkPipeline* p : { &m_RefinePipeline, &m_PrepareIndirectPipeline, &m_HashBuildPipeline, &m_NeighborLookupPipeline })
	{
		if (*p != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(m_Device, *p, nullptr); *p = VK_NULL_HANDLE;
			spdlog::debug("Destroyed compute pipeline (ID: {})", fmt::ptr(*p));
		}
	}
	if (m_ComputePipelineLayout != VK_NULL_HANDLE)
	{
		vkDestroyPipelineLayout(m_Device, m_ComputePipelineLayout, nullptr);
		spdlog::debug("Destroyed compute pipeline layout (ID: {})", fmt::ptr(m_ComputePipelineLayout));
		m_ComputePipelineLayout = VK_NULL_HANDLE;
	}

    if (m_ImguiDescriptorPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(m_Device, m_ImguiDescriptorPool, nullptr);
        spdlog::debug("Destroyed ImGui descriptor pool (ID: {})", fmt::ptr(m_ImguiDescriptorPool));
        m_ImguiDescriptorPool = VK_NULL_HANDLE;
    };

	for (FrameData& l_Frame : m_Frames)
	{
		destroyStorageBuffer(l_Frame.nodeBufferA);
		destroyStorageBuffer(l_Frame.nodeBufferB);
		destroyStorageBuffer(l_Frame.leafBuffer);
		destroyStorageBuffer(l_Frame.hashBuffer);
		destroyStorageBuffer(l_Frame.indirectBuffer);

		if (l_Frame.inFlightFence != VK_NULL_HANDLE)
		{
			m_DeviceTable.vkDestroyFence(m_Device, l_Frame.inFlightFence, nullptr);
		}
		if (l_Frame.imageAvailableSemaphore != VK_NULL_HANDLE)
		{
			m_DeviceTable.vkDestroySemaphore(m_Device, l_Frame.imageAvailableSemaphore, nullptr);
		}
		if (l_Frame.commandBuffer != VK_NULL_HANDLE)
		{
			m_DeviceTable.vkFreeCommandBuffers(m_Device, l_Frame.commandPool, 1, &l_Frame.commandBuffer);
		}
		if (l_Frame.commandPool != VK_NULL_HANDLE)
		{
			m_DeviceTable.vkDestroyCommandPool(m_Device, l_Frame.commandPool, nullptr);
		}
	}
    m_Frames.clear();

    destroyDepthResources();

    if (m_Swapchain != VK_NULL_HANDLE)
    {
		for (const VkSemaphore& l_Semaphore : m_RenderFinishedSemaphores)
        {
			if (l_Semaphore != VK_NULL_HANDLE)
			{
				m_DeviceTable.vkDestroySemaphore(m_Device, l_Semaphore, nullptr);
				spdlog::debug("Destroyed semaphore (ID: {})", fmt::ptr(l_Semaphore));
			}
		}
        for (const VkImageView& l_ImageView : m_SwapchainImageViews)
        {
            if (l_ImageView != VK_NULL_HANDLE)
            {
                m_DeviceTable.vkDestroyImageView(m_Device, l_ImageView, nullptr);
                spdlog::debug("Destroyed swapchain image view (ID: {})", fmt::ptr(l_ImageView));
            }
        }
        m_SwapchainImageViews.clear();
        m_SwapchainImages.clear();

        m_DeviceTable.vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr);
        spdlog::debug("Destroyed swapchain (ID: {})", fmt::ptr(m_Swapchain));
        m_Swapchain = VK_NULL_HANDLE;
    }
	if (m_Device != VK_NULL_HANDLE)
	{
		vkDestroyDevice(m_Device, nullptr);
		spdlog::debug("Destroyed logical device (ID: {})", fmt::ptr(m_Device));
		m_Device = VK_NULL_HANDLE;
	}
	if (m_DebugMessenger != VK_NULL_HANDLE)
	{
		destroyDebugUtilsMessengerEXT(m_Instance, m_DebugMessenger, nullptr);
		spdlog::debug("Destroyed debug messenger (ID: {})", fmt::ptr(m_DebugMessenger));
		m_DebugMessenger = VK_NULL_HANDLE;
	}

    m_Window.destroy(m_Instance);

	if (m_Instance != VK_NULL_HANDLE)
	{
		vkDestroyInstance(m_Instance, nullptr);
		spdlog::debug("Destroyed Vulkan instance (ID: {})", fmt::ptr(m_Instance));
		m_Instance = VK_NULL_HANDLE;
	}

    spdlog::info("Engine resources destroyed successfully");
}
