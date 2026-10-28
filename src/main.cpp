#include <print>

#include <engi/OneWindow.hpp>
#include <engi/SkiaRenderer.hpp>

int main()
{
    auto w = engi::OneWindow::create(800,600, "The Temple of Speed");
    if (!w)
        return 1;

    auto skia = engi::SkiaRenderer::create(w->get_instance(), w->get_physical_device(), w->get_device(),
        w->get_queue_graphics(), w->get_queue_graphics_family());
    if (!skia)
    {
        std::println("[ERROR] Failed to create SkiaRenderer");
        return 1;
    }
    
    while(!w->should_close())
    {
        auto res = w->acquire();

        const bool ui = skia->begin_frame(res.vkImage, res.format, res.width, res.height);
        if (ui) skia->draw_rectangle(20, 20, 200, 60, 0.2f, 0.5f, 1.f, 1.f);

        auto cmd = w->cmd_start();
        w->draw_start(cmd, go::vf4{0.1f, 0.1f, 0.1f, 1.0f});
        w->draw_end(cmd);

        VkImageMemoryBarrier2 b = 
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .dstStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = res.vkImage,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        };
        VkDependencyInfo dep = 
        {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &b,
        };
        vkCmdPipelineBarrier2(cmd, &dep);

        w->cmd_end();
        auto signal = w->submit_start();
        if (signal)
        {
            auto skia_sem = skia->end_frame(*signal, res.frame);
            w->submit_end(skia_sem ? skia_sem : *signal);
        }
        w->check_events(true);
    }
    return 0;
}