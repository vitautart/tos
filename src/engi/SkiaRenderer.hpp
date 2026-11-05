#pragma once
#include <memory>
#include <optional>
#include <vulkan/vulkan.h>

#include <vk_mem_alloc.h>

namespace engi
{
class SkiaRenderer;
class SkiaSurface;

class SkiaSurface
{
public:
    friend class SkiaRenderer;
    SkiaSurface(const SkiaSurface&) = delete;
    auto operator=(const SkiaSurface&) -> SkiaSurface& = delete;

    SkiaSurface(SkiaSurface&&) noexcept;
    auto operator=(SkiaSurface&&) noexcept -> SkiaSurface&;

    ~SkiaSurface() noexcept;

    auto image(uint32_t frame_id) const noexcept -> VkImage;
    auto view(uint32_t frame_id) const noexcept -> VkImageView;
    auto format() const noexcept -> VkFormat;
private:
    SkiaSurface() = default;
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

class SkiaRenderer
{
public:
    friend class SkiaSurface;
    SkiaRenderer(const SkiaRenderer&) = delete;
    auto operator=(const SkiaRenderer&) -> SkiaRenderer& = delete;

    SkiaRenderer(SkiaRenderer&&) noexcept;
    auto operator=(SkiaRenderer&&) noexcept -> SkiaRenderer&;

    ~SkiaRenderer() noexcept; 

    static auto create(VkInstance instance, VkPhysicalDevice phys_dev, VkDevice device, 
                    VmaAllocator alloc, VkCommandPool cmd_pool, VkQueue queue, 
                    uint32_t queue_family) -> std::optional<SkiaRenderer>;
    auto create_surface(uint32_t width, uint32_t height) -> std::optional<SkiaSurface>;

    auto draw_rectangle(SkiaSurface& surface, uint32_t frame_id, int x, int y, int width, int height, float r, float g, float b, float a) -> void;
    auto draw_text(SkiaSurface& surface, uint32_t frame_id, int x, int y, const std::string& text, float r, float g, float b, float a) -> void;
    auto submit(const SkiaSurface& surface, VkSemaphore wait, uint32_t frame_id) -> VkSemaphore;
    auto finalize() -> void;

private:
    SkiaRenderer() = default;
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}