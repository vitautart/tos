#include <memory>
#include <array>

#include <vulkan/vulkan.h>

namespace engi
{
    constexpr size_t FRAMES_FLYING = 2;

    auto constexpr static get_instance_layers() noexcept
    {
#ifdef ENGI_RENDER_DEBUG
        return std::array<const char*, 1>{"VK_LAYER_KHRONOS_validation"};
#else
        return std::array<const char*, 0>{};
#endif
    }

    auto constexpr static get_instance_extensions() noexcept
    {
        return std::array
        {
            VK_KHR_SURFACE_EXTENSION_NAME,
            "VK_KHR_xlib_surface",
            VK_EXT_DEBUG_UTILS_EXTENSION_NAME
        };
    }

    auto constexpr static get_device_layers() noexcept
    {
        return std::array<const char*, 0>{}; 
    }

    auto constexpr static get_device_extensions() noexcept
    {
        return  std::array
        {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            VK_EXT_DESCRIPTOR_HEAP_EXTENSION_NAME,
            VK_KHR_SHADER_UNTYPED_POINTERS_EXTENSION_NAME,
        };
    }

    struct DeviceFeatures
    {
        VkPhysicalDeviceFeatures2 features2{};
        VkPhysicalDeviceVulkan12Features features12{};
        VkPhysicalDeviceVulkan13Features features13{};
        VkPhysicalDeviceVulkan14Features features14{};
        VkPhysicalDeviceShaderUntypedPointersFeaturesKHR shader_untyped_pointers{};
        VkPhysicalDeviceDescriptorHeapFeaturesEXT descriptor_heap{};

        static auto create() -> std::unique_ptr<DeviceFeatures>
        {
            auto features = std::make_unique<DeviceFeatures>();
            features->features2 = 
            { 
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                .pNext = &features->features12,
                .features =
                {
                    .multiDrawIndirect = VK_TRUE,
                    .fillModeNonSolid = VK_TRUE,
                    .wideLines = VK_TRUE,
                    .samplerAnisotropy = VK_TRUE,
                    .vertexPipelineStoresAndAtomics = VK_TRUE,
                }
            };
            features->features12 = 
            {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
                .pNext = &features->features13,
                .bufferDeviceAddress = VK_TRUE
            };
            features->features13 = 
            {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
                .pNext = &features->features14,
                .synchronization2 = VK_TRUE,
                .dynamicRendering = VK_TRUE
            };
            features->features14 = 
            {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
                .pNext = &features->shader_untyped_pointers,
                .maintenance5 = VK_TRUE
            };
            features->shader_untyped_pointers = 
            {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_UNTYPED_POINTERS_FEATURES_KHR,
                .pNext = &features->descriptor_heap,
                .shaderUntypedPointers = VK_TRUE
            };
            features->descriptor_heap = 
            {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_FEATURES_EXT,
                .pNext = nullptr,
                .descriptorHeap = VK_TRUE
            };

            return features;
        }
    };
}