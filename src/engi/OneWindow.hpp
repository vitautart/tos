#pragma once

#include <cstddef>
#include <string>
#include <optional>

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <memory>

#include <engi/gomath.hpp>

namespace engi {
constexpr VkSampleCountFlagBits ANTIALIASING_SAMPLES = VK_SAMPLE_COUNT_4_BIT;

struct AcquireResult
{
    uint32_t image;
    uint32_t frame;
    VkResult result;

    VkImage  vkImage    = VK_NULL_HANDLE;
    VkFormat format     = VK_FORMAT_UNDEFINED;
    int      width      = 0;
    int      height     = 0;
};

class OneWindow
{
public:
    OneWindow(const OneWindow&) = delete;
    auto operator=(const OneWindow&) -> OneWindow& = delete;

    OneWindow(OneWindow&&) noexcept;
    auto operator=(OneWindow&&) noexcept -> OneWindow&;

    ~OneWindow() noexcept; 
    static auto create(size_t width, size_t height, const std::string& name) -> std::optional<OneWindow>;

    auto get_instance() const -> VkInstance;
    auto get_device() const -> VkDevice;
    auto get_physical_device() const -> VkPhysicalDevice;
    auto get_allocator() const -> VmaAllocator;
    auto get_queue_graphics() const -> VkQueue;
    auto get_queue_graphics_family() const -> uint32_t;
    auto get_queue_present() const -> VkQueue;
    auto get_queue_present_family() const -> uint32_t;
    auto get_size() const -> std::pair<int, int>;

    auto should_close() const -> bool;

    // Level 1 scope
    auto acquire() -> AcquireResult;
    auto submit_start() -> std::optional<VkSemaphore>;
    auto submit_end(VkSemaphore wait_semaphore) -> bool;
    auto check_events(bool wait_events) -> void;

    // Level 2 scope
    auto cmd_start() -> VkCommandBuffer;
    auto cmd_end() -> void;

    // Level 3 scope
    auto draw_start(VkCommandBuffer cmd, go::vf4 srgba_bg) -> void;
    auto draw_start(VkCommandBuffer cmd, const VkRect2D& view, go::vf4 srgba_bg) -> void;
    // More generic draw_start: control depth, multisampling and attachment ops
    auto draw_start(VkCommandBuffer cmd, const VkRect2D& view,
        go::vf4 srgba_bg, bool enable_depth, bool enable_msaa,
        VkAttachmentLoadOp colorLoadOp, VkAttachmentStoreOp colorStoreOp,
        VkAttachmentLoadOp depthLoadOp, VkAttachmentStoreOp depthStoreOp) -> void;
    // Explicit-image-view draw_start: provide image views for color/depth/resolve
    auto draw_start(
        VkCommandBuffer cmd, const VkRect2D& view,
        go::vf4 srgba_bg, VkImageView color_view, VkImageView depth_view,
        VkImageView resolve_view, bool enable_msaa,
        VkAttachmentLoadOp colorLoadOp, VkAttachmentStoreOp colorStoreOp,
        VkAttachmentLoadOp depthLoadOp, VkAttachmentStoreOp depthStoreOp) -> void;
    auto view_set(VkCommandBuffer cmd, const VkRect2D& view) -> void;
    auto scissors_set(VkCommandBuffer cmd, const VkRect2D& view) -> void;
    auto viewport_set(VkCommandBuffer cmd, const VkRect2D& view) -> void;
    auto draw_end(VkCommandBuffer cmd) -> void;

private:
    OneWindow() = default;
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // tos