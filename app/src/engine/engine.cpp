#include "engine.hpp"

#include <array>
#include <volk.h>
#include <spdlog/spdlog.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include "shader.hpp"
#include "vk_base.hpp"

constexpr bool g_AssertOnError = true;

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

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(const VkDebugUtilsMessageSeverityFlagBitsEXT p_MessageSeverity, VkDebugUtilsMessageTypeFlagsEXT p_MessageType, const VkDebugUtilsMessengerCallbackDataEXT* p_CallbackData, void* p_UserData)
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

		// Find mesh shader support
		bool l_MeshShaderSupport = false;
        bool l_SwapchainSupport = false;
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
			if (l_MeshShaderSupport && l_SwapchainSupport)
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

        VkPhysicalDeviceMeshShaderFeaturesEXT l_MeshFeatures{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT };
        VkPhysicalDeviceVulkan13Features l_Features13{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
        l_Features13.pNext = &l_MeshFeatures;

        VkPhysicalDeviceFeatures2 l_Features2{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
        l_Features2.pNext = &l_Features13;
        vkGetPhysicalDeviceFeatures2(l_PhysicalDevice, &l_Features2);

        bool l_SupportsMeshShaders = l_MeshFeatures.meshShader && l_MeshFeatures.taskShader;
		bool l_SupportsFeatures13 = l_Features13.dynamicRendering && l_Features13.synchronization2;
        if (!l_SupportsFeatures13 || !l_SupportsMeshShaders)
        {
			spdlog::debug("- Physical device (ID: {}) does not support required features (mesh shader support: {}, dynamic rendering support: {}, synchronization2 support: {}), skipping", fmt::ptr(l_PhysicalDevice), l_SupportsMeshShaders, l_Features13.dynamicRendering, l_Features13.synchronization2);
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

    // Check that the number of frames in flight does not exceed the maximum number of images supported by the swapchain
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

    VkDescriptorPoolSize l_PoolSizes[] =
    {
    	{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE },
		{ VK_DESCRIPTOR_TYPE_SAMPLER, IMGUI_IMPL_VULKAN_MINIMUM_SAMPLER_POOL_SIZE },
    };
    VkDescriptorPoolCreateInfo l_PoolInfo = {};
    l_PoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    l_PoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    l_PoolInfo.maxSets = 0;
    for (const VkDescriptorPoolSize& l_PoolSize : l_PoolSizes)
        l_PoolInfo.maxSets += l_PoolSize.descriptorCount;
    l_PoolInfo.poolSizeCount = static_cast<uint32_t>(IM_COUNTOF(l_PoolSizes));
    l_PoolInfo.pPoolSizes = l_PoolSizes;
    VULKAN_TRY(vkCreateDescriptorPool(m_Device, &l_PoolInfo, nullptr, &m_ImguiDescriptorPool));

    VkPipelineRenderingCreateInfo l_PipelineRenderingInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .pNext = nullptr,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &m_SwapchainFormat.format,
        .depthAttachmentFormat = VK_FORMAT_D32_SFLOAT,
        .stencilAttachmentFormat = VK_FORMAT_UNDEFINED
    };


    ImGui_ImplSDL3_InitForVulkan(*m_Window);
    ImGui_ImplVulkan_InitInfo l_InitInfo = {};
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

        volkLoadInstance(m_Instance);
		spdlog::info("Loaded instance functions with volk");
        
        m_Window.createSurface(m_Instance);
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

		std::array<const char*, 2> l_DeviceExtensions = { VK_EXT_MESH_SHADER_EXTENSION_NAME, VK_KHR_SWAPCHAIN_EXTENSION_NAME };
		l_DeviceCreateInfo.ppEnabledExtensionNames = l_DeviceExtensions.data();
        l_DeviceCreateInfo.enabledExtensionCount = 2;

        VkPhysicalDeviceMeshShaderFeaturesEXT l_MeshFeatures{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT };
        l_MeshFeatures.taskShader = VK_TRUE;
        l_MeshFeatures.meshShader = VK_TRUE;
        VkPhysicalDeviceVulkan13Features l_Features13{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
        l_Features13.dynamicRendering = VK_TRUE;
		l_Features13.synchronization2 = VK_TRUE;
        l_Features13.pNext = &l_MeshFeatures;
        VkPhysicalDeviceFeatures2 l_DeviceFeatures{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
        l_DeviceFeatures.pNext = &l_Features13;

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
		}
    }

    // Pipeline
    {
		{
            VkPushConstantRange l_PushConstantRange{
			    .stageFlags = VK_SHADER_STAGE_MESH_BIT_EXT,
			    .offset = 0,
			    .size = sizeof(glm::vec4)
            };

            VkPipelineLayoutCreateInfo l_LayoutInfo{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .pushConstantRangeCount = 1,
                .pPushConstantRanges = &l_PushConstantRange
            };
            VULKAN_TRY(vkCreatePipelineLayout(m_Device, &l_LayoutInfo, nullptr, &m_PipelineLayout));
			spdlog::info("Created pipeline layout (ID: {}) with push constant range for mesh shader stage", fmt::ptr(m_PipelineLayout));
		}

        std::vector<uint32_t> l_MeshSpirv;
    	std::vector<uint32_t> l_FragSpirv;
        Shader shaderBundle("src/shaders/mesh.slang");
        try
        {
            l_MeshSpirv = shaderBundle.getSpirv("meshMain");
            l_FragSpirv = shaderBundle.getSpirv("fragmentMain");
			spdlog::info("Loaded shader SPIR-V for mesh and fragment stages from shader bundle");
        }
		catch (const std::exception& e)
		{
			spdlog::error("Failed to load shader: {}", e.what());
			throw std::runtime_error("Failed to load shader");
		}

    	VkShaderModuleCreateInfo meshCreateInfo{
		    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		    .codeSize = l_MeshSpirv.size() * sizeof(uint32_t),
		    .pCode = l_MeshSpirv.data()
        };
        VkShaderModule l_MeshModule;
        vkCreateShaderModule(m_Device, &meshCreateInfo, nullptr, &l_MeshModule);

		VkShaderModuleCreateInfo fragCreateInfo{
			.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
			.codeSize = l_FragSpirv.size() * sizeof(uint32_t),
			.pCode = l_FragSpirv.data()
		};
		VkShaderModule l_FragModule;
		vkCreateShaderModule(m_Device, &fragCreateInfo, nullptr, &l_FragModule);

        std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};
        shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[0].stage = VK_SHADER_STAGE_MESH_BIT_EXT;
        shaderStages[0].module = l_MeshModule;
        shaderStages[0].pName = "main";

        shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        shaderStages[1].module = l_FragModule;
        shaderStages[1].pName = "main";

        VkPipelineRenderingCreateInfo renderingCreateInfo{
		    .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
		    .colorAttachmentCount = 1,
		    .pColorAttachmentFormats = &m_SwapchainFormat.format
        };

		VkPipelineViewportStateCreateInfo l_ViewportState{ .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
		l_ViewportState.scissorCount = 1;
		l_ViewportState.viewportCount = 1;

        VkPipelineRasterizationStateCreateInfo l_RasterizationState{ .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        l_RasterizationState.depthClampEnable = VK_FALSE;
        l_RasterizationState.rasterizerDiscardEnable = VK_FALSE;
        l_RasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
        l_RasterizationState.lineWidth = 1.0f;
        l_RasterizationState.cullMode = VK_CULL_MODE_BACK_BIT;
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

        std::array<VkDynamicState, 2> l_DynamicStates = {
		    VK_DYNAMIC_STATE_VIEWPORT,
		    VK_DYNAMIC_STATE_SCISSOR
        };

        VkPipelineDynamicStateCreateInfo l_DynamicState{ .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
        l_DynamicState.dynamicStateCount = static_cast<uint32_t>(std::size(l_DynamicStates));
        l_DynamicState.pDynamicStates = l_DynamicStates.data();

		VkGraphicsPipelineCreateInfo pipelineCreateInfo{ .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        pipelineCreateInfo.pNext = &renderingCreateInfo;
        pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
        pipelineCreateInfo.pStages = shaderStages.data();
        
        pipelineCreateInfo.pVertexInputState = nullptr;
        pipelineCreateInfo.pInputAssemblyState = nullptr;
        
	    pipelineCreateInfo.pViewportState = &l_ViewportState;
	    pipelineCreateInfo.pRasterizationState = &l_RasterizationState;
	    pipelineCreateInfo.pMultisampleState = &l_MultisampleState;
	    pipelineCreateInfo.pColorBlendState = &l_ColorBlendState;
		pipelineCreateInfo.pDepthStencilState = nullptr;
		pipelineCreateInfo.pDynamicState = &l_DynamicState;
        
	    pipelineCreateInfo.layout = m_PipelineLayout;
        pipelineCreateInfo.renderPass = VK_NULL_HANDLE;

        VULKAN_TRY(vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &m_MeshPipeline));

		vkDestroyShaderModule(m_Device, l_MeshModule, nullptr);
		spdlog::debug("Destroyed shader module (ID: {}) for mesh shader stage", fmt::ptr(l_MeshModule));
		vkDestroyShaderModule(m_Device, l_FragModule, nullptr);
		spdlog::debug("Destroyed shader module (ID: {}) for fragment shader stage", fmt::ptr(l_FragModule));

		spdlog::info("Created graphics pipeline (ID: {}) with mesh shader and fragment shader", fmt::ptr(m_MeshPipeline));
    }

    // Imgui
    {
        initImgui();
        spdlog::info("Initialized ImGui for Vulkan");
    }
}

void Engine::imguiDraw(const uint32_t p_FrameIndex)
{
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
        ImGui::ColorPicker4("Color Multiplier", &m_ColorMultiplier[0]);
		ImGui::End();
    }

    ImGui::Render();
    ImDrawData* l_DrawData = ImGui::GetDrawData();
    ImGui_ImplVulkan_RenderDrawData(l_DrawData, m_Frames[p_FrameIndex].commandBuffer);
}

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

            VkImageMemoryBarrier2 l_ImageBarrier{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                .srcAccessMask = 0,
                .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .image = m_SwapchainImages[l_ImageIndex],
                .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1 }
            };

            VkDependencyInfo l_DepInfo{
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .imageMemoryBarrierCount = 1,
                .pImageMemoryBarriers = &l_ImageBarrier
            };

            vkCmdPipelineBarrier2(l_Frame.commandBuffer, &l_DepInfo);

            VkRenderingAttachmentInfo l_ColorAttachment{
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = m_SwapchainImageViews[l_ImageIndex],
                .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue = {.color = { 0.0f, 0.0f, 0.1f, 1.0f } }
            };

            VkRenderingInfo l_RenderingInfo{
                .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                .renderArea = {.offset = {.x = 0, .y = 0}, .extent = {.width = m_Window.getSize().width, .height = m_Window.getSize().height } },
                .layerCount = 1,
                .colorAttachmentCount = 1,
                .pColorAttachments = &l_ColorAttachment
            };

            vkCmdBeginRendering(l_Frame.commandBuffer, &l_RenderingInfo);

            VkViewport viewport;
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = static_cast<float>(m_SwapchainExtent.width);
            viewport.height = static_cast<float>(m_SwapchainExtent.height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;

            VkRect2D scissor;
            scissor.offset = { 0, 0 };
            scissor.extent = m_SwapchainExtent;

            vkCmdSetScissor(l_Frame.commandBuffer, 0, 1, &scissor);
			vkCmdSetViewport(l_Frame.commandBuffer, 0, 1, &viewport);

            vkCmdBindPipeline(l_Frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_MeshPipeline);
            vkCmdPushConstants(l_Frame.commandBuffer, m_PipelineLayout, VK_SHADER_STAGE_MESH_BIT_EXT, 0, sizeof(m_ColorMultiplier), &m_ColorMultiplier);
            vkCmdDrawMeshTasksEXT(l_Frame.commandBuffer, 1, 1, 1);

            imguiDraw(l_FrameIndex);

            vkCmdEndRendering(l_Frame.commandBuffer);

            l_ImageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            l_ImageBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            l_ImageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
            l_ImageBarrier.dstAccessMask = 0;
            l_ImageBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            l_ImageBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

            vkCmdPipelineBarrier2(l_Frame.commandBuffer, &l_DepInfo);

            vkEndCommandBuffer(l_Frame.commandBuffer);
            
			VkPipelineStageFlags l_WaitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

			VkSubmitInfo l_SubmitInfo{
				.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
				.waitSemaphoreCount = 1,
				.pWaitSemaphores = &l_Frame.imageAvailableSemaphore,
				.pWaitDstStageMask = &l_WaitStage,
				.commandBufferCount = 1,
				.pCommandBuffers = &l_Frame.commandBuffer,
				.signalSemaphoreCount = 1,
				.pSignalSemaphores = &m_RenderFinishedSemaphores[l_ImageIndex]
			};

            VULKAN_TRY(vkQueueSubmit(m_Queue, 1, &l_SubmitInfo, l_Frame.inFlightFence));

            VkPresentInfoKHR l_PresentInfo{
                .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                .waitSemaphoreCount = 1,
                .pWaitSemaphores = &m_RenderFinishedSemaphores[l_ImageIndex],
                .swapchainCount = 1,
                .pSwapchains = &m_Swapchain,
                .pImageIndices = &l_ImageIndex
            };

            VULKAN_TRY(vkQueuePresentKHR(m_Queue, &l_PresentInfo));
		}

        m_CurrentFrameIndex++;
	}
}

void Engine::destroy()
{
    VULKAN_TRY(vkDeviceWaitIdle(m_Device));

	spdlog::info("Destroying engine resources");

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    spdlog::debug("Imgui shutdown complete");

	if (m_PipelineLayout != VK_NULL_HANDLE)
	{
		vkDestroyPipelineLayout(m_Device, m_PipelineLayout, nullptr);
		spdlog::debug("Destroyed pipeline layout (ID: {})", fmt::ptr(m_PipelineLayout));
		m_PipelineLayout = VK_NULL_HANDLE;
	}

	if (m_MeshPipeline != VK_NULL_HANDLE)
	{
		vkDestroyPipeline(m_Device, m_MeshPipeline, nullptr);
		spdlog::debug("Destroyed graphics pipeline (ID: {})", fmt::ptr(m_MeshPipeline));
		m_MeshPipeline = VK_NULL_HANDLE;
	}

    if (m_ImguiDescriptorPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(m_Device, m_ImguiDescriptorPool, nullptr);
        spdlog::debug("Destroyed ImGui descriptor pool (ID: {})", fmt::ptr(m_ImguiDescriptorPool));
        m_ImguiDescriptorPool = VK_NULL_HANDLE;
    };

	for (auto& [commandPool, commandBuffer, imageAvailableSemaphore, inFlightFence] : m_Frames)
	{
		if (inFlightFence != VK_NULL_HANDLE)
		{
			m_DeviceTable.vkDestroyFence(m_Device, inFlightFence, nullptr);
			spdlog::debug("Destroyed fence (ID: {}) for frame in-flight synchronization", fmt::ptr(inFlightFence));
		}
		if (imageAvailableSemaphore != VK_NULL_HANDLE)
		{
			m_DeviceTable.vkDestroySemaphore(m_Device, imageAvailableSemaphore, nullptr);
			spdlog::debug("Destroyed semaphore (ID: {}) for frame image availability", fmt::ptr(imageAvailableSemaphore));
		}
		if (commandBuffer != VK_NULL_HANDLE)
		{
			m_DeviceTable.vkFreeCommandBuffers(m_Device, commandPool, 1, &commandBuffer);
			spdlog::debug("Freed command buffer (ID: {}) from command pool (ID: {}) for frame", fmt::ptr(commandBuffer), fmt::ptr(commandPool));
		}
		if (commandPool != VK_NULL_HANDLE)
		{
			m_DeviceTable.vkDestroyCommandPool(m_Device, commandPool, nullptr);
			spdlog::debug("Destroyed command pool (ID: {}) for frame", fmt::ptr(commandPool));
		}
	}
    m_Frames.clear();

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
