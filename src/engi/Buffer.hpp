#pragma once

#include <expected>

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
    
namespace engi
{  
    class Buffer
    {
    public: 
        Buffer() noexcept = default;
        Buffer(const Buffer&) = delete;
        Buffer(Buffer&&) noexcept;
        auto operator=(this Buffer&, Buffer&&) noexcept -> Buffer&;
        ~Buffer() noexcept;
        static auto create_cpu(const VkBufferCreateInfo& info, 
            VmaAllocator allocator, VkDevice device) noexcept -> std::expected<Buffer, VkResult>;
        static auto create_gpu(const VkBufferCreateInfo& info, 
            VmaAllocator allocator, VkDevice device) noexcept -> std::expected<Buffer, VkResult>;

        auto buffer() const noexcept -> const VkBuffer& { return m_buffer; };
        auto size() const noexcept -> VkDeviceSize { return m_size; };
        auto write(const void* src, VkDeviceSize src_size, VkDeviceSize dst_offset) noexcept -> void;
        auto data() noexcept -> void* { return m_ptr; }

        auto swap(this Buffer&, Buffer&) noexcept -> void;
    private:
        VkBuffer m_buffer = VK_NULL_HANDLE;
        void* m_ptr = nullptr;
        VkDeviceSize m_size = 0;
        VmaAllocation m_memory = VK_NULL_HANDLE;

        VmaAllocator m_allocator = VK_NULL_HANDLE;
        VkDevice m_device = VK_NULL_HANDLE;
    };
}