
// engi_descriptor_heap.hpp
// Minimal VK_EXT_descriptor_heap helper (C++23) using VMA for the heap memory.
//
// Requirements
//  - Vulkan headers new enough to contain VK_EXT_descriptor_heap
//  - VmaAllocator created with VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT
//  - Device created with: VK_EXT_descriptor_heap, VK_KHR_shader_untyped_pointers,
//    bufferDeviceAddress, and VkPhysicalDeviceDescriptorHeapFeaturesEXT::descriptorHeap
//  - Pipelines / shader objects created with VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT
//    (or VK_SHADER_CREATE_DESCRIPTOR_HEAP_BIT_EXT) and a NULL pipeline layout
//
// Usage
//   engi::DescriptorHeap heap;
//   heap.init(phys, dev, vma, {});
//   uint32_t s = heap.alloc_resource_slot();
//   heap.write_buffer(s, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, addr, size);
//   ...
//   heap.bind(cmd);                                   // once per command buffer
//   struct Push { uint32_t buf; uint32_t tex; uint32_t smp; uint32_t pad; };
//   heap.push(cmd, Push{heap.buffer_index(s), ...});  // per draw/dispatch
//
// Not thread-safe. Descriptors must not be rewritten while the GPU may still read them.
 
#pragma once
 
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
 
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>
 
namespace engi {
 
class DescriptorHeap {
public:
    struct Config {
        uint32_t max_resources = 16384;
        uint32_t max_samplers = 256;
        // Reserve extra sampler-heap space if shaders use embedded samplers.
        bool embedded_samplers = false;
    };
 
    DescriptorHeap() = default;
    ~DescriptorHeap() { destroy(); }
 
    DescriptorHeap(const DescriptorHeap&) = delete;
    DescriptorHeap& operator=(const DescriptorHeap&) = delete;
    DescriptorHeap(DescriptorHeap&& o) noexcept : m(std::exchange(o.m, State{})) {}
    DescriptorHeap& operator=(DescriptorHeap&& o) noexcept {
        if (this != &o) {
            destroy();
            m = std::exchange(o.m, State{});
        }
        return *this;
    }
 
    // ------------------------------------------------------------------ lifetime
    [[nodiscard]] VkResult init(VkPhysicalDevice phys, VkDevice dev, VmaAllocator vma, const Config& cfg) {
        destroy();
        m.device = dev;
        m.vma = vma;
 
        VkPhysicalDeviceDescriptorHeapPropertiesEXT hp{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_PROPERTIES_EXT};
        VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &hp};
        vkGetPhysicalDeviceProperties2(phys, &p2);
 
        m.image_size = hp.imageDescriptorSize;
        m.buffer_size = hp.bufferDescriptorSize;
        m.sampler_size = hp.samplerDescriptorSize;
        m.max_push = hp.maxPushDataSize;
 
        // One slot fits either an image or a buffer descriptor. All sizes and
        // alignments are powers of two, so the slot size is a multiple of both
        // descriptor sizes and a valid shader array index exists for each.
        const VkDeviceSize align = std::max({hp.imageDescriptorAlignment, hp.bufferDescriptorAlignment});
        m.slot_size = align_up(std::max(m.image_size, m.buffer_size), align);
        m.sampler_stride = align_up(m.sampler_size, hp.samplerDescriptorAlignment);
 
        m.max_res = cfg.max_resources;
        m.max_smp = cfg.max_samplers;
 
        auto load = [&](const char* name) { return vkGetDeviceProcAddr(dev, name); };
        m.write_resources = reinterpret_cast<PFN_vkWriteResourceDescriptorsEXT>(load("vkWriteResourceDescriptorsEXT"));
        m.write_samplers = reinterpret_cast<PFN_vkWriteSamplerDescriptorsEXT>(load("vkWriteSamplerDescriptorsEXT"));
        m.cmd_bind_resource = reinterpret_cast<PFN_vkCmdBindResourceHeapEXT>(load("vkCmdBindResourceHeapEXT"));
        m.cmd_bind_sampler = reinterpret_cast<PFN_vkCmdBindSamplerHeapEXT>(load("vkCmdBindSamplerHeapEXT"));
        m.cmd_push_data = reinterpret_cast<PFN_vkCmdPushDataEXT>(load("vkCmdPushDataEXT"));
        if (!m.write_resources || !m.write_samplers || !m.cmd_bind_resource || !m.cmd_bind_sampler || !m.cmd_push_data) {
            destroy();
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }
 
        VkResult r = create_heap(VkDeviceSize(m.max_res) * m.slot_size, hp.minResourceHeapReservedRange,
                                 hp.resourceHeapAlignment, hp.maxResourceHeapSize, m.res);
        if (r != VK_SUCCESS) { destroy(); return r; }
 
        const VkDeviceSize smp_reserved =
            cfg.embedded_samplers ? hp.minSamplerHeapReservedRangeWithEmbedded : hp.minSamplerHeapReservedRange;
        r = create_heap(VkDeviceSize(m.max_smp) * m.sampler_stride, smp_reserved,
                        hp.samplerHeapAlignment, hp.maxSamplerHeapSize, m.smp);
        if (r != VK_SUCCESS) { destroy(); return r; }
 
        return VK_SUCCESS;
    }
 
    void destroy() {
        if (m.vma) {
            if (m.res.buffer) vmaDestroyBuffer(m.vma, m.res.buffer, m.res.alloc);
            if (m.smp.buffer) vmaDestroyBuffer(m.vma, m.smp.buffer, m.smp.alloc);
        }
        m = State{};
    }
 
    // ------------------------------------------------------------------ slots
    [[nodiscard]] uint32_t alloc_resource_slot() { return alloc_slot(m.res_free, m.next_res, m.max_res); }
    [[nodiscard]] uint32_t alloc_sampler_slot() { return alloc_slot(m.smp_free, m.next_smp, m.max_smp); }
    void free_resource_slot(uint32_t slot) { assert(slot < m.next_res); m.res_free.push_back(slot); }
    void free_sampler_slot(uint32_t slot) { assert(slot < m.next_smp); m.smp_free.push_back(slot); }
 
    // ------------------------------------------------------------------ writing descriptors
    // Uniform / storage buffer (raw address range, no VkBuffer or view needed).
    void write_buffer(uint32_t slot, VkDescriptorType type, VkDeviceAddress address, VkDeviceSize size) {
        assert(type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        const VkDeviceAddressRangeEXT range{address, size};
        VkResourceDescriptorInfoEXT info{VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT};
        info.type = type;
        info.data.pAddressRange = &range;
        write_resource(slot, info, m.buffer_size);
    }
 
    // `view.image` must be set; no VkImageView object is created.
    void write_image(uint32_t slot, VkDescriptorType type, const VkImageViewCreateInfo& view,
                     VkImageLayout layout) {
        assert(type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE || type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        VkImageDescriptorInfoEXT img{VK_STRUCTURE_TYPE_IMAGE_DESCRIPTOR_INFO_EXT};
        img.pView = &view;
        img.layout = layout;
        VkResourceDescriptorInfoEXT info{VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT};
        info.type = type;
        info.data.pImage = &img;
        write_resource(slot, info, m.image_size);
    }
 
    void write_sampled_image(uint32_t slot, const VkImageViewCreateInfo& view,
                             VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        write_image(slot, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, view, layout);
    }
 
    void write_storage_image(uint32_t slot, const VkImageViewCreateInfo& view,
                             VkImageLayout layout = VK_IMAGE_LAYOUT_GENERAL) {
        write_image(slot, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, view, layout);
    }
 
    void write_sampler(uint32_t slot, const VkSamplerCreateInfo& ci) {
        assert(slot < m.max_smp);
        const VkHostAddressRangeEXT dst{m.smp.ptr + VkDeviceSize(slot) * m.sampler_stride, m.sampler_size};
        [[maybe_unused]] VkResult r = m.write_samplers(m.device, 1, &ci, &dst);
        assert(r == VK_SUCCESS);
        flush(m.smp, VkDeviceSize(slot) * m.sampler_stride, m.sampler_stride);
    }
 
    // ------------------------------------------------------------------ shader indices
    // What the shader indexes ResourceHeap / SamplerHeap arrays with.
    // (Slots are `slot_size` bytes apart; the shader array stride is the descriptor's own size.)
    [[nodiscard]] uint32_t buffer_index(uint32_t slot) const { return uint32_t(slot * m.slot_size / m.buffer_size); }
    [[nodiscard]] uint32_t image_index(uint32_t slot) const { return uint32_t(slot * m.slot_size / m.image_size); }
    [[nodiscard]] uint32_t sampler_index(uint32_t slot) const { return uint32_t(slot * m.sampler_stride / m.sampler_size); }
 
    // Byte offsets, for descriptor-set-and-binding mappings (HEAP_WITH_CONSTANT_OFFSET etc.).
    [[nodiscard]] uint32_t resource_byte_offset(uint32_t slot) const { return uint32_t(slot * m.slot_size); }
    [[nodiscard]] uint32_t sampler_byte_offset(uint32_t slot) const { return uint32_t(slot * m.sampler_stride); }
 
    // ------------------------------------------------------------------ command buffer
    // Binding invalidates descriptor sets / descriptor buffers; do it once per command buffer.
    void bind(VkCommandBuffer cmd) const {
        bind_one(cmd, m.smp, m.cmd_bind_sampler);
        bind_one(cmd, m.res, m.cmd_bind_resource);
    }
 
    template <class T>
    void push(VkCommandBuffer cmd, const T& data, uint32_t offset = 0) const {
        static_assert(sizeof(T) % 4 == 0, "push data must be a multiple of 4 bytes");
        assert(offset + sizeof(T) <= m.max_push);
        VkPushDataInfoEXT info{VK_STRUCTURE_TYPE_PUSH_DATA_INFO_EXT};
        info.offset = offset;
        info.data = {&data, sizeof(T)};
        m.cmd_push_data(cmd, &info);
    }
 
    // ------------------------------------------------------------------ misc
    [[nodiscard]] VkDeviceSize image_descriptor_size() const { return m.image_size; }
    [[nodiscard]] VkDeviceSize buffer_descriptor_size() const { return m.buffer_size; }
    [[nodiscard]] VkDeviceSize sampler_descriptor_size() const { return m.sampler_size; }
    [[nodiscard]] VkDeviceSize max_push_data_size() const { return m.max_push; }
 
private:
    struct HeapBuffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation alloc = nullptr;
        VkDeviceAddress address = 0;
        std::byte* ptr = nullptr;
        VkDeviceSize size = 0;             // total, including reserved range
        VkDeviceSize reserved_offset = 0;  // == bytes available to the application
        VkDeviceSize reserved_size = 0;
    };
 
    struct State {
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
    };
 
    static constexpr VkDeviceSize align_up(VkDeviceSize v, VkDeviceSize a) { return (v + a - 1) & ~(a - 1); }
 
    static uint32_t alloc_slot(std::vector<uint32_t>& freelist, uint32_t& next, uint32_t max) {
        if (!freelist.empty()) {
            const uint32_t s = freelist.back();
            freelist.pop_back();
            return s;
        }
        assert(next < max && "descriptor heap is full");
        return next++;
    }
 
    // Layout: [ user descriptors | reserved (implementation owned) ]
    VkResult create_heap(VkDeviceSize user_bytes, VkDeviceSize reserved, VkDeviceSize alignment,
                         VkDeviceSize max_size, HeapBuffer& out) {
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
        VkResult r = vmaCreateBufferWithAlignment(m.vma, &bi, &ai, alignment, &out.buffer, &out.alloc, &info);
        if (r != VK_SUCCESS) return r;
 
        VkBufferDeviceAddressInfo da{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
        da.buffer = out.buffer;
        out.address = vkGetBufferDeviceAddress(m.device, &da);
        out.ptr = static_cast<std::byte*>(info.pMappedData);
        out.size = total;
        out.reserved_offset = user_bytes;
        out.reserved_size = reserved;
 
        assert(out.address % alignment == 0);
        std::memset(out.ptr, 0, size_t(user_bytes));
        flush(out, 0, user_bytes);
        return VK_SUCCESS;
    }
 
    void write_resource(uint32_t slot, const VkResourceDescriptorInfoEXT& info, VkDeviceSize desc_size) {
        assert(slot < m.max_res);
        const VkDeviceSize off = VkDeviceSize(slot) * m.slot_size;
        const VkHostAddressRangeEXT dst{m.res.ptr + off, desc_size};
        [[maybe_unused]] VkResult r = m.write_resources(m.device, 1, &info, &dst);
        assert(r == VK_SUCCESS);
        flush(m.res, off, m.slot_size);
    }
 
    void flush(const HeapBuffer& h, VkDeviceSize offset, VkDeviceSize size) const {
        vmaFlushAllocation(m.vma, h.alloc, offset, size);  // no-op on coherent memory
    }
 
    static void bind_one(VkCommandBuffer cmd, const HeapBuffer& h, auto fn) {
        VkBindHeapInfoEXT bi{VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT};
        bi.heapRange = {h.address, h.size};
        bi.reservedRangeOffset = h.reserved_offset;
        bi.reservedRangeSize = h.reserved_size;
        fn(cmd, &bi);
    }
 
    State m;
};
 
}  // namespace engi
