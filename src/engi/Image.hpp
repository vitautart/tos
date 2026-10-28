#pragma once

#include <expected>

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

namespace engi
{
    struct Image
    {
    public:
        explicit Image() = default;
        Image(const Image&) = delete;
        Image(Image&&) noexcept;
        auto operator=(this Image&, Image&&) noexcept -> Image&;
        ~Image();
        static auto create(const VkImageCreateInfo& image_info, 
            const VkImageViewCreateInfo& view_info,
            VmaAllocator allocator,
            VkDevice device
        ) -> std::expected<Image, VkResult>;
        static auto create(VkImage vk_image, const VkImageViewCreateInfo& view_info,
            VmaAllocator allocator,
            VkDevice device
        ) -> std::expected<Image, VkResult>;
        auto view() const { return m_view; }
        auto image() const { return m_image; }

        auto swap(this Image&, Image&) noexcept -> void;

    private:
        VkImage m_image = VK_NULL_HANDLE;
        VkImageView m_view = VK_NULL_HANDLE;
        VkFormat m_format = VK_FORMAT_UNDEFINED;
        VmaAllocation m_memory = VK_NULL_HANDLE;

        VmaAllocator m_allocator = VK_NULL_HANDLE;
        VkDevice m_device = VK_NULL_HANDLE;
    };
}