#include <engi/Buffer.hpp>

#include <print>
#include <cstring>

#define ENGI_SWAP_V(o1, field, o2) {auto m = o1.field; o1.field = o2.field; o2.field = m;}

engi::Buffer::Buffer(Buffer&& other) noexcept : Buffer{}
{
    swap(other);
}

auto engi::Buffer::operator=(this Buffer& lhs, Buffer&& rhs) noexcept -> Buffer&
{
    lhs.swap(rhs); return lhs;
}

auto engi::Buffer::swap(this Buffer& lhs, Buffer& rhs) noexcept -> void
{
    static_assert(sizeof(Buffer) == (offsetof(Buffer, m_device) + sizeof(VkDevice)), "Swap needs to be updated");
    ENGI_SWAP_V(lhs, m_buffer, rhs);
    ENGI_SWAP_V(lhs, m_ptr, rhs);
    ENGI_SWAP_V(lhs, m_size, rhs);
    ENGI_SWAP_V(lhs, m_memory, rhs);

    ENGI_SWAP_V(lhs, m_allocator, rhs);
    ENGI_SWAP_V(lhs, m_device, rhs);
}

auto engi::Buffer::create_cpu(const VkBufferCreateInfo& info, VmaAllocator allocator, VkDevice device) noexcept -> std::expected<Buffer, VkResult>
{
    Buffer out;
    VmaAllocationInfo alloc_info;
    VmaAllocationCreateInfo alloc_create_info = 
    {
        .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
        .usage = VmaMemoryUsage::VMA_MEMORY_USAGE_AUTO
    };
    auto result = vmaCreateBuffer(allocator, &info, &alloc_create_info, &out.m_buffer, &out.m_memory, &alloc_info);
    if (result != VK_SUCCESS) return std::unexpected(result);

    out.m_ptr = alloc_info.pMappedData;
    out.m_size = info.size;
    return out;
}

auto engi::Buffer::create_gpu(const VkBufferCreateInfo& info, VmaAllocator allocator, VkDevice device) noexcept -> std::expected<Buffer, VkResult>
{
    Buffer out;
    out.m_allocator = allocator;
    out.m_device = device;
    VmaAllocationInfo alloc_info;
    VmaAllocationCreateInfo alloc_reate_info = 
    {
        .flags = 0,
        .usage = VmaMemoryUsage::VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
    };
    auto result = vmaCreateBuffer(allocator, &info, &alloc_reate_info, &out.m_buffer, &out.m_memory, &alloc_info);
    if (result != VK_SUCCESS) return std::unexpected(result);

    out.m_ptr = nullptr;
    out.m_size = info.size;

    return out;
}

engi::Buffer::~Buffer() noexcept
{
    if (m_buffer && m_memory && m_allocator)
            vmaDestroyBuffer(m_allocator, m_buffer, m_memory);
}

auto engi::Buffer::write(const void* src, VkDeviceSize src_size, VkDeviceSize dst_offset) noexcept -> void
{
    if (m_ptr != nullptr)
        std::memcpy((char*)m_ptr + dst_offset, src, src_size);
    else
        std::println("[WARNING] Trying to write to non-mapped buffer!");
}