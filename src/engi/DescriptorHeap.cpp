#include <engi/DescriptorHeap.hpp>
 
#include <vk_mem_alloc.h>
 
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <utility>
#include <vector>
 
namespace engi
{
 
namespace
{
 
constexpr auto align_up(VkDeviceSize v, VkDeviceSize a) -> VkDeviceSize
{
    return (v + a - 1) & ~(a - 1);
}
 
}  // namespace
 
// ====================================================================== Impl
struct DescriptorHeap::Impl
{
    struct HeapBuffer
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation alloc = nullptr;
        VkDeviceAddress address = 0;
        std::byte* ptr = nullptr;
        VkDeviceSize size = 0;             // total, including reserved range
        VkDeviceSize reserved_offset = 0;  // == bytes available to the application
        VkDeviceSize reserved_size = 0;
    };
 
    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator vma = nullptr;
    HeapBuffer res, smp;
    VkDeviceSize image_size = 0, buffer_size = 0, sampler_size = 0;
    VkDeviceSize slot_size = 0, sampler_stride = 0, max_push = 0;
    uint32_t max_res = 0, max_smp = 0, next_res = 0, next_smp = 0;
    std::vector<uint32_t> res_free, smp_free;
    PFN_vkWriteResourceDescriptorsEXT write_resources = nullptr;
    PFN_vkWriteSamplerDescriptorsEXT write_samplers = nullptr;
    PFN_vkCmdBindResourceHeapEXT cmd_bind_resource = nullptr;
    PFN_vkCmdBindSamplerHeapEXT cmd_bind_sampler = nullptr;
    PFN_vkCmdPushDataEXT cmd_push_data = nullptr;
 
    Impl() = default;
    Impl(const Impl&) = delete;
    auto operator=(const Impl&) -> Impl& = delete;
 
    ~Impl()
    {
        if (!vma) return;
        if (res.buffer) vmaDestroyBuffer(vma, res.buffer, res.alloc);
        if (smp.buffer) vmaDestroyBuffer(vma, smp.buffer, smp.alloc);
    }
 
    auto init(VkPhysicalDevice phys, VkDevice dev, VmaAllocator allocator, const Config& cfg) -> VkResult
    {
        device = dev;
        vma = allocator;
 
        VkPhysicalDeviceDescriptorHeapPropertiesEXT hp{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_PROPERTIES_EXT};
        VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &hp};
        vkGetPhysicalDeviceProperties2(phys, &p2);
 
        image_size = hp.imageDescriptorSize;
        buffer_size = hp.bufferDescriptorSize;
        sampler_size = hp.samplerDescriptorSize;
        max_push = hp.maxPushDataSize;
 
        // One slot fits either an image or a buffer descriptor. All sizes and
        // alignments are powers of two, so the slot size is a multiple of both
        // descriptor sizes and a valid shader array index exists for each.
        const VkDeviceSize align = std::max({hp.imageDescriptorAlignment, hp.bufferDescriptorAlignment});
        slot_size = align_up(std::max(image_size, buffer_size), align);
        sampler_stride = align_up(sampler_size, hp.samplerDescriptorAlignment);
 
        max_res = cfg.max_resources;
        max_smp = cfg.max_samplers;
 
        auto load = [&](const char* name) { return vkGetDeviceProcAddr(dev, name); };
        write_resources = reinterpret_cast<PFN_vkWriteResourceDescriptorsEXT>(load("vkWriteResourceDescriptorsEXT"));
        write_samplers = reinterpret_cast<PFN_vkWriteSamplerDescriptorsEXT>(load("vkWriteSamplerDescriptorsEXT"));
        cmd_bind_resource = reinterpret_cast<PFN_vkCmdBindResourceHeapEXT>(load("vkCmdBindResourceHeapEXT"));
        cmd_bind_sampler = reinterpret_cast<PFN_vkCmdBindSamplerHeapEXT>(load("vkCmdBindSamplerHeapEXT"));
        cmd_push_data = reinterpret_cast<PFN_vkCmdPushDataEXT>(load("vkCmdPushDataEXT"));
        if (!write_resources || !write_samplers || !cmd_bind_resource || !cmd_bind_sampler || !cmd_push_data)
            return VK_ERROR_EXTENSION_NOT_PRESENT;
 
        VkResult r = create_heap(VkDeviceSize(max_res) * slot_size, hp.minResourceHeapReservedRange,
                                 hp.resourceHeapAlignment, hp.maxResourceHeapSize, res);
        if (r != VK_SUCCESS) return r;
 
        return create_heap(VkDeviceSize(max_smp) * sampler_stride, hp.minSamplerHeapReservedRange,
                           hp.samplerHeapAlignment, hp.maxSamplerHeapSize, smp);
    }
 
    static auto alloc_slot(std::vector<uint32_t>& freelist, uint32_t& next, uint32_t max) -> uint32_t
    {
        if (!freelist.empty())
        {
            const uint32_t s = freelist.back();
            freelist.pop_back();
            return s;
        }
        assert(next < max && "descriptor heap is full");
        return next++;
    }
 
    // Layout: [ user descriptors | reserved (implementation owned) ]
    auto create_heap(VkDeviceSize user_bytes, VkDeviceSize reserved, VkDeviceSize alignment,
                     VkDeviceSize max_size, HeapBuffer& out) -> VkResult
    {
        const VkDeviceSize total = user_bytes + reserved;
        if (total > max_size) return VK_ERROR_TOO_MANY_OBJECTS;
 
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size = total;
        bi.usage = VK_BUFFER_USAGE_DESCRIPTOR_HEAP_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
 
        // Host-visible, persistently mapped; VMA prefers device-local (ReBAR/UMA) when available.
        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO;
        ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        ai.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        ai.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
 
        VmaAllocationInfo info{};
        VkResult r = vmaCreateBufferWithAlignment(vma, &bi, &ai, alignment, &out.buffer, &out.alloc, &info);
        if (r != VK_SUCCESS) return r;
 
        VkBufferDeviceAddressInfo da{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
        da.buffer = out.buffer;
        out.address = vkGetBufferDeviceAddress(device, &da);
        out.ptr = static_cast<std::byte*>(info.pMappedData);
        out.size = total;
        out.reserved_offset = user_bytes;
        out.reserved_size = reserved;
 
        assert(out.address % alignment == 0);
        std::memset(out.ptr, 0, size_t(user_bytes));
        flush(out, 0, user_bytes);
        return VK_SUCCESS;
    }
 
    auto write_resource(uint32_t slot, const VkResourceDescriptorInfoEXT& info, VkDeviceSize desc_size) -> void
    {
        assert(slot < max_res);
        const VkDeviceSize off = VkDeviceSize(slot) * slot_size;
        const VkHostAddressRangeEXT dst{res.ptr + off, desc_size};
        [[maybe_unused]] VkResult r = write_resources(device, 1, &info, &dst);
        assert(r == VK_SUCCESS);
        flush(res, off, slot_size);
    }
 
    auto write_image(uint32_t slot, VkDescriptorType type, const VkImageViewCreateInfo& view,
                     VkImageLayout layout) -> void
    {
        assert(type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE || type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        VkImageDescriptorInfoEXT img{VK_STRUCTURE_TYPE_IMAGE_DESCRIPTOR_INFO_EXT};
        img.pView = &view;
        img.layout = layout;
        VkResourceDescriptorInfoEXT info{VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT};
        info.type = type;
        info.data.pImage = &img;
        write_resource(slot, info, image_size);
    }
 
    auto flush(const HeapBuffer& h, VkDeviceSize offset, VkDeviceSize size) const -> void
    {
        vmaFlushAllocation(vma, h.alloc, offset, size);  // no-op on coherent memory
    }
 
    template <class Fn>
    static auto bind_one(VkCommandBuffer cmd, const HeapBuffer& h, Fn fn) -> void
    {
        VkBindHeapInfoEXT bi{VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT};
        bi.heapRange = {h.address, h.size};
        bi.reservedRangeOffset = h.reserved_offset;
        bi.reservedRangeSize = h.reserved_size;
        fn(cmd, &bi);
    }
};
 
// ====================================================================== lifetime
DescriptorHeap::DescriptorHeap(DescriptorHeap&&) noexcept = default;
auto DescriptorHeap::operator=(DescriptorHeap&&) noexcept -> DescriptorHeap& = default;
DescriptorHeap::~DescriptorHeap() noexcept = default;
 
auto DescriptorHeap::create(VkPhysicalDevice phys, VkDevice dev, VmaAllocator vma,
                            const Config& cfg) -> std::optional<DescriptorHeap>
{
    DescriptorHeap heap;
    heap.m_impl = std::make_unique<Impl>();
    if (heap.m_impl->init(phys, dev, vma, cfg) != VK_SUCCESS) return std::nullopt;
    return std::optional<DescriptorHeap>(std::move(heap));
}
 
// ====================================================================== slots
auto DescriptorHeap::alloc_resource_slot() -> uint32_t
{
    return Impl::alloc_slot(m_impl->res_free, m_impl->next_res, m_impl->max_res);
}
 
auto DescriptorHeap::alloc_sampler_slot() -> uint32_t
{
    return Impl::alloc_slot(m_impl->smp_free, m_impl->next_smp, m_impl->max_smp);
}
 
auto DescriptorHeap::free_resource_slot(uint32_t slot) -> void
{
    assert(slot < m_impl->next_res);
    m_impl->res_free.push_back(slot);
}
 
auto DescriptorHeap::free_sampler_slot(uint32_t slot) -> void
{
    assert(slot < m_impl->next_smp);
    m_impl->smp_free.push_back(slot);
}
 
// ====================================================================== writing descriptors
auto DescriptorHeap::write_buffer(uint32_t slot, VkDescriptorType type, VkDeviceAddress address,
                                  VkDeviceSize size) -> void
{
    assert(type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    const VkDeviceAddressRangeEXT range{address, size};
    VkResourceDescriptorInfoEXT info{VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT};
    info.type = type;
    info.data.pAddressRange = &range;
    m_impl->write_resource(slot, info, m_impl->buffer_size);
}
 
auto DescriptorHeap::write_sampled_image(uint32_t slot, const VkImageViewCreateInfo& view,
                                         VkImageLayout layout) -> void
{
    m_impl->write_image(slot, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, view, layout);
}
 
auto DescriptorHeap::write_storage_image(uint32_t slot, const VkImageViewCreateInfo& view,
                                         VkImageLayout layout) -> void
{
    m_impl->write_image(slot, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, view, layout);
}
 
auto DescriptorHeap::write_sampler(uint32_t slot, const VkSamplerCreateInfo& ci) -> void
{
    auto& s = *m_impl;
    assert(slot < s.max_smp);
    const VkDeviceSize off = VkDeviceSize(slot) * s.sampler_stride;
    const VkHostAddressRangeEXT dst{s.smp.ptr + off, s.sampler_size};
    [[maybe_unused]] VkResult r = s.write_samplers(s.device, 1, &ci, &dst);
    assert(r == VK_SUCCESS);
    s.flush(s.smp, off, s.sampler_stride);
}
 
// ====================================================================== shader indices
auto DescriptorHeap::buffer_index(uint32_t slot) const -> uint32_t
{
    return uint32_t(slot * m_impl->slot_size / m_impl->buffer_size);
}
 
auto DescriptorHeap::image_index(uint32_t slot) const -> uint32_t
{
    return uint32_t(slot * m_impl->slot_size / m_impl->image_size);
}
 
auto DescriptorHeap::sampler_index(uint32_t slot) const -> uint32_t
{
    return uint32_t(slot * m_impl->sampler_stride / m_impl->sampler_size);
}
 
// ====================================================================== command buffer
auto DescriptorHeap::bind(VkCommandBuffer cmd) const -> void
{
    Impl::bind_one(cmd, m_impl->smp, m_impl->cmd_bind_sampler);
    Impl::bind_one(cmd, m_impl->res, m_impl->cmd_bind_resource);
}
 
auto DescriptorHeap::push_raw(VkCommandBuffer cmd, const void* data, uint32_t size, uint32_t offset) const -> void
{
    assert(offset + size <= m_impl->max_push);
    VkPushDataInfoEXT info{VK_STRUCTURE_TYPE_PUSH_DATA_INFO_EXT};
    info.offset = offset;
    info.data = {data, size};
    m_impl->cmd_push_data(cmd, &info);
}
 
}  // namespace engi

