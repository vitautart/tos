#include <print>

#include <vk_mem_alloc.h>

#include <engi/SkiaRenderer.hpp>
#include <engi/DeviceFeatures.hpp>

#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkPaint.h"
#include "include/core/SkSurface.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMgr.h"
#include "include/ports/SkFontMgr_fontconfig.h"
#include "include/ports/SkFontScanner_FreeType.h"
#include "include/gpu/graphite/Context.h"
#include "include/gpu/graphite/ContextOptions.h"
#include "include/gpu/graphite/Recorder.h"
#include "include/gpu/graphite/Surface.h"
#include "include/gpu/graphite/vk/VulkanGraphiteContext.h"
#include "include/gpu/graphite/vk/VulkanGraphiteTypes.h"
#include "include/gpu/vk/VulkanBackendContext.h"
#include "include/gpu/vk/VulkanExtensions.h"
#include "include/gpu/vk/VulkanMutableTextureState.h"

#include "include/gpu/vk/VulkanMemoryAllocator.h"

#include <engi/Image.hpp>

namespace
{
const VkFormat SKIA_COLOR_FORMAT = VK_FORMAT_B8G8R8A8_UNORM;

auto to_sk_color_type(VkFormat f) -> std::optional<SkColorType>
{
    switch (f) {
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB: return kBGRA_8888_SkColorType;
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB: return kSRGBA_8888_SkColorType;
        default:                      return std::nullopt;
    }
}

auto one_time_submit_start(VkCommandPool cmd_pool, VkDevice device) -> VkCommandBuffer
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

auto one_time_submit_end(VkCommandBuffer cmd, VkCommandPool cmd_pool, VkDevice device, VkQueue queue) -> void
{
    vkEndCommandBuffer(cmd);
    
    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
    };
    
    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);
    vkFreeCommandBuffers(device, cmd_pool, 1, &cmd);
}

auto image_transitions(const std::vector<engi::Image>& images, VkCommandPool cmd_pool, VkDevice device, VkQueue queue) -> void
{
    auto cmd = one_time_submit_start(cmd_pool, device);
    
    // Prepare barrier structures
    VkImageSubresourceRange color_range = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
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
        //.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .subresourceRange = color_range,
    };
    
    // Build barrier array for all MSAA images
    std::vector<VkImageMemoryBarrier2> barriers;
    
    for (size_t i = 0; i < images.size(); i++)
    {
        color_barrier.image = images[i].image();
        barriers.push_back(color_barrier);
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

    one_time_submit_end(cmd, cmd_pool, device, queue);
}
}

struct engi::SkiaSurface::Impl
{
    std::vector<engi::Image> images;
    std::vector<sk_sp<SkSurface>> surfaces;

    auto init(VkDevice device, VmaAllocator alloc, VkCommandPool cmd_pool, VkQueue queue,
         skgpu::graphite::Recorder* recorder, uint32_t width, uint32_t height, uint32_t queue_family) -> bool
    {
        VkFormat format = SKIA_COLOR_FORMAT;
        VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT 
        | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT
        | VK_IMAGE_USAGE_2_SAMPLED_BIT_KHR;
        VkImageCreateInfo image_info = 
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .imageType = VkImageType::VK_IMAGE_TYPE_2D,
            .format = format,
            .extent = { width, height, 1}, 
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VkImageTiling::VK_IMAGE_TILING_OPTIMAL,
            .usage = usage,
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
            .image = VK_NULL_HANDLE, // will be filled inside engi::Image::create
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = format,
            .components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, 
                VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY},
            .subresourceRange = 
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };

        const auto colorType = to_sk_color_type(format);
        if (!colorType) return false;

        for (size_t i = 0; i < FRAMES_FLYING; i++)
        {
            auto image = engi::Image::create(image_info, view_info, alloc, device);
            EXP_CHECK_RETURN(image, "[ERROR] Couldn't create resolve images for swapchanin: {}");
            images.emplace_back(std::move(image.value()));
        }

        image_transitions(images, cmd_pool, device, queue);
 
        for (const auto& image : images)
        {
            skgpu::graphite::VulkanTextureInfo info;
            info.fSampleCount     = skgpu::graphite::SampleCount::k1;
            info.fMipmapped       = skgpu::Mipmapped::kNo;
            info.fFlags           = 0;
            info.fFormat          = format;
            info.fImageTiling     = VK_IMAGE_TILING_OPTIMAL;
            info.fImageUsageFlags = usage;
            info.fSharingMode     = VK_SHARING_MODE_EXCLUSIVE;
            info.fAspectMask      = VK_IMAGE_ASPECT_COLOR_BIT;

            auto tex = skgpu::graphite::BackendTextures::MakeVulkan(
                    {(int)width, (int)height}, info, 
                    VK_IMAGE_LAYOUT_GENERAL,
                    queue_family, image.image(), skgpu::VulkanAlloc());

            auto surface = SkSurfaces::WrapBackendTexture(recorder, tex, SkColorSpace::MakeSRGB(), nullptr);
            if (!surface) return false;
            surfaces.emplace_back(std::move(surface));
        }

        return true;
    }
};

engi::SkiaSurface::SkiaSurface(SkiaSurface&&) noexcept = default;
auto engi::SkiaSurface::operator=(SkiaSurface&&) noexcept -> SkiaSurface& = default;
engi::SkiaSurface::~SkiaSurface() noexcept = default;

auto engi::SkiaSurface::image(uint32_t frame_id) const noexcept -> VkImage { return m_impl->images[frame_id].image(); }
auto engi::SkiaSurface::view(uint32_t frame_id) const noexcept -> VkImageView { return m_impl->images[frame_id].view(); }
auto engi::SkiaSurface::format() const noexcept -> VkFormat { return SKIA_COLOR_FORMAT; }

// Implementation based on skia's source src/gpu/vk/vulkanmemoryallocator/VulkanAMDMemoryAllocator.cpp
class VmaSkiaAllocator : public skgpu::VulkanMemoryAllocator {
public:
    static sk_sp<skgpu::VulkanMemoryAllocator> create(VkInstance instance, VkPhysicalDevice physicalDevice,
        VkDevice device, bool threadSafe)
    {
        VmaAllocatorCreateInfo info;
        info.flags = 0;
        if (threadSafe == false) {
            info.flags |= VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT;
        }
        // Dedicated allocation is always available since Vulkan 1.1 is the minimum requirement.
        info.flags |= VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT;

        info.physicalDevice = physicalDevice;
        info.device = device;
        // 4MB was picked for the size here by looking at memory usage of Android apps and runs of DM.
        // It seems to be a good compromise of not wasting unused allocated space and not making too
        // many small allocations. The AMD allocator will start making blocks at 1/8 the max size and
        // builds up block size as needed before capping at the max set here.
        constexpr size_t kBlockSize = 4 * 1024 * 1024;
        info.preferredLargeHeapBlockSize = kBlockSize;
        info.pAllocationCallbacks = nullptr;
        info.pDeviceMemoryCallbacks = nullptr;
        info.pHeapSizeLimit = nullptr;
        info.pVulkanFunctions = nullptr;
        info.instance = instance;
        info.vulkanApiVersion = VK_API_VERSION_1_4;
        info.pTypeExternalMemoryHandleTypes = nullptr;

        VmaAllocator allocator;
        vmaCreateAllocator(&info, &allocator);

        return sk_sp<VmaSkiaAllocator>(new VmaSkiaAllocator(allocator, true));
    }

    static sk_sp<skgpu::VulkanMemoryAllocator> create(VmaAllocator allocator)
    {
        return sk_sp<VmaSkiaAllocator>(new VmaSkiaAllocator(allocator, false));
    }


    ~VmaSkiaAllocator() override
    {
        if (free_allocator) vmaDestroyAllocator(fAllocator);
        fAllocator = VK_NULL_HANDLE;
    }

    VkResult allocateImageMemory(VkImage image,
                                 uint32_t allocationPropertyFlags,
                                 skgpu::VulkanBackendMemory*) override;

    VkResult allocateBufferMemory(VkBuffer buffer,
                                  BufferUsage usage,
                                  uint32_t allocationPropertyFlags,
                                  skgpu::VulkanBackendMemory*) override;

    void freeMemory(const skgpu::VulkanBackendMemory&) override;

    void getAllocInfo(const skgpu::VulkanBackendMemory&, skgpu::VulkanAlloc*) const override;

    VkResult mapMemory(const skgpu::VulkanBackendMemory&, void** data) override;
    void unmapMemory(const skgpu::VulkanBackendMemory&) override;

    VkResult flushMemory(const skgpu::VulkanBackendMemory&,
                         VkDeviceSize offset,
                         VkDeviceSize size) override;
    VkResult invalidateMemory(const skgpu::VulkanBackendMemory&,
                              VkDeviceSize offset,
                              VkDeviceSize size) override;

    std::pair<uint64_t, uint64_t> totalAllocatedAndUsedMemory() const override;

private:
    VmaSkiaAllocator(VmaAllocator allocator, bool free_allocator) : fAllocator(allocator), free_allocator(free_allocator) {}
private:
    VmaAllocator fAllocator;
    bool free_allocator = false;
};

VkResult VmaSkiaAllocator::allocateImageMemory(VkImage image,
                                                       uint32_t allocationPropertyFlags,
                                                       skgpu::VulkanBackendMemory* backendMemory) 
{
    VmaAllocationCreateInfo info = {};
    info.flags = 0;
    info.usage = VMA_MEMORY_USAGE_UNKNOWN;
    info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    info.preferredFlags = 0;
    info.memoryTypeBits = 0;
    info.pool = VK_NULL_HANDLE;
    info.pUserData = nullptr;

    if (kDedicatedAllocation_AllocationPropertyFlag & allocationPropertyFlags) {
        info.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    }
    if (kLazyAllocation_AllocationPropertyFlag & allocationPropertyFlags) {
        info.requiredFlags |= VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
    }
    if (kProtected_AllocationPropertyFlag & allocationPropertyFlags) {
        info.requiredFlags |= VK_MEMORY_PROPERTY_PROTECTED_BIT;
    }

    VmaAllocation allocation;
    VkResult result = vmaAllocateMemoryForImage(fAllocator, image, &info, &allocation, nullptr);
    if (VK_SUCCESS == result) {
        *backendMemory = (skgpu::VulkanBackendMemory)allocation;
    }
    return result;
}

VkResult VmaSkiaAllocator::allocateBufferMemory(VkBuffer buffer,
                                                        BufferUsage usage,
                                                        uint32_t allocationPropertyFlags,
                                                        skgpu::VulkanBackendMemory* backendMemory) 
{
    VmaAllocationCreateInfo info = {};
    info.flags = 0;
    info.usage = VMA_MEMORY_USAGE_UNKNOWN;
    info.memoryTypeBits = 0;
    info.pool = VK_NULL_HANDLE;
    info.pUserData = nullptr;

    switch (usage) {
        case BufferUsage::kGpuOnly:
            info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            info.preferredFlags = 0;
            break;
        case BufferUsage::kCpuWritesGpuReads:
            // When doing cpu writes and gpu reads the general rule of thumb is to use coherent
            // memory. Though this depends on the fact that we are not doing any cpu reads and the
            // cpu writes are sequential. For sparse writes we'd want cpu cached memory, however we
            // don't do these types of writes in Skia.
            //
            // TODO: In the future there may be times where specific types of memory could benefit
            // from a coherent and cached memory. Typically these allow for the gpu to read cpu
            // writes from the cache without needing to flush the writes throughout the cache. The
            // reverse is not true and GPU writes tend to invalidate the cache regardless. Also
            // these gpu cache read access are typically lower bandwidth than non-cached memory.
            // For now Skia doesn't really have a need or want of this type of memory. But if we
            // ever do we could pass in an AllocationPropertyFlag that requests the cached property.
            info.requiredFlags =
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            info.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            break;
        case BufferUsage::kTransfersFromCpuToGpu:
            info.requiredFlags =
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            info.preferredFlags = 0;
            break;
        case BufferUsage::kTransfersFromGpuToCpu:
            info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
            info.preferredFlags = VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
            break;
    }

    if (kDedicatedAllocation_AllocationPropertyFlag & allocationPropertyFlags) {
        info.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    }
    if ((kLazyAllocation_AllocationPropertyFlag & allocationPropertyFlags) &&
        BufferUsage::kGpuOnly == usage) {
        info.preferredFlags |= VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
    }

    if (kPersistentlyMapped_AllocationPropertyFlag & allocationPropertyFlags) {
        SkASSERT(BufferUsage::kGpuOnly != usage);
        info.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }

    if (kProtected_AllocationPropertyFlag & allocationPropertyFlags) {
        info.requiredFlags |= VK_MEMORY_PROPERTY_PROTECTED_BIT;
    }

    VmaAllocation allocation;
    VkResult result = vmaAllocateMemoryForBuffer(fAllocator, buffer, &info, &allocation, nullptr);
    if (VK_SUCCESS == result) {
        *backendMemory = (skgpu::VulkanBackendMemory)allocation;
    }

    return result;
}

void VmaSkiaAllocator::freeMemory(const skgpu::VulkanBackendMemory& memoryHandle) 
{
    const VmaAllocation allocation = (VmaAllocation)memoryHandle;
    vmaFreeMemory(fAllocator, allocation);
}

void VmaSkiaAllocator::getAllocInfo(const skgpu::VulkanBackendMemory& memoryHandle,
                                            skgpu::VulkanAlloc* alloc) const 
{
    const VmaAllocation allocation = (VmaAllocation)memoryHandle;
    VmaAllocationInfo vmaInfo;
    vmaGetAllocationInfo(fAllocator, allocation, &vmaInfo);

    VkMemoryPropertyFlags memFlags;
    vmaGetMemoryTypeProperties(fAllocator, vmaInfo.memoryType, &memFlags);

    uint32_t flags = 0;
    if (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT & memFlags) {
        flags |= skgpu::VulkanAlloc::kMappable_Flag;
    }
    if (!SkToBool(VK_MEMORY_PROPERTY_HOST_COHERENT_BIT & memFlags)) {
        flags |= skgpu::VulkanAlloc::kNoncoherent_Flag;
    }
    if (VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT & memFlags) {
        flags |= skgpu::VulkanAlloc::kLazilyAllocated_Flag;
    }

    alloc->fMemory        = vmaInfo.deviceMemory;
    alloc->fOffset        = vmaInfo.offset;
    alloc->fSize          = vmaInfo.size;
    alloc->fFlags         = flags;
    alloc->fBackendMemory = memoryHandle;
}

VkResult VmaSkiaAllocator::mapMemory(const skgpu::VulkanBackendMemory& memoryHandle, void** data) 
{
    const VmaAllocation allocation = (VmaAllocation)memoryHandle;
    return vmaMapMemory(fAllocator, allocation, data);
}

void VmaSkiaAllocator::unmapMemory(const skgpu::VulkanBackendMemory& memoryHandle)
{
    const VmaAllocation allocation = (VmaAllocation)memoryHandle;
    vmaUnmapMemory(fAllocator, allocation);
}

VkResult VmaSkiaAllocator::flushMemory(const skgpu::VulkanBackendMemory& memoryHandle,
                                               VkDeviceSize offset,
                                               VkDeviceSize size)
{
    const VmaAllocation allocation = (VmaAllocation)memoryHandle;
    return vmaFlushAllocation(fAllocator, allocation, offset, size);
}

VkResult VmaSkiaAllocator::invalidateMemory(const skgpu::VulkanBackendMemory& memoryHandle,
                                                    VkDeviceSize offset,
                                                    VkDeviceSize size)
{
    const VmaAllocation allocation = (VmaAllocation)memoryHandle;
    return vmaInvalidateAllocation(fAllocator, allocation, offset, size);
}

std::pair<uint64_t, uint64_t> VmaSkiaAllocator::totalAllocatedAndUsedMemory() const
{
    VmaTotalStatistics stats;
    vmaCalculateStatistics(fAllocator, &stats);
    return {stats.total.statistics.blockBytes, stats.total.statistics.allocationBytes};
}

struct engi::SkiaRenderer::Impl
{
    skgpu::VulkanExtensions                    extensions;
    std::unique_ptr<skgpu::graphite::Context>  context;
    std::unique_ptr<skgpu::graphite::Recorder> recorder;
    sk_sp<SkFontMgr>                           font_mgr;

    VkDevice device = VK_NULL_HANDLE;
    uint32_t queue_family = 0;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;

    ~Impl()
    {
        if (device) vkDeviceWaitIdle(device);     // pending Skia work and submit_end's wait
        recorder.reset();
        context.reset();
    }

    auto init(VkInstance instance, VkPhysicalDevice phys_dev, VkDevice dev, 
                    VmaAllocator alloc, VkCommandPool cmd_pool, VkQueue queue_in, 
                    uint32_t queue_family) -> bool
    {
        if (!instance || !phys_dev || !dev || !queue_in) 
        {
            std::println("[ERROR] Invalid parameters for SkiaRenderer initialization");
            return false;
        }
        device = dev;
        queue_family = queue_family;
        allocator = alloc;
        command_pool = cmd_pool;
        queue = queue_in;

        auto getProc =[](const char* name, VkInstance inst, VkDevice dev) -> PFN_vkVoidFunction 
        {
            return dev ? vkGetDeviceProcAddr(dev, name) : vkGetInstanceProcAddr(inst, name);
        };

        auto features = DeviceFeatures::create();
        auto ins_ext = engi::get_instance_extensions();
        auto dev_ext = engi::get_device_extensions();
        extensions.init(getProc, instance, phys_dev, ins_ext.size(), ins_ext.data(), dev_ext.size(), dev_ext.data());

        skgpu::VulkanBackendContext backend;
        backend.fInstance           = instance;
        backend.fPhysicalDevice     = phys_dev;
        backend.fDevice             = device;
        backend.fQueue              = queue;
        backend.fGraphicsQueueIndex = queue_family;
        backend.fMaxAPIVersion      = VK_API_VERSION_1_4;
        backend.fVkExtensions       = &extensions;
        backend.fDeviceFeatures2    = &features->features2;
        backend.fGetProc            = getProc;

        backend.fMemoryAllocator = VmaSkiaAllocator::create(instance, phys_dev, device, false);
        if (!backend.fMemoryAllocator) 
        {
            std::println("[ERROR] Skia VMA allocator creation failed");
            return false;
        }

        context = skgpu::graphite::ContextFactory::MakeVulkan(backend, {});
        if (!context) 
        {
            std::println("[ERROR] Failed to create Skia Graphite context");
            return false;
        }
        recorder = context->makeRecorder();
        if (!recorder) 
        {
            std::println("[ERROR] Failed to create Skia Graphite recorder");
            return false;
        }

        font_mgr = SkFontMgr_New_FontConfig(nullptr, SkFontScanner_Make_FreeType());

        return true;
    }

    auto draw_rectangle(SkiaSurface& surface, uint32_t frame_id, int x, int y, int w, int h, float r, float g, float b, float a) -> void
    {
        auto canvas = surface.m_impl->surfaces[frame_id]->getCanvas();
        if (!canvas) return;

        SkPaint paint;
        paint.setAntiAlias(true);
        paint.setColor4f({r, g, b, a});
        canvas->drawRect(SkRect::MakeXYWH(float(x), float(y), float(w), float(h)), paint);
    }

    auto draw_text(SkiaSurface& surface, uint32_t frame_id, int x, int y, const std::string& text, float r, float g, float b, float a) -> void
    {
        auto canvas = surface.m_impl->surfaces[frame_id]->getCanvas();
        if (!canvas) return;

        sk_sp<SkTypeface> face = font_mgr->matchFamilyStyle("Arial", SkFontStyle());
        SkFont font(face, 24);
        font.setEdging(SkFont::Edging::kAntiAlias);   // or kSubpixelAntiAlias for LCD
        font.setSubpixel(true);

        SkPaint paint;
        paint.setColor({r, g, b, a});

        canvas->drawString(text.c_str(), x, y, font, paint);
    }

    auto submit(const SkiaSurface& surface, VkSemaphore wait, uint32_t frame_id) -> VkSemaphore
    {
        auto recording = recorder->snap();
        if (!recording) return wait;

        auto state = skgpu::MutableTextureStates::MakeVulkan(VK_IMAGE_LAYOUT_GENERAL, queue_family);
        
        auto frame_surface = surface.m_impl->surfaces[frame_id];
        if (!frame_surface) return wait;

        skgpu::graphite::InsertRecordingInfo insert = 
        {
            .fRecording           = recording.get(),
            .fTargetSurface       = frame_surface.get(),
            .fTargetTextureState  = &state,
        };

        if (!context->insertRecording(insert)) return wait;

        return VK_NULL_HANDLE;
    }

    auto finalize() -> void
    {
        context->submit(skgpu::graphite::SyncToCpu::kNo);
        context->checkAsyncWorkCompletion();
    }
};

auto engi::SkiaRenderer::create(VkInstance instance, VkPhysicalDevice phys_dev, VkDevice device, 
                    VmaAllocator alloc, VkCommandPool cmd_pool, VkQueue queue, 
                    uint32_t queue_family) -> std::optional<SkiaRenderer>
{
    auto impl = std::make_unique<Impl>();
    if (!impl->init(instance, phys_dev, device, alloc, cmd_pool, queue, queue_family)) return std::nullopt;

    SkiaRenderer renderer;
    renderer.m_impl = std::move(impl);
    return renderer;
}

auto engi::SkiaRenderer::create_surface(uint32_t width, uint32_t height) -> std::optional<SkiaSurface>
{
    SkiaSurface output;
    output.m_impl = std::make_unique<SkiaSurface::Impl>();

    if (!output.m_impl->init(m_impl->device, m_impl->allocator, 
        m_impl->command_pool, m_impl->queue, 
        m_impl->recorder.get(), 
        width, height,
         m_impl->queue_family))
        return std::nullopt;

    return output;
}

auto engi::SkiaRenderer::draw_rectangle(SkiaSurface& surface, uint32_t frame_id, int x, int y, int width, int height,
                                        float r, float g, float b, float a) -> void
{
    m_impl->draw_rectangle(surface, frame_id, x, y, width, height, r, g, b, a);
}

auto engi::SkiaRenderer::draw_text(SkiaSurface& surface, uint32_t frame_id, int x, int y, const std::string& text,
                                    float r, float g, float b, float a) -> void
{
    m_impl->draw_text(surface, frame_id, x, y, text, r, g, b, a);
}

auto engi::SkiaRenderer::submit(const SkiaSurface& surface, VkSemaphore wait, uint32_t frame_id) -> VkSemaphore
{
    return m_impl->submit(surface, wait, frame_id);
}

auto engi::SkiaRenderer::finalize() -> void
{
    return m_impl->finalize();
}

engi::SkiaRenderer::SkiaRenderer(SkiaRenderer&&) noexcept = default;
auto engi::SkiaRenderer::operator=(SkiaRenderer&&) noexcept -> SkiaRenderer& = default;
engi::SkiaRenderer::~SkiaRenderer() noexcept = default;