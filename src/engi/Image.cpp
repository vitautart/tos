#include <engi/Image.hpp>

#define ENGI_SWAP_V(o1, field, o2) {auto m = o1.field; o1.field = o2.field; o2.field = m;}

engi::Image::Image(Image&& other) noexcept : Image{}
{
    swap(other);
}

auto engi::Image::operator=(this Image& lhs, Image&& rhs) noexcept -> Image&
{
    lhs.swap(rhs); return lhs;
}

auto engi::Image::swap(this Image& lhs, Image& rhs) noexcept -> void
{
    static_assert(sizeof(Image) == (offsetof(Image, m_device) + sizeof(VkDevice)), "Swap needs to be updated");
    ENGI_SWAP_V(lhs, m_image, rhs);
    ENGI_SWAP_V(lhs, m_view, rhs);
    ENGI_SWAP_V(lhs, m_format, rhs);
    ENGI_SWAP_V(lhs, m_memory, rhs);
    ENGI_SWAP_V(lhs, m_allocator, rhs);
    ENGI_SWAP_V(lhs, m_device, rhs);
}

auto engi::Image::create(const VkImageCreateInfo& image_info, const VkImageViewCreateInfo& view_info,
    VmaAllocator allocator, VkDevice device) -> std::expected<Image, VkResult>
{
    Image output;
    output.m_allocator = allocator;
    output.m_device = device;
    VmaAllocation allocation;
    VmaAllocationInfo alloc_info;
    VmaAllocationCreateInfo alloc_create_info = { .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT, .usage = VmaMemoryUsage::VMA_MEMORY_USAGE_AUTO };

    auto result = vmaCreateImage(allocator, &image_info, &alloc_create_info, &output.m_image, &output.m_memory, &alloc_info);
    if (result != VK_SUCCESS) return std::unexpected(result);

    VkImageViewCreateInfo view_info_copy = view_info;
    view_info_copy.image = output.m_image;
    result = vkCreateImageView(device, &view_info_copy, nullptr, &output.m_view);
    if (result != VK_SUCCESS) return std::unexpected(result);

    output.m_format = image_info.format;

    return output;
}

auto engi::Image::create(VkImage vk_image, const VkImageViewCreateInfo& view_info,
    VmaAllocator allocator, VkDevice device) -> std::expected<Image, VkResult>
{
    Image output;
    output.m_allocator = allocator;
    output.m_device = device;
    VkImageViewCreateInfo view_info_copy = view_info;
    view_info_copy.image = vk_image;
    
    auto result = vkCreateImageView(device, &view_info_copy, nullptr, &output.m_view);
    if (result != VK_SUCCESS) return std::unexpected(result);

    output.m_format = view_info.format;
    output.m_memory = VK_NULL_HANDLE;
    output.m_image = vk_image;

    return output;
}

engi::Image::~Image()
{
    if (m_view && m_device)
        vkDestroyImageView(m_device, m_view, nullptr);
    if (m_image && m_memory && m_allocator)
        vmaDestroyImage(m_allocator, m_image, m_memory);
    m_view = VK_NULL_HANDLE;
    m_image = VK_NULL_HANDLE;
    m_memory = VK_NULL_HANDLE;
    m_allocator = VK_NULL_HANDLE;
    m_device = VK_NULL_HANDLE;
}