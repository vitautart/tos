#include <print>

#include <vk_mem_alloc.h>

#include <engi/SkiaRenderer.hpp>
#include <engi/DeviceFeatures.hpp>

#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkPaint.h"
#include "include/core/SkSurface.h"
#include "include/gpu/graphite/BackendSemaphore.h"
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

namespace
{
auto to_sk_color_type(VkFormat f) -> std::optional<SkColorType>
{
    switch (f) {
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB: return kBGRA_8888_SkColorType;
            //return kSRGBA_8888_SkColorType;
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB: return kSRGBA_8888_SkColorType;
        default:                      return std::nullopt;
    }
}
}

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

    VkDevice device = VK_NULL_HANDLE;
    uint32_t queue_family = 0;

    std::vector<VkSemaphore> signal_semaphores;
    sk_sp<SkSurface> surface;
    SkCanvas*        canvas = nullptr;

    ~Impl()
    {
        if (device) vkDeviceWaitIdle(device);     // pending Skia work and submit_end's wait
        canvas = nullptr;
        surface.reset();
        recorder.reset();
        context.reset();
        for (auto s : signal_semaphores) vkDestroySemaphore(device, s, nullptr);
    }

    auto init(VkInstance instance, VkPhysicalDevice physDev, VkDevice dev,
              VkQueue queue, uint32_t queueFamilyIndex) -> bool
    {
        if (!instance || !physDev || !dev || !queue) 
        {
            std::println("[ERROR] Invalid parameters for SkiaRenderer initialization");
            return false;
        }
        device = dev;
        queue_family = queueFamilyIndex;

        auto getProc =[](const char* name, VkInstance inst, VkDevice dev) -> PFN_vkVoidFunction 
        {
            return dev ? vkGetDeviceProcAddr(dev, name) : vkGetInstanceProcAddr(inst, name);
        };

        auto features = DeviceFeatures::create();
        auto ins_ext = engi::get_instance_extensions();
        auto dev_ext = engi::get_device_extensions();
        extensions.init(getProc, instance, physDev, ins_ext.size(), ins_ext.data(), dev_ext.size(), dev_ext.data());

        skgpu::VulkanBackendContext backend;
        backend.fInstance           = instance;
        backend.fPhysicalDevice     = physDev;
        backend.fDevice             = device;
        backend.fQueue              = queue;
        backend.fGraphicsQueueIndex = queueFamilyIndex;
        backend.fMaxAPIVersion      = VK_API_VERSION_1_4;
        backend.fVkExtensions       = &extensions;
        backend.fDeviceFeatures2    = &features->features2;
        backend.fGetProc            = getProc;

        backend.fMemoryAllocator = VmaSkiaAllocator::create(instance, physDev, device, false);
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

        VkSemaphoreCreateInfo sci = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        signal_semaphores.resize(engi::FRAMES_FLYING, VK_NULL_HANDLE);
        for (auto& s : signal_semaphores)
        {
            if (vkCreateSemaphore(device, &sci, nullptr, &s) != VK_SUCCESS) 
            {
                std::println("[ERROR] Failed to create signal semaphore");
                return false;
            }
        }
        return true;
    }

    auto begin_frame(VkImage image, VkFormat format, int width, int height) -> bool
    {
        canvas = nullptr;
        surface.reset();

        if (!image || width <= 0 || height <= 0) return false;

        const auto colorType = to_sk_color_type(format);
        if (!colorType) return false;

        skgpu::graphite::VulkanTextureInfo info;
        info.fSampleCount     = skgpu::graphite::SampleCount::k1;
        info.fMipmapped       = skgpu::Mipmapped::kNo;
        info.fFlags           = 0;
        info.fFormat          = format;
        info.fImageTiling     = VK_IMAGE_TILING_OPTIMAL;
        info.fImageUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
        info.fSharingMode     = VK_SHARING_MODE_EXCLUSIVE;
        info.fAspectMask      = VK_IMAGE_ASPECT_COLOR_BIT;

        // Your color_cmd + scene leave the image in COLOR_ATTACHMENT_OPTIMAL.
        auto tex = skgpu::graphite::BackendTextures::MakeVulkan(
                {width, height}, info, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                queue_family, image, skgpu::VulkanAlloc());

        surface = SkSurfaces::WrapBackendTexture(recorder.get(), tex, *colorType, SkColorSpace::MakeSRGB(), nullptr);
        //surface = SkSurfaces::WrapBackendTexture(recorder.get(), tex, SkColorSpace::MakeSRGB(), nullptr);
        if (!surface) return false;

        canvas = surface->getCanvas();
        return true;
    }

    auto draw_rectangle(int x, int y, int w, int h, float r, float g, float b, float a) -> void
    {
        if (!canvas) return;

        SkPaint paint;
        paint.setAntiAlias(true);
        paint.setColor4f({r, g, b, a});
        canvas->drawRect(SkRect::MakeXYWH(float(x), float(y), float(w), float(h)), paint);
    }

    auto end_frame(VkSemaphore wait, uint32_t frame_id) -> VkSemaphore
    {
        if (!surface) return wait;

        auto frame_surface = std::move(surface);   // always release the surface
        canvas = nullptr;

        auto recording = recorder->snap();
        if (!recording) return wait;

        const VkSemaphore signal = signal_semaphores[frame_id % signal_semaphores.size()];

        // Keep the layout: your present_cmd transitions to PRESENT_SRC.
        auto state = skgpu::MutableTextureStates::MakeVulkan(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, queue_family);
        auto waitSem   = skgpu::graphite::BackendSemaphores::MakeVulkan(wait);
        auto signalSem = skgpu::graphite::BackendSemaphores::MakeVulkan(signal);

        skgpu::graphite::InsertRecordingInfo insert;
        insert.fRecording           = recording.get();
        insert.fTargetSurface       = frame_surface.get();
        insert.fTargetTextureState  = &state;
        insert.fNumWaitSemaphores   = 1;
        insert.fWaitSemaphores      = &waitSem;
        insert.fNumSignalSemaphores = 1;
        insert.fSignalSemaphores    = &signalSem;

        if (!context->insertRecording(insert)) return wait;

        context->submit(skgpu::graphite::SyncToCpu::kNo);
        context->checkAsyncWorkCompletion();
        return signal;
    }
};

auto engi::SkiaRenderer::create(VkInstance instance, VkPhysicalDevice physDev, VkDevice device,
                          VkQueue queue, uint32_t queueFamilyIndex) -> std::optional<SkiaRenderer>
{
    auto impl = std::make_unique<Impl>();
    if (!impl->init(instance, physDev, device, queue, queueFamilyIndex)) return std::nullopt;

    SkiaRenderer renderer;
    renderer.m_impl = std::move(impl);
    return renderer;
}

auto engi::SkiaRenderer::begin_frame(VkImage image, VkFormat format, int width, int height) -> bool
{
    return m_impl->begin_frame(image, format, width, height);
}

auto engi::SkiaRenderer::draw_rectangle(int x, int y, int width, int height,
                                        float r, float g, float b, float a) -> void
{
    m_impl->draw_rectangle(x, y, width, height, r, g, b, a);
}

auto engi::SkiaRenderer::end_frame(VkSemaphore wait, uint32_t frame_id) -> VkSemaphore
{
    return m_impl->end_frame(wait, frame_id);
}

engi::SkiaRenderer::SkiaRenderer(SkiaRenderer&&) noexcept = default;
auto engi::SkiaRenderer::operator=(SkiaRenderer&&) noexcept -> SkiaRenderer& = default;
engi::SkiaRenderer::~SkiaRenderer() noexcept = default;