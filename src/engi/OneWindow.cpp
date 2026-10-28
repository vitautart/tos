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

#define VK_CHECK_RETURN(result, msg) if (result != VK_SUCCESS) { std::println(msg, (int)result); return false; } 
#define OPT_CHECK_RETURN(opt, msg) if (!opt.has_value()) { std::println(msg); return false; } 
#define EXP_CHECK_RETURN(opt, msg) if (!opt.has_value()) { std::println(msg, (int)opt.error()); return false; }

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

auto constexpr static get_depth_format() noexcept
{
    return std::array
    {
        VkFormat::VK_FORMAT_D32_SFLOAT,
        VkFormat::VK_FORMAT_D32_SFLOAT_S8_UINT
    };
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
    std::vector<engi::Image> sw_depth_images;

    std::vector<VkSemaphore> image_semaphores;
    std::vector<VkSemaphore> command_semaphores;
    std::vector<VkFence> command_fences;

    VkCommandPool cmd_pool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> color_cmd_buffers;
    std::vector<VkCommandBuffer> present_cmd_buffers;

    std::vector<VkCommandBuffer> main_cmd_buffers;
    std::vector<VkCommandBuffer> work_cmd_buffers;


    uint32_t image_id = 0;
    uint32_t frame_id = 0;

    VkExtent2D extent = { 0, 0 };

    //bool m_pending_resize = false;

    auto create_window(size_t width, size_t height, const std::string& name) -> bool
    {
        window = RGFW_createWindow(name.c_str(), 0, 0, width, height, RGFW_windowCenter | RGFW_windowNoResize);
        //window = RGFW_createWindow(name.c_str(), 0, 0, width, height, RGFW_windowCenter);
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
                auto formats = get_depth_format();
                
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
            .flags = 0,
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
        for (auto f : formats)
        std::println ("Format: {} {}", (int)f.format, (int)f.colorSpace);
        auto opt_format = engi::find_if(formats, [](VkSurfaceFormatKHR f) 
        { 
            //return f.format == VK_FORMAT_B8G8R8A8_SRGB && 
            return f.format == VK_FORMAT_B8G8R8A8_UNORM && 
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
            int width = 0, height = 0;
            RGFW_window_getSize(win, &width, &height);
            if (width == 0 || height == 0)
                return std::nullopt;

            if (cap.currentExtent.width != UINT32_MAX)
                return cap.currentExtent;

            return VkExtent2D
            { 
                .width = std::clamp(uint32_t(width), cap.minImageExtent.width, cap.maxImageExtent.width),
                .height = std::clamp(uint32_t(height), cap.minImageExtent.height, cap.maxImageExtent.height)
            };
        };

        auto opt_extent = get_extent(cap, window);
        OPT_CHECK_RETURN(opt_extent, "[ERROR] Couldn't find max extents");
        extent = opt_extent.value();

        bool gfx_present_same = gfx_queue.family == present_queue.family;
        auto indices = std::array{gfx_queue.family, present_queue.family};

        VkSwapchainCreateInfoKHR info = 
        {
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .pNext = nullptr,
            .flags = 0,
            .surface = surface,
            .minImageCount = std::clamp(cap.minImageCount+1, cap.minImageCount,
                cap.maxImageCount > 0 ? cap.maxImageCount : 256),
            .imageFormat = color_format,
            .imageColorSpace = color_space,
            .imageExtent = extent,
            .imageArrayLayers = 1,
            .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT 
                | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT, // needed for skia renderer
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
        VkSemaphoreCreateInfo info_sem = 
        {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0
        };

        VkFenceCreateInfo info_fen = 
        {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT
        };

        if (image_semaphores.empty())
        {
            for (size_t i = 0; i < FRAMES_FLYING; i++)
            {
                VkSemaphore item;
                auto result = vkCreateSemaphore(device, &info_sem, nullptr, &item);
                VK_CHECK_RETURN(result, "[ERROR] Semaphore creation failed: {}");
                image_semaphores.push_back(item);
            }
        }

        if (command_fences.empty())
        {
            for (size_t i = 0; i < FRAMES_FLYING; i++)
            {
                VkFence item;
                auto result = vkCreateFence(device, &info_fen, nullptr, &item);
                VK_CHECK_RETURN(result, "[ERROR] Fence creation failed: {}");
                command_fences.push_back(item);
            }
        }

        if (command_semaphores.empty())
        {
            for (const auto& _ : sw_resolve_images)
            {
                VkSemaphore item;
                auto result = vkCreateSemaphore(device, &info_sem, nullptr, &item);
                VK_CHECK_RETURN(result, "[ERROR] Semaphore creation failed: {}");
                command_semaphores.push_back(item);
            }
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
        if (!create_sync_data()) return false;
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

        for (auto semaphore : command_semaphores)
            vkDestroySemaphore(device, semaphore, nullptr);
        command_semaphores.clear();

        sw_color_images.clear();
        sw_depth_images.clear();
        sw_resolve_images.clear();

        if (swapchain)
        {
            vkDestroySwapchainKHR(device, swapchain, nullptr);
            swapchain = VK_NULL_HANDLE;
        }

        //ins.win.work_cmd_buffers.clear();
        //ins.win.image_id = 0;
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

        std::println("[INFO] OneWindow created successfully");
        return true;
    }

    auto on_resize() -> bool
    {
        if (!device)
            return false;

        vkDeviceWaitIdle(device);

        auto [width, height] = get_size(); 
        
        if (width == 0 || height == 0)
            return false;

        destroy_resizable_resources();
        if (!create_resizable_resources())
            return false;

        work_cmd_buffers.clear();
        image_id = 0;
        frame_id = 0;

        std::println("[INFO] Swapchain resized to {}x{}", width, height);
        
        return true;
    }

    auto get_size() const -> std::pair<int, int>
    {
        int width = 0, height = 0;
        RGFW_window_getSize(window, &width, &height);
        return { width, height };
    }

    auto acquire() -> AcquireResult
    {
        const auto cmd_fence = command_fences[frame_id];
        vkWaitForFences(device, 1, &cmd_fence, VK_TRUE, UINT64_MAX);

        // Clear and push dummy cmd for later submit
        // This dummy cmd will be used later after aquire (with aquired image id), 
        // to push actuall color_cmd_buffer for image transition.
        work_cmd_buffers.clear();
        work_cmd_buffers.push_back(VK_NULL_HANDLE);

        const auto img_semaphore = image_semaphores[frame_id];
        
        auto result = vkAcquireNextImageKHR(device, swapchain, 
                UINT64_MAX, img_semaphore, nullptr, &image_id);

        //if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            on_resize();
            return acquire(); // recursive call to acquire again after resize
        }

        return 
        {
            .image = image_id,
            .frame = frame_id,
            .result = result,
            .vkImage    = sw_resolve_images[image_id].image(),
            .format     = color_format,
            .width      = int(extent.width),
            .height     = int(extent.height),
        };
    }

    auto submit_start() -> std::optional<VkSemaphore> 
    { 
        if (work_cmd_buffers.empty())
        {
            std::println("[WARNING] Submit was called without wait_frame being called first");
            return std::nullopt; // nothing to submit, skip sync and present
        }
        const auto cmd_semaphore = command_semaphores[image_id];
        const auto img_semaphore = image_semaphores[frame_id];

        work_cmd_buffers[0] = color_cmd_buffers[image_id];

        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

        VkSubmitInfo submit_info =
        {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &img_semaphore,
            .pWaitDstStageMask = waitStages,
            .commandBufferCount = (uint32_t)work_cmd_buffers.size(),
            .pCommandBuffers = work_cmd_buffers.data(),
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &cmd_semaphore
        };
        auto result = vkQueueSubmit(gfx_queue.queue, 1, &submit_info, VK_NULL_HANDLE);

        if (result != VK_SUCCESS)
        {
            std::println("[ERROR] Submit failed: {}", (int)result);
            return std::nullopt;
        }

        return cmd_semaphore;
    }

    auto submit_end(VkSemaphore wait_semaphore) -> bool 
    { 
        const auto cmd_fence = command_fences[frame_id];
        const auto cmd_semaphore = command_semaphores[image_id]; //TODO: check if it is ok if used the same as in submit_start

        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_ALL_COMMANDS_BIT};
        vkResetFences(device, 1, &cmd_fence);

        VkSubmitInfo submit_info =
        {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &wait_semaphore,
            .pWaitDstStageMask = waitStages,
            .commandBufferCount = 1,
            .pCommandBuffers = &present_cmd_buffers[image_id],
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &cmd_semaphore
        };
        auto result = vkQueueSubmit(gfx_queue.queue, 1, &submit_info, cmd_fence);
        VK_CHECK_RETURN(result, "[ERROR] Submit failed: {}");

        VkPresentInfoKHR present_info = 
        {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .pNext = nullptr,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &cmd_semaphore,
            .swapchainCount = 1,
            .pSwapchains = &swapchain,
            .pImageIndices = &image_id
        };
        result = vkQueuePresentKHR(present_queue.queue, &present_info);

        frame_id = (frame_id + 1) % FRAMES_FLYING;

        //return (result == VK_SUBOPTIMAL_KHR) || (result == VK_SUCCESS);
        return result == VK_SUCCESS;
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

        destroy_resizable_resources();

        if (auto& data = main_cmd_buffers; !data.empty())
            vkFreeCommandBuffers(device, cmd_pool, data.size(), data.data());
        main_cmd_buffers.clear();

        if (cmd_pool)
            vkDestroyCommandPool(device, cmd_pool, nullptr);

        for (auto semaphore : image_semaphores)
            vkDestroySemaphore(device, semaphore, nullptr);
        image_semaphores.clear();

        for (auto fence : command_fences)
            vkDestroyFence(device, fence, nullptr);
        command_fences.clear();

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
auto engi::OneWindow::acquire() -> AcquireResult { return m_impl->acquire(); }
auto engi::OneWindow::submit_start() -> std::optional<VkSemaphore> { return m_impl->submit_start(); }
auto engi::OneWindow::submit_end(VkSemaphore wait_semaphore) -> bool { return m_impl->submit_end(wait_semaphore); }
auto engi::OneWindow::check_events(bool wait_events) -> void { return m_impl->check_events(wait_events); }

auto engi::OneWindow::cmd_start() -> VkCommandBuffer
{
    if (m_impl->work_cmd_buffers.empty())
        std::println("[WARNING] Command buffer recording was started without wait_frame being called first");

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
        .flags = 0, // TODO: maybe here can be something usefull
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

auto engi::OneWindow::get_size() const -> std::pair<int, int> { return m_impl->get_size(); }
auto engi::OneWindow::get_instance() const -> VkInstance { return m_impl->instance; }
auto engi::OneWindow::get_device() const -> VkDevice { return m_impl->device; }
auto engi::OneWindow::get_physical_device() const -> VkPhysicalDevice { return m_impl->device_physical; }
auto engi::OneWindow::get_allocator() const -> VmaAllocator { return m_impl->allocator; }
auto engi::OneWindow::get_queue_graphics() const -> VkQueue { return m_impl->gfx_queue.queue; }
auto engi::OneWindow::get_queue_graphics_family() const -> uint32_t { return m_impl->gfx_queue.family; }
auto engi::OneWindow::get_queue_present() const -> VkQueue { return m_impl->present_queue.queue; }
auto engi::OneWindow::get_queue_present_family() const -> uint32_t { return m_impl->present_queue.family; }