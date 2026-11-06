#pragma once
 
#include <vulkan/vulkan.h>
 
#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
 
// Forward declaration so users of this header don't need vk_mem_alloc.h.
// (Identical typedef redeclaration is legal; matches VMA's own definition.)
VK_DEFINE_HANDLE(VmaAllocator)
 
namespace engi
{
 
class DescriptorHeap
{
public:
    struct Config
    {
        uint32_t max_resources = 16384;
        uint32_t max_samplers = 256;
    };
 
    DescriptorHeap(const DescriptorHeap&) = delete;
    auto operator=(const DescriptorHeap&) -> DescriptorHeap& = delete;
 
    DescriptorHeap(DescriptorHeap&&) noexcept;
    auto operator=(DescriptorHeap&&) noexcept -> DescriptorHeap&;
 
    ~DescriptorHeap() noexcept;
 
    // Returns std::nullopt if the extension entry points are missing, the heaps
    // exceed device limits, or allocation fails.
    [[nodiscard]] static auto create(VkPhysicalDevice phys, VkDevice dev, VmaAllocator vma,
                                     const Config& cfg) -> std::optional<DescriptorHeap>;
 
    // ------------------------------------------------------------------ slots
    [[nodiscard]] auto alloc_resource_slot() -> uint32_t;
    [[nodiscard]] auto alloc_sampler_slot() -> uint32_t;
    auto free_resource_slot(uint32_t slot) -> void;
    auto free_sampler_slot(uint32_t slot) -> void;
 
    // ------------------------------------------------------------------ writing descriptors
    // Uniform / storage buffer (raw address range, no VkBuffer or view needed).
    auto write_buffer(uint32_t slot, VkDescriptorType type, VkDeviceAddress address,
                      VkDeviceSize size) -> void;
 
    // `view.image` must be set; no VkImageView object is created.
    auto write_sampled_image(uint32_t slot, const VkImageViewCreateInfo& view,
                             VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) -> void;
 
    auto write_storage_image(uint32_t slot, const VkImageViewCreateInfo& view,
                             VkImageLayout layout = VK_IMAGE_LAYOUT_GENERAL) -> void;
 
    auto write_sampler(uint32_t slot, const VkSamplerCreateInfo& ci) -> void;
 
    // ------------------------------------------------------------------ shader indices
    // What the shader indexes ResourceHeap / SamplerHeap arrays with.
    [[nodiscard]] auto buffer_index(uint32_t slot) const -> uint32_t;
    [[nodiscard]] auto image_index(uint32_t slot) const -> uint32_t;
    [[nodiscard]] auto sampler_index(uint32_t slot) const -> uint32_t;
 
    // ------------------------------------------------------------------ command buffer
    // Binding invalidates descriptor sets / descriptor buffers; do it once per command buffer.
    auto bind(VkCommandBuffer cmd) const -> void;
 
    template <class T>
    auto push(VkCommandBuffer cmd, const T& data, uint32_t offset = 0) const -> void
    {
        static_assert(sizeof(T) % 4 == 0, "push data must be a multiple of 4 bytes");
        push_raw(cmd, &data, static_cast<uint32_t>(sizeof(T)), offset);
    }
 
private:
    DescriptorHeap() = default;
 
    auto push_raw(VkCommandBuffer cmd, const void* data, uint32_t size, uint32_t offset) const -> void;
 
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
 
}  // namespace engi
