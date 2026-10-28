#pragma once
#include <memory>
#include <optional>
#include <vulkan/vulkan.h>

namespace engi
{
class SkiaRenderer
{
public:
    SkiaRenderer(const SkiaRenderer&) = delete;
    auto operator=(const SkiaRenderer&) -> SkiaRenderer& = delete;

    SkiaRenderer(SkiaRenderer&&) noexcept;
    auto operator=(SkiaRenderer&&) noexcept -> SkiaRenderer&;

    ~SkiaRenderer() noexcept; 

    static auto create(VkInstance instance, VkPhysicalDevice physDev, VkDevice device,
                VkQueue queue, uint32_t queueFamilyIndex) -> std::optional<SkiaRenderer>;

    auto begin_frame(VkImage image, VkFormat format, int width, int height) -> bool;
    auto draw_rectangle(int x, int y, int width, int height, float r, float g, float b, float a) -> void;
    auto end_frame(VkSemaphore wait, uint32_t frame_id) -> VkSemaphore;

private:
    SkiaRenderer() = default;
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}