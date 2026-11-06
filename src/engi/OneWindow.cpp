#define RGFW_IMPLEMENTATION
//#define RGFW_X11
#define VK_USE_PLATFORM_XLIB_KHR
#define VMA_IMPLEMENTATION

#include <print>
#include <vector>
#include <ranges>
#include <algorithm>
#include <set>
#include <deque>

#include <engi/OneWindow.hpp>
#include <engi/Image.hpp>
#include <engi/DeviceFeatures.hpp>

#include <X11/Xlib.h>
#include <RGFW.h>

#ifdef ENGI_RENDER_DEBUG
constexpr bool IS_DEBUG_RENDERING = true;
#else
constexpr bool IS_DEBUG_RENDERING = false;
#endif

namespace engi
{
template<typename FUNC, typename OUT, typename ... IN>
auto vkenum(FUNC pFunc, std::vector<OUT>& pOut, IN... pIn) noexcept -> VkResult
    requires (std::is_invocable_r_v<VkResult, decltype(pFunc), IN..., uint32_t*, OUT*>)
{
    uint32_t count = 0;
    auto res = pFunc(pIn..., &count, nullptr);
    if (res != VK_SUCCESS) 
        return res;
    
    pOut = std::vector<OUT>(count);
    return pFunc(pIn..., &count, pOut.data());
}

template<typename FUNC, typename OUT,  typename ...IN>
auto vkenum(FUNC pFunc, std::vector<OUT>& pOut, IN... pIn) noexcept -> void
{
    uint32_t count = 0;
    pFunc(pIn..., &count, nullptr);
    pOut = std::vector<OUT>(count);
    return pFunc(pIn..., &count, pOut.data());
}

template<typename T, typename Fn>
auto find_if(const std::vector<T>& t, Fn fn) -> std::optional<T>
{
    if (auto it = std::ranges::find_if(t, fn); it != std::end(t)) return *it;
    return std::nullopt;
}
}

static VKAPI_ATTR VkBool32 VKAPI_CALL 
debug_cb(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT type, 
        const VkDebugUtilsMessengerCallbackDataEXT* data, void* user_data) noexcept
{
    std::println("[VALIDATION] {}", data->pMessage);
    bool do_assert = severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    if (do_assert) assert(false);
    return VK_FALSE;
}

auto check_present_support(VkPhysicalDevice device, int family_queue_id) -> bool
{
    if constexpr(__linux)
    {
        auto display = (Display*)RGFW_getDisplay_X11();
        auto visual = XDefaultVisual(display, XDefaultScreen(display));
        return vkGetPhysicalDeviceXlibPresentationSupportKHR(device, 
                family_queue_id, display, visual->visualid) == VK_TRUE;
    }
    else // Not supported
    {
        assert(false);
        return false;
    }
}

struct engi::OneWindow::Impl 
{
    struct RenderingQueue 
    {
        uint32_t family = 0;
        uint32_t id = 0;
        VkQueue queue = VK_NULL_HANDLE;
    };

    RGFW_window* window = nullptr;

    VkInstance instance = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice device_physical = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE; 
    VkDebugUtilsMessengerEXT debugger = VK_NULL_HANDLE;

    RenderingQueue gfx_queue = {};
    RenderingQueue present_queue = {};

    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR color_space = VK_COLOR_SPACE_MAX_ENUM_KHR;

    std::vector<engi::Image> sw_color_images;
    std::vector<engi::Image> sw_resolve_images;
    std::vector<VkImageView> sw_resolve_images_view_unorm;
    std::vector<engi::Image> sw_depth_images;

    VkSemaphore timeline = VK_NULL_HANDLE;
    uint64_t submit_counter = 0;
    std::array<uint64_t, FRAMES_FLYING> frame_values = {0};

    std::array<VkSemaphore, FRAMES_FLYING> acquire_sems = {VK_NULL_HANDLE};
    std::array<VkSemaphore, FRAMES_FLYING> render_done_sems = {VK_NULL_HANDLE};
    std::array<VkFence, FRAMES_FLYING> present_fences = {VK_NULL_HANDLE};
    std::array<bool, FRAMES_FLYING> present_pending = {false};
    bool needs_resize = false;

    VkCommandPool cmd_pool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> color_cmd_buffers;
    std::vector<VkCommandBuffer> present_cmd_buffers;

    std::vector<VkCommandBuffer> main_cmd_buffers;
    std::vector<VkCommandBuffer> work_cmd_buffers;


    uint32_t image_id = 0;
    uint32_t frame_id = 0;

    VkExtent2D extent = { 0, 0 };

    auto create_window(size_t width, size_t height, const std::string& name) -> bool
    {
        window = RGFW_createWindow(name.c_str(), 0, 0, width, height, RGFW_windowCenter | RGFW_windowNoResize);
        //window = RGFW_createWindow(name.c_str(), 0, 0, width, height, RGFW_windowCenter);
        //RGFW_window_setFlags(window, RGFW_windowCenter | RGFW_windowNoResize);
        RGFW_window_setExitKey(window, RGFW_escape);
        if (!window)
        {
            std::println("[ERROR] Window creation failed");
            return false;
        }
        return true;
    }

    auto create_instance() -> bool
    {
        const auto layers = get_instance_layers();
        const auto extensions = get_instance_extensions();

        VkApplicationInfo app_info = 
        {
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pNext = nullptr,
            .pApplicationName = "ENGI",
            .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
            .pEngineName = "ENGI",
            .engineVersion = VK_MAKE_VERSION(1, 0, 0),
            .apiVersion = VK_API_VERSION_1_4
        };

        std::println("[INFO] Vulkan API version: {}", app_info.apiVersion);

        VkValidationFeatureEnableEXT enableValidationFeatures[] = 
        {
            VkValidationFeatureEnableEXT::VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
        };
        VkValidationFeaturesEXT validationFeatures = 
        {
            .sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
            .pNext = nullptr,
            .enabledValidationFeatureCount = sizeof(enableValidationFeatures) / sizeof(enableValidationFeatures[0]),
            .pEnabledValidationFeatures = enableValidationFeatures,
            .disabledValidationFeatureCount = 0,
            .pDisabledValidationFeatures = nullptr
        };
        VkInstanceCreateInfo info = 
        {
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            // TO REMOVE SYNCHRONIZATION DEBUG, COMMENT THIS OUT
            .pNext = &validationFeatures,
            //.pNext = nullptr,
            .flags = 0,
            .pApplicationInfo = &app_info,
            .enabledLayerCount = (uint32_t)(layers.size()),
            .ppEnabledLayerNames = layers.empty() ? nullptr : layers.data(),
            .enabledExtensionCount = (uint32_t)extensions.size(),
            .ppEnabledExtensionNames = extensions.data()
        };

        auto result = vkCreateInstance(&info, nullptr, &instance);
        VK_CHECK_RETURN(result, "[ERROR] Instance creation failed: {}");

        return true;
    }

    auto create_debugger() -> bool
    {
        if constexpr (!IS_DEBUG_RENDERING)
            return true;

        VkDebugUtilsMessengerCreateInfoEXT info = 
        {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .pNext = nullptr,
            .flags = 0,
            .messageSeverity = 
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | 
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | 
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | 
                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | 
                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = debug_cb,
            .pUserData = nullptr
        };
        auto fn = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
        auto result = fn(instance, &info, nullptr, &debugger);
        VK_CHECK_RETURN(result, "[ERROR] Debugger creation failed: {}");

        std::println("[INFO] Debugger created successfully");
        return true;
    }

    auto select_physical_device() -> bool
    {
        std::vector<VkPhysicalDevice> devices;
        auto result = engi::vkenum(vkEnumeratePhysicalDevices, devices, instance);
        VK_CHECK_RETURN(result, "[ERROR] Can't enumerate physical devices: {}");

        struct DeviceProps
        {
            bool is_discrete;
            bool has_present_support;
            std::optional<VkFormat> required_format;
            VkPhysicalDevice handle;
        };

        auto get_device_props_fn = [](VkPhysicalDevice dev) -> DeviceProps
        {
            VkPhysicalDeviceProperties physical_props;
            vkGetPhysicalDeviceProperties(dev, &physical_props);
            bool is_discrete = physical_props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;

            uint32_t queue_count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(dev, &queue_count, nullptr);
            bool has_present = std::ranges::any_of(
                std::views::iota(0u, queue_count), 
                [dev](size_t i) {return check_present_support(dev, i); }
            );

            auto get_required_format = [](VkPhysicalDevice dev) -> std::optional<VkFormat>
            {
                auto formats = get_depth_format_list();
                
                auto it = std::ranges::find_if(formats, [dev](VkFormat format) {
                    VkFormatProperties props;
                    vkGetPhysicalDeviceFormatProperties(dev, format, &props);
                    return (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
                });
                return (it != formats.end()) ? std::optional{*it} : std::nullopt;
            };

            return 
            {
                .is_discrete = is_discrete,
                .has_present_support = has_present,
                .required_format = get_required_format(dev),
                .handle = dev
            };
        };

        std::vector<DeviceProps> dev_props;

        for (auto dev : devices)
            dev_props.push_back(get_device_props_fn(dev));

        bool selected = false;
        for (auto props : dev_props)
        {
            if (!props.required_format)
                continue;

            if (!props.has_present_support)
                continue;

            device_physical = props.handle;
            depth_format = *props.required_format;
            selected = true;
            if (props.is_discrete)
                break;
        }

        if (!selected)
        {
            std::println("[ERROR] Couldn't find needed depth format");
        }

        return true;
    }

    auto select_queue_families() -> bool
    {
        std::vector<VkQueueFamilyProperties> props;
        engi::vkenum(vkGetPhysicalDeviceQueueFamilyProperties, props, device_physical);

        auto selected_gfx_queue = [&props]
        {
            auto gfx = std::views::iota(0u, props.size()) | std::views::filter([&](uint32_t i) 
            { 
                return props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT;
            });
            return gfx.empty() ? std::nullopt : std::make_optional(gfx.front());
        }();
        OPT_CHECK_RETURN(selected_gfx_queue, "[ERROR] Can't find gfx queue");

        auto selected_present_queue = [&]
        {
            if (check_present_support(device_physical, *selected_gfx_queue))
                return selected_gfx_queue;

            auto present = std::views::iota(0u, props.size())| std::views::filter([&](uint32_t i) 
            { 
                return check_present_support(device_physical, i); 
            });
            return present.empty() ? std::nullopt : std::make_optional(present.front());
        }();
        OPT_CHECK_RETURN(selected_present_queue, "[ERROR] Can't find present queue");

        gfx_queue = RenderingQueue { .family = *selected_gfx_queue, .id = 0 };
        present_queue = RenderingQueue { .family = *selected_present_queue, .id = 0 };
        
        return true;
    }

    auto create_queue_infos(const std::vector<RenderingQueue>& queues) -> std::vector<VkDeviceQueueCreateInfo>
    {
        static const float priority = 1.0f;

        return queues 
            | std::views::transform([](const auto& q) { return q.family; })
            | std::ranges::to<std::set>()
            | std::views::transform([](uint32_t family) {
                return VkDeviceQueueCreateInfo{
                    .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                    .queueFamilyIndex = family,
                    .queueCount = 1,
                    .pQueuePriorities = &priority
                };
            })
            | std::ranges::to<std::vector>();
    }

    auto create_device_and_queues() -> bool
    {
        if (!select_queue_families())
            return false;

        auto queue_infos = create_queue_infos({ gfx_queue, present_queue });
        auto features = DeviceFeatures::create();
        auto extensions = get_device_extensions();
        auto layers = get_device_layers();
        VkDeviceCreateInfo deviceInfo =
        {
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pNext = &features->features2, 
            .flags = 0,
            .queueCreateInfoCount = (uint32_t)(queue_infos.size()),
            .pQueueCreateInfos = queue_infos.data(),
            .enabledLayerCount = (uint32_t)(layers.size()),
            .ppEnabledLayerNames = layers.empty() ? nullptr : layers.data(),
            .enabledExtensionCount = (uint32_t)extensions.size(),
            .ppEnabledExtensionNames = extensions.empty() ? nullptr : extensions.data(),
            .pEnabledFeatures =  nullptr
        };

        auto result = vkCreateDevice(device_physical, &deviceInfo, nullptr, &device);
        VK_CHECK_RETURN(result, "[ERROR] Device creation failed: {}");

        vkGetDeviceQueue(device, gfx_queue.family, gfx_queue.id, &gfx_queue.queue);
        vkGetDeviceQueue(device, present_queue.family, present_queue.id, &present_queue.queue);

        return true;
    }

    auto create_allocator() -> bool
    {
        VmaAllocatorCreateInfo info = 
        {
            .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
            .physicalDevice = device_physical,
            .device = device,
            /// Preferred size of a single `VkDeviceMemory` block to be allocated from large heaps > 1 GiB. Optional.
            /** Set to 0 to use default, which is currently 256 MiB. */
            .preferredLargeHeapBlockSize = 0,
            .pAllocationCallbacks = nullptr,
            .pDeviceMemoryCallbacks = nullptr,
            //.frameInUseCount = 2,
            .pHeapSizeLimit = nullptr,
            .pVulkanFunctions = nullptr,
            //.pRecordSettings = nullptr,
            .instance = instance,
            .vulkanApiVersion = VK_API_VERSION_1_4
        };

        auto result = vmaCreateAllocator(&info, &allocator);
        VK_CHECK_RETURN(result, "[ERROR] Allocator creation failed: {}");

        return true;
    }

    auto create_surface() -> bool
    {
        if constexpr (__linux)
        {
            auto disp  = (Display*)RGFW_getDisplay_X11();
            auto win = RGFW_window_getWindow_X11(window);
            
            VkXlibSurfaceCreateInfoKHR info = 
            {
                .sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
                .pNext = nullptr,
                .flags = 0,
                .dpy = disp,
                .window = win
            }; 
            auto result = vkCreateXlibSurfaceKHR(instance, &info, nullptr, &surface);
            VK_CHECK_RETURN(result, "[ERROR] Surface creation failed: {}");

            return true;
        }
        else
        {
            assert(false);
        }
    }

    auto create_swapchain() -> bool
    {
        std::vector<VkSurfaceFormatKHR> formats;
        engi::vkenum(vkGetPhysicalDeviceSurfaceFormatsKHR, formats, device_physical, surface);
        auto opt_format = engi::find_if(formats, [](VkSurfaceFormatKHR f) 
        { 
            return f.format == VK_FORMAT_B8G8R8A8_SRGB && 
            //return f.format == VK_FORMAT_B8G8R8A8_UNORM && 
                f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        });
        OPT_CHECK_RETURN(opt_format, "[ERROR] Couldn't find needed surface format");
        color_format = opt_format->format;
        color_space = opt_format->colorSpace;

        std::vector<VkPresentModeKHR> modes;
        engi::vkenum(vkGetPhysicalDeviceSurfacePresentModesKHR, modes, device_physical, surface);
        auto opt_mode = engi::find_if(modes, [](VkPresentModeKHR m) 
        {
            return m == VK_PRESENT_MODE_MAILBOX_KHR 
                || m == VK_PRESENT_MODE_FIFO_KHR;
        });
        OPT_CHECK_RETURN(opt_mode, "[ERROR] Couldn't find needed present mode");

        VkSurfaceCapabilitiesKHR cap;
        auto result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device_physical, surface, &cap);
        VK_CHECK_RETURN(result, "[ERROR] Couldn't get data from device: {}");

        auto get_extent = [](const VkSurfaceCapabilitiesKHR& cap, RGFW_window* win) -> std::optional<VkExtent2D>
        {
            VkExtent2D e = cap.currentExtent;
            if (e.width == UINT32_MAX)
            {
                int w = 0, h = 0;
                RGFW_window_getSize(win, &w, &h);
                e = {
                    std::clamp(uint32_t(std::max(w, 0)), cap.minImageExtent.width,  cap.maxImageExtent.width),
                    std::clamp(uint32_t(std::max(h, 0)), cap.minImageExtent.height, cap.maxImageExtent.height),
                };
            }
            return (e.width && e.height) ? std::optional(e) : std::nullopt;
        };

        auto opt_extent = get_extent(cap, window);
        OPT_CHECK_RETURN(opt_extent, "[ERROR] Couldn't find max extents");
        extent = opt_extent.value();

        bool gfx_present_same = gfx_queue.family == present_queue.family;
        auto indices = std::array{gfx_queue.family, present_queue.family};

        VkFormat view_formats[] = { VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_B8G8R8A8_SRGB };
        VkImageFormatListCreateInfo format_list = 
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO,
            .viewFormatCount = 2,
            .pViewFormats = view_formats,
        };

        VkSwapchainCreateInfoKHR info = 
        {
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .pNext = &format_list,
            .flags = VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR,
            .surface = surface,
            .minImageCount = std::clamp(cap.minImageCount+1, cap.minImageCount,
                cap.maxImageCount > 0 ? cap.maxImageCount : 256),
            .imageFormat = color_format,
            .imageColorSpace = color_space,
            .imageExtent = extent,
            .imageArrayLayers = 1,
            .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .imageSharingMode = gfx_present_same ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT,
            .queueFamilyIndexCount = gfx_present_same ? 0u : uint32_t(indices.size()),
            .pQueueFamilyIndices = gfx_present_same ? nullptr : indices.data(),
            .preTransform = cap.currentTransform,
            .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            .presentMode = opt_mode.value(),
            .clipped = VK_TRUE,
            .oldSwapchain = VK_NULL_HANDLE
        };

        result = vkCreateSwapchainKHR(device, &info, nullptr, &swapchain);
        VK_CHECK_RETURN(result, "[ERROR] Swapchain creation failed: {}");

        return true;
    }

    auto create_swapchain_images() -> bool
    {
        std::vector<VkImage> swapchain_images;
        engi::vkenum(vkGetSwapchainImagesKHR, swapchain_images, device, swapchain);

        VkImageCreateInfo image_info = 
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .imageType = VkImageType::VK_IMAGE_TYPE_2D,
            .extent = { extent.width, extent.height, 1}, 
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = ANTIALIASING_SAMPLES,
            .tiling = VkImageTiling::VK_IMAGE_TILING_OPTIMAL,
            .sharingMode = VkSharingMode::VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
        };

        VkImageViewCreateInfo view_info = 
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, 
                VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY},
            .subresourceRange = 
            {
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };

        for (auto sw_image : swapchain_images)
        {
            view_info.image = VK_NULL_HANDLE; // will be added inside create method
            view_info.format = color_format;
            view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    
            auto image = Image::create(sw_image, view_info, allocator, device);
            EXP_CHECK_RETURN(image, "[ERROR] Couldn't create resolve images for swapchanin: {}");
            sw_resolve_images.emplace_back(std::move(image.value()));

            view_info.format = VK_FORMAT_B8G8R8A8_UNORM;
            view_info.image = sw_image;

            VkImageView unorm_view = VK_NULL_HANDLE;
            auto res = vkCreateImageView(device, &view_info, nullptr, &unorm_view);
            VK_CHECK_RETURN(res, "[ERROR] Couldn't create UNORM view for swapchain: {}");
            sw_resolve_images_view_unorm.push_back(unorm_view);
        }

        for (auto _ : swapchain_images)
        {
            image_info.format = color_format;
            image_info.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

            view_info.image = VK_NULL_HANDLE; // will be added inside create method
            view_info.format = color_format;
            view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        
            auto image = Image::create(image_info, view_info, allocator, device);
            EXP_CHECK_RETURN(image, "[ERROR] Couldn't create color images for swapchanin: {}");
            sw_color_images.emplace_back(std::move(image.value()));
        }

        for (auto _ : swapchain_images)
        {
            image_info.format = depth_format;
            image_info.usage = VkImageUsageFlagBits::VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

            view_info.image = VK_NULL_HANDLE; // will be added inside create method
            view_info.format = depth_format;
            view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

            auto image = Image::create(image_info, view_info, allocator, device);
            EXP_CHECK_RETURN(image, "[ERROR] Couldn't create depth images for swapchanin: {}");
            sw_depth_images.emplace_back(std::move(image.value()));
        }

        return true;
    }

    auto create_sync_data() -> bool
    {
        VkSemaphoreTypeCreateInfo type_info = 
        {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
            .pNext = nullptr,
            .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
            .initialValue = 0
        };
        VkSemaphoreCreateInfo timeline_ci =
        {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = &type_info,
            .flags = 0
        };
        auto result = vkCreateSemaphore(device, &timeline_ci, nullptr, &timeline);
        VK_CHECK_RETURN(result, "[ERROR] Timeline semaphore creation failed: {}");

        VkSemaphoreCreateInfo binary_ci = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkFenceCreateInfo fence_ci = {. sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };

        for (size_t i = 0; i < FRAMES_FLYING; i++)
        {
            result = vkCreateSemaphore(device, &binary_ci, nullptr, &acquire_sems[i]);
            VK_CHECK_RETURN(result, "[ERROR] Binary semaphore creation failed: {}");

            result = vkCreateSemaphore(device, &binary_ci, nullptr, &render_done_sems[i]);
            VK_CHECK_RETURN(result, "[ERROR] Binary semaphore creation failed: {}");

            result = vkCreateFence(device, &fence_ci, nullptr, &present_fences[i]);
            VK_CHECK_RETURN(result, "[ERROR] Fence creation failed: {}");
        }

        return true;
    }

    auto create_cmd_pool() -> bool
    {    
        VkCommandPoolCreateInfo poolInfo = 
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = gfx_queue.family
        };
        auto result = vkCreateCommandPool(device, &poolInfo, nullptr, &cmd_pool);
        VK_CHECK_RETURN(result, "[ERROR] Command buffer pool creation failed: {}");

        return true;
    }

    auto one_time_submit_start() -> VkCommandBuffer
    {
        VkCommandBufferAllocateInfo allocInfo =
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = cmd_pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        
        VkCommandBuffer commandBuffer;
        vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);
        
        // Begin recording
        VkCommandBufferBeginInfo beginInfo = 
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        vkBeginCommandBuffer(commandBuffer, &beginInfo);
        
        return commandBuffer;
    }

    auto one_time_submit_end(VkCommandBuffer cmd) -> void
    {
        vkEndCommandBuffer(cmd);
        
        VkSubmitInfo submitInfo = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers = &cmd,
        };
        
        vkQueueSubmit(gfx_queue.queue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(gfx_queue.queue);
        vkFreeCommandBuffers(device, cmd_pool, 1, &cmd);
    }

    auto create_transit_cmd_buffers() -> bool
    {
        VkCommandBufferAllocateInfo info = 
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext = nullptr,
            .commandPool = cmd_pool,
            .level = VkCommandBufferLevel::VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = uint32_t(sw_resolve_images.size())
        };
        {
            std::vector<VkCommandBuffer> commandBuffers; commandBuffers.resize(info.commandBufferCount);
            auto result = vkAllocateCommandBuffers(device, &info, commandBuffers.data());
            VK_CHECK_RETURN(result, "[ERROR] Command buffer allocation failed: {}");
            color_cmd_buffers = std::move(commandBuffers);
        }
        {
            std::vector<VkCommandBuffer> commandBuffers; commandBuffers.resize(info.commandBufferCount);
            auto result = vkAllocateCommandBuffers(device, &info, commandBuffers.data());
            VK_CHECK_RETURN(result, "[ERROR] Command buffer allocation failed: {}");
            present_cmd_buffers = std::move(commandBuffers);
        }

        return true;
    }

    auto create_main_cmd_buffers() -> bool
    {
        VkCommandBufferAllocateInfo info = 
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext = nullptr,
            .commandPool = cmd_pool,
            .level = VkCommandBufferLevel::VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = FRAMES_FLYING
        };

        std::vector<VkCommandBuffer> commandBuffers; commandBuffers.resize(info.commandBufferCount);
        auto result = vkAllocateCommandBuffers( device, &info, commandBuffers.data());
        VK_CHECK_RETURN(result, "[ERROR] Command buffer allocation failed: {}");
        main_cmd_buffers = std::move(commandBuffers);

        return true;
    }

    auto record_swapchain_resolve_images_transitions() -> void
    {
        VkCommandBufferBeginInfo cbBeginInfo = { 
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO 
        };
        
        VkImageSubresourceRange common_range = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        };
        
        VkImageMemoryBarrier2 resolve_barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            //.srcStageMask = VK_PIPELINE_STAGE_2_NONE,
            .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, // to avoid sync errors
            .srcAccessMask = VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .subresourceRange = common_range,
        };
        
        for (size_t i = 0; i < color_cmd_buffers.size(); i++) 
        {
            resolve_barrier.image = sw_resolve_images[i].image();
            
            auto cb = color_cmd_buffers[i];
            vkResetCommandBuffer(cb, 0);
            vkBeginCommandBuffer(cb, &cbBeginInfo);
            
            VkDependencyInfo dependencyInfo = {
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .imageMemoryBarrierCount = 1,
                .pImageMemoryBarriers = &resolve_barrier,
            };
            
            vkCmdPipelineBarrier2(cb, &dependencyInfo);
            vkEndCommandBuffer(cb);
        }
        
        // Present transition (unchanged)
        VkImageMemoryBarrier2 present_barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
            .dstAccessMask = VK_ACCESS_2_NONE,
            .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .subresourceRange = common_range,
        };
        
        for (size_t i = 0; i < present_cmd_buffers.size(); i++) 
        {
            present_barrier.image = sw_resolve_images[i].image();
            
            auto cb = present_cmd_buffers[i];
            vkResetCommandBuffer(cb, 0);
            vkBeginCommandBuffer(cb, &cbBeginInfo);
            
            VkDependencyInfo dependencyInfo = {
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .imageMemoryBarrierCount = 1,
                .pImageMemoryBarriers = &present_barrier,
            };
            
            vkCmdPipelineBarrier2(cb, &dependencyInfo);
            vkEndCommandBuffer(cb);
        }
    }

    auto make_swapchain_color_images_transitions() -> void
    {
        auto cmd = one_time_submit_start();
        
        // Prepare barrier structures
        VkImageSubresourceRange color_range = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        };
        
        VkImageSubresourceRange depth_range = {
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        };
        
        VkImageMemoryBarrier2 color_barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext = nullptr,
            .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask = VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .subresourceRange = color_range,
        };
        
        VkImageMemoryBarrier2 depth_barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext = nullptr,
            .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask = VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | 
                            VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            .dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .subresourceRange = depth_range,
        };
        
        // Build barrier array for all MSAA images
        std::vector<VkImageMemoryBarrier2> barriers;
        barriers.reserve(sw_color_images.size() * 2); // color + depth for each
        
        for (size_t i = 0; i < sw_color_images.size(); i++) {
            // Add color attachment barrier
            color_barrier.image = sw_color_images[i].image();
            barriers.push_back(color_barrier);
            
            // Add depth attachment barrier
            depth_barrier.image = sw_depth_images[i].image();
            barriers.push_back(depth_barrier);
        }

        VkDependencyInfo dependencyInfo = {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext = nullptr,
            .dependencyFlags = 0,
            .memoryBarrierCount = 0,
            .pMemoryBarriers = nullptr,
            .bufferMemoryBarrierCount = 0,
            .pBufferMemoryBarriers = nullptr,
            .imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size()),
            .pImageMemoryBarriers = barriers.data(),
        };
        
        vkCmdPipelineBarrier2(cmd, &dependencyInfo);

        one_time_submit_end(cmd);
    }

    auto print_device_info() -> void
    {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(device_physical, &props);
        std::println("------------------------------------------------------------");
        std::println("DEVICE: {} \tVERSION: {}", props.deviceName, props.driverVersion);
        std::println("GRAPHIC QUEUE:\tFAMILY: {} \tID: {}", gfx_queue.family, gfx_queue.id);
        std::println("PRESENT QUEUE:\tFAMILY: {} \tID: {}\n", present_queue.family, present_queue.id);
        std::println("------------------------------------------------------------");
    }

    auto create_resizable_resources() -> bool
    {
        if (!create_swapchain()) return false;
        if (!create_swapchain_images()) return false;
        if (!create_transit_cmd_buffers()) return false;
        record_swapchain_resolve_images_transitions();
        make_swapchain_color_images_transitions();

        return true;
    }

    auto destroy_resizable_resources() -> void
    {
        if (auto& data = color_cmd_buffers; !data.empty())
            vkFreeCommandBuffers(device, cmd_pool, data.size(), data.data());
        color_cmd_buffers.clear();

        if (auto& data = present_cmd_buffers; !data.empty())
            vkFreeCommandBuffers(device, cmd_pool, data.size(), data.data());
        present_cmd_buffers.clear();

        for (auto v : sw_resolve_images_view_unorm)
            vkDestroyImageView(device, v, nullptr);
        sw_resolve_images_view_unorm.clear();

        sw_color_images.clear();
        sw_depth_images.clear();
        sw_resolve_images.clear();

        if (swapchain)
        {
            vkDestroySwapchainKHR(device, swapchain, nullptr);
            swapchain = VK_NULL_HANDLE;
        }
    }

    auto create(size_t width, size_t height, const std::string& name) -> bool
    {
        if (!create_window(width, height, name)) return false;
        if (!create_instance()) return false;
        if (!create_debugger()) return false;
        if (!select_physical_device()) return false;
        if (!create_device_and_queues()) return false;
        if (!create_allocator()) return false;
        if (!create_cmd_pool()) return false;
        if (!create_surface()) return false;
        if (!create_main_cmd_buffers()) return false;

        print_device_info();

        if (!create_resizable_resources()) return false;
        if (!create_sync_data()) return false;

        std::println("[INFO] OneWindow created successfully");
        return true;
    }

    auto recreate_render_done_semaphores() -> bool
    {
        const VkSemaphoreCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        for (auto& s : render_done_sems)
        {
            vkDestroySemaphore(device, s, nullptr);
            s = VK_NULL_HANDLE;
            auto result = vkCreateSemaphore(device, &ci, nullptr, &s);
            VK_CHECK_RETURN(result, "[ERROR] Render done semaphore creation failed: {}");
        }
        return true;
    }

    auto on_resize() -> bool
    {
        if (!device)
            return false;

        int w = 0, h = 0;
        RGFW_window_getSize(window, &w, &h); // window size, NOT get_size()
        if (w == 0 || h == 0)
        {
            needs_resize = true;
            return false;
        }

        vkDeviceWaitIdle(device);

        // Present fences may be signaled and still marked pending; consume them.
        for (uint32_t i = 0; i < FRAMES_FLYING; ++i)
            wait_present_fence(i);

        destroy_resizable_resources();
        if (!recreate_render_done_semaphores()) return false;
        if (!create_resizable_resources()) return false;

        work_cmd_buffers.clear();
        needs_resize = false;

        std::println("[INFO] Swapchain resized to {}x{}", extent.width, extent.height);
        
        return true;
    }

    auto get_size() const -> go::vu2
    {
        return {extent.width, extent.height};
    }

    void wait_timeline(uint64_t value)
    {
        if (value == 0) return;
        VkSemaphoreWaitInfo wi = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
            .semaphoreCount = 1, .pSemaphores = &timeline, .pValues = &value,
        };
        vkWaitSemaphores(device, &wi, UINT64_MAX);
    }

    void wait_present_fence(uint32_t slot)
    {
        if (present_pending[slot])
            vkWaitForFences(device, 1, &present_fences[slot], VK_TRUE, UINT64_MAX);
        vkResetFences(device, 1, &present_fences[slot]);   // always, covers failed presents
        present_pending[slot] = false;
    }

    auto acquire() -> std::optional<AcquireResult>
    {
        wait_timeline(frame_values[frame_id]);
        wait_present_fence(frame_id);
        work_cmd_buffers.clear();
        bool resized_and_waited = false;

        if (needs_resize || swapchain == VK_NULL_HANDLE)
        { 
            if (!on_resize()) return std::nullopt;
            resized_and_waited = true;
        }

        VkResult result;
        while ((result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
                acquire_sems[frame_id], VK_NULL_HANDLE, &image_id)) == VK_ERROR_OUT_OF_DATE_KHR)
        {
            if (!on_resize()) return std::nullopt;
            resized_and_waited = true;
        }
        // VK_SUBOPTIMAL_KHR: the semaphore IS signaled, keep going and resize after present.
        if (result == VK_SUBOPTIMAL_KHR) needs_resize = true;

        return std::optional(
        AcquireResult{
            .image   = image_id,
            .frame   = frame_id,
            .resized_and_waited = resized_and_waited
        });
    }

    auto submit_present() -> bool
    {
        work_cmd_buffers.insert(work_cmd_buffers.begin(), color_cmd_buffers[image_id]);
        work_cmd_buffers.push_back(present_cmd_buffers[image_id]);

        std::vector<VkCommandBufferSubmitInfo> cmds;
        cmds.reserve(work_cmd_buffers.size());
        for (auto cb : work_cmd_buffers)
            cmds.push_back({ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = cb });

        const uint64_t signal_value = submit_counter + 1;

        VkSemaphoreSubmitInfo wait_info = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = acquire_sems[frame_id],
            .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        };
        VkSemaphoreSubmitInfo signal_infos[2] = {
            { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = render_done_sems[frame_id],
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT },
            { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = timeline, .value = signal_value,
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT },
        };

        VkSubmitInfo2 submit = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .waitSemaphoreInfoCount = 1,   .pWaitSemaphoreInfos = &wait_info,
            .commandBufferInfoCount = (uint32_t)cmds.size(), .pCommandBufferInfos = cmds.data(),
            .signalSemaphoreInfoCount = 2, .pSignalSemaphoreInfos = signal_infos,
        };

        if (auto r = vkQueueSubmit2(gfx_queue.queue, 1, &submit, VK_NULL_HANDLE); r != VK_SUCCESS)
        {
            std::println("[ERROR] Submit failed: {}", (int)r);
            return false;
        }
        submit_counter = signal_value;
        frame_values[frame_id] = signal_value;

        VkSwapchainPresentFenceInfoKHR fence_info = {
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_KHR,
            .swapchainCount = 1,
            .pFences = &present_fences[frame_id],
        };
        VkPresentInfoKHR present_info = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .pNext = &fence_info,
            .waitSemaphoreCount = 1, .pWaitSemaphores = &render_done_sems[frame_id],
            .swapchainCount = 1, .pSwapchains = &swapchain, .pImageIndices = &image_id,
        };
        const auto result = vkQueuePresentKHR(present_queue.queue, &present_info);

        // Only trust the fence if the present was actually accepted.
        present_pending[frame_id] = (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR);

        if (result == VK_SUBOPTIMAL_KHR || result == VK_ERROR_OUT_OF_DATE_KHR)
            needs_resize = true;

        frame_id = (frame_id + 1) % FRAMES_FLYING;
        return result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR;
    }

    auto check_events(bool wait_events) -> void 
    {
        if (wait_events) RGFW_waitForEvent(RGFW_eventWaitNext);
        for (RGFW_event event; RGFW_window_checkEvent(window, &event););
    }

    ~Impl () 
    {
        if (device)
            vkDeviceWaitIdle(device);

        for (auto semaphore : acquire_sems)
            vkDestroySemaphore(device, semaphore, nullptr);
        acquire_sems = {};

        for (auto semaphore : render_done_sems)
            vkDestroySemaphore(device, semaphore, nullptr);
        render_done_sems = {};

        for (auto fence : present_fences)
            vkDestroyFence(device, fence, nullptr);
        present_fences = {};

        if (timeline != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(device, timeline, nullptr);
            timeline = VK_NULL_HANDLE;
        }

        destroy_resizable_resources();

        if (auto& data = main_cmd_buffers; !data.empty())
            vkFreeCommandBuffers(device, cmd_pool, data.size(), data.data());
        main_cmd_buffers.clear();

        if (cmd_pool)
            vkDestroyCommandPool(device, cmd_pool, nullptr);

        if (surface)
        {
            vkDestroySurfaceKHR(instance, surface, nullptr);
            surface = VK_NULL_HANDLE;
        }

        if (allocator)
        {
            vmaDestroyAllocator(allocator);
            allocator = VK_NULL_HANDLE;
        }

        if (device)
        {
            vkDestroyDevice(device, nullptr);
            device = VK_NULL_HANDLE;
        }

        if (debugger)
        {
            auto fn =(PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
            fn(instance, debugger, nullptr);
            debugger = VK_NULL_HANDLE;
        }

        if (instance)
        {
            vkDestroyInstance(instance, nullptr);
            instance = VK_NULL_HANDLE;
        }

        if (window)
        {
            RGFW_window_close(window);
            window = nullptr;
        }
    }
};

// For pimpl
engi::OneWindow::~OneWindow() noexcept = default;
engi::OneWindow::OneWindow(OneWindow&&) noexcept = default;
auto engi::OneWindow::operator=(OneWindow&&) noexcept -> OneWindow& = default;

auto engi::OneWindow::create(size_t width, size_t height, const std::string& name) -> std::optional<OneWindow>
{
    OneWindow w;
    w.m_impl = std::make_unique<Impl>();
    if (!w.m_impl->create(width, height, name))
        return std::nullopt;
    return w;
}

auto engi::OneWindow::should_close() const -> bool { return RGFW_window_shouldClose(m_impl->window); }
auto engi::OneWindow::acquire() -> std::optional<AcquireResult> { return m_impl->acquire(); }
auto engi::OneWindow::submit_present() -> bool { return m_impl->submit_present(); }
auto engi::OneWindow::check_events(bool wait_events) -> void { return m_impl->check_events(wait_events); }

auto engi::OneWindow::cmd_start() -> VkCommandBuffer
{
    auto cmd = m_impl->main_cmd_buffers[m_impl->frame_id];
    VkCommandBufferBeginInfo info = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    vkResetCommandBuffer(cmd, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
    vkBeginCommandBuffer(cmd, &info);

    return cmd;
}

auto engi::OneWindow::cmd_end() -> void
{
    auto cmd = m_impl->main_cmd_buffers[m_impl->frame_id];
    vkEndCommandBuffer(cmd);
    m_impl->work_cmd_buffers.push_back(cmd);
}

auto engi::OneWindow::draw_start(VkCommandBuffer cmd, go::vf4 srgba_bg) -> void
{   
    draw_start(cmd, { {0, 0}, m_impl->extent }, srgba_bg);
}

auto engi::OneWindow::draw_start(VkCommandBuffer cmd, const VkRect2D& view, go::vf4 srgba_bg) -> void
{   
    auto lcolor = go::srgba_to_linear(srgba_bg);
    VkRenderingAttachmentInfo colorAttachment = 
    {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = nullptr,
        .imageView = m_impl->sw_color_images[m_impl->image_id].view(),
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .resolveMode = VkResolveModeFlagBits::VK_RESOLVE_MODE_AVERAGE_BIT,
        .resolveImageView = m_impl->sw_resolve_images[m_impl->image_id].view(), 
        .resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VkAttachmentLoadOp::VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VkAttachmentStoreOp::VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = { .color = {lcolor[0], lcolor[1], lcolor[2], lcolor[3]} }
    };
    VkRenderingAttachmentInfo depthAttachment = 
    {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = nullptr,
        .imageView = m_impl->sw_depth_images[m_impl->image_id].view(),
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .resolveMode = VkResolveModeFlagBits::VK_RESOLVE_MODE_NONE,
        .resolveImageView = VK_NULL_HANDLE, 
        .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .loadOp = VkAttachmentLoadOp::VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VkAttachmentStoreOp::VK_ATTACHMENT_STORE_OP_DONT_CARE,
        //.clearValue = { .depthStencil = {.depth = 1.0f, .stencil = 0} } // default-Z
        .clearValue = { .depthStencil = {.depth = 0.0f, .stencil = 0} } // reverse-Z
    };

    VkRenderingInfo renderingInfo = 
    {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .pNext = nullptr,
        .flags = 0,
        .renderArea = view,
        .layerCount = 1, 
        .viewMask = 0,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
        .pDepthAttachment = &depthAttachment, 
        .pStencilAttachment = nullptr
    };

    vkCmdBeginRendering(cmd, &renderingInfo);
    view_set(cmd, view);
}

// Generic draw_start: control depth, multisampling and load/store ops.
auto engi::OneWindow::draw_start(
    VkCommandBuffer cmd,
    const VkRect2D& view,
    go::vf4 srgba_bg,
    bool enable_depth,
    bool enable_msaa,
    VkAttachmentLoadOp colorLoadOp,
    VkAttachmentStoreOp colorStoreOp,
    VkAttachmentLoadOp depthLoadOp,
    VkAttachmentStoreOp depthStoreOp) -> void
{
    VkImageView colorView = VK_NULL_HANDLE;
    VkImageView resolveView = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;

    if (enable_msaa)
    {
        colorView = m_impl->sw_color_images[m_impl->image_id].view();
        resolveView = m_impl->sw_resolve_images[m_impl->image_id].view();
    }
    else
    {
        colorView = m_impl->sw_resolve_images[m_impl->image_id].view();
        resolveView = VK_NULL_HANDLE;
    }

    if (enable_depth)
        depthView = m_impl->sw_depth_images[m_impl->image_id].view();
    else
        depthView = VK_NULL_HANDLE;

    draw_start(cmd, view, srgba_bg,
               colorView, depthView, resolveView, enable_msaa,
               colorLoadOp, colorStoreOp, depthLoadOp, depthStoreOp);
}

// Explicit image-view draw_start: use provided image views directly.
auto engi::OneWindow::draw_start(
    VkCommandBuffer cmd,
    const VkRect2D& view,
    go::vf4 srgba_bg,
    VkImageView color_view,
    VkImageView depth_view,
    VkImageView resolve_view,
    bool enable_msaa,
    VkAttachmentLoadOp colorLoadOp,
    VkAttachmentStoreOp colorStoreOp,
    VkAttachmentLoadOp depthLoadOp,
    VkAttachmentStoreOp depthStoreOp) -> void
{
    VkViewport viewport = 
    {
        .x = (float)view.offset.x,
        .y = (float)view.offset.y,
        .width = (float)view.extent.width,
        .height = (float)view.extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };
    auto lcolor = go::srgba_to_linear(srgba_bg);
    VkRenderingAttachmentInfo colorAttachment = 
    {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = nullptr,
        .imageView = color_view,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .resolveMode = (enable_msaa && resolve_view != VK_NULL_HANDLE) ? VkResolveModeFlagBits::VK_RESOLVE_MODE_AVERAGE_BIT : VkResolveModeFlagBits::VK_RESOLVE_MODE_NONE,
        .resolveImageView = (enable_msaa ? resolve_view : VK_NULL_HANDLE), 
        .resolveImageLayout = (enable_msaa ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED),
        .loadOp = colorLoadOp,
        .storeOp = colorStoreOp,
        .clearValue = { .color = {lcolor[0], lcolor[1], lcolor[2], lcolor[3]} }
    };

    VkRenderingAttachmentInfo depthAttachment = 
    {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = nullptr,
        .imageView = depth_view,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .resolveMode = VkResolveModeFlagBits::VK_RESOLVE_MODE_NONE,
        .resolveImageView = VK_NULL_HANDLE, 
        .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .loadOp = depthLoadOp,
        .storeOp = depthStoreOp,
        .clearValue = { .depthStencil = {.depth = 0.0f, .stencil = 0} } // reverse-Z
    };

    VkRenderingInfo renderingInfo = 
    {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .pNext = nullptr,
        .flags = 0,
        .renderArea = view,
        .layerCount = 1, 
        .viewMask = 0,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
        .pDepthAttachment = depth_view != VK_NULL_HANDLE ? &depthAttachment : nullptr, 
        .pStencilAttachment = nullptr
    };

    vkCmdBeginRendering(cmd, &renderingInfo);
    view_set(cmd, view);
}

auto engi::OneWindow::view_set(VkCommandBuffer cmd, const VkRect2D& view) -> void
{
    viewport_set(cmd, view);
    scissors_set(cmd, view);
}

auto engi::OneWindow::scissors_set(VkCommandBuffer cmd, const VkRect2D& view) -> void
{
    vkCmdSetScissor(cmd, 0, 1, &view);
}

auto engi::OneWindow::viewport_set(VkCommandBuffer cmd, const VkRect2D& view) -> void
{
    VkViewport viewport = 
    {
        .x = (float)view.offset.x,
        .y = (float)view.offset.y,
        .width = (float)view.extent.width,
        .height = (float)view.extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
}

auto engi::OneWindow::draw_end(VkCommandBuffer cmd) -> void
{
    vkCmdEndRendering(cmd);
}

auto engi::OneWindow::wait_idle() -> void
{
    if (m_impl->device) vkDeviceWaitIdle(m_impl->device);
}

auto engi::OneWindow::add_overlay_barrier(VkCommandBuffer cmd) -> void
{
    VkMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT
    };
    VkDependencyInfo dependencyInfo = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = 1,
        .pMemoryBarriers = &barrier
    };
    vkCmdPipelineBarrier2(cmd, &dependencyInfo);
}

auto engi::OneWindow::get_size() const -> go::vu2 { return m_impl->get_size(); }
auto engi::OneWindow::get_instance() const -> VkInstance { return m_impl->instance; }
auto engi::OneWindow::get_device() const -> VkDevice { return m_impl->device; }
auto engi::OneWindow::get_physical_device() const -> VkPhysicalDevice { return m_impl->device_physical; }
auto engi::OneWindow::get_allocator() const -> VmaAllocator { return m_impl->allocator; }
auto engi::OneWindow::get_queue_graphics() const -> VkQueue { return m_impl->gfx_queue.queue; }
auto engi::OneWindow::get_queue_graphics_family() const -> uint32_t { return m_impl->gfx_queue.family; }
auto engi::OneWindow::get_queue_present() const -> VkQueue { return m_impl->present_queue.queue; }
auto engi::OneWindow::get_queue_present_family() const -> uint32_t { return m_impl->present_queue.family; }
auto engi::OneWindow::get_command_pool() const -> VkCommandPool { return m_impl->cmd_pool; }
auto engi::OneWindow::get_color_format() const -> VkFormat { return m_impl->color_format; }
auto engi::OneWindow::get_depth_format() const -> VkFormat { return m_impl->depth_format; }
