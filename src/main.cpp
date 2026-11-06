#include <print>

#include <engi/DeviceFeatures.hpp>
#include <engi/OneWindow.hpp>
#include <engi/SkiaRenderer.hpp>
#include <engi/Pipeline.hpp>
#include <engi/DescriptorHeap.hpp>

int main()
{
    auto w = engi::OneWindow::create(800,600, "The Temple of Speed");
    if (!w)
        return 1;

    auto skia = engi::SkiaRenderer::create(
        w->get_instance(), 
        w->get_physical_device(), 
        w->get_device(), 
        w->get_allocator(), 
        w->get_command_pool(), 
        w->get_queue_graphics(), 
        w->get_queue_graphics_family());
    if (!skia)
    {
        std::println("[ERROR] Failed to create SkiaRenderer");
        return 1;
    }

    auto surface = skia->create_surface(800, 600);
    if (!surface)
    {
        std::println("[ERROR] Failed to create SkiaSurface");
        return 1;
    }

    auto pipeline = engi::PipelineBuilder(w->get_device())
        .vertex_shader_from_file("shaders/SkiaSurface.vert.spv")
        .fragment_shader_from_file("shaders/SkiaSurface.frag.spv")
        .color_format(w->get_color_format())
        .depth_format(w->get_depth_format())
        //.samples(VK_SAMPLE_COUNT_1_BIT)
        //.topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
        //.polygon_mode(VK_POLYGON_MODE_FILL)
        .cull_mode(VK_CULL_MODE_NONE)
        .front_face(VK_FRONT_FACE_COUNTER_CLOCKWISE)
        .depth_test(false)
        .depth_write(false)
        .alpha_blending()
        .build();
    if (!pipeline)
    {
        std::println("[ERROR] Failed to create SkiaSurface pipeline");
        return 1;
    }
    else
    {
        std::println("[INFO] SkiaSurface pipeline created successfully");
    }

    auto heap = engi::DescriptorHeap::create(w->get_physical_device(), w->get_device(), w->get_allocator(), {});
    if (!heap)
    {
        std::println("[ERROR] Failed to create descriptor heap");
        return 1;
    }
    else
    {
        std::println("[INFO] Descriptor heap created successfully");
    }
    struct SkiaPush { float src_rect[4]; uint32_t tex; uint32_t smp; };   // 24 bytes
    std::array<SkiaPush, engi::FRAMES_FLYING> push_data = {};
    auto assign_descriptors = [&push_data, &heap](const engi::SkiaSurface& surface)
    {
        for (size_t i = 0; i < engi::FRAMES_FLYING; ++i)
        {
            uint32_t ui_slot  = heap->alloc_resource_slot();
            uint32_t smp_slot = heap->alloc_sampler_slot();
            VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            vci.image = surface.image(i);  
            vci.viewType = VK_IMAGE_VIEW_TYPE_2D;  
            vci.format = surface.format();
            vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            heap->write_sampled_image(ui_slot, vci, VK_IMAGE_LAYOUT_GENERAL);  // Using GENERAL layout for Skia surface

            VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
            sci.magFilter = sci.minFilter = VK_FILTER_LINEAR;
            sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            heap->write_sampler(smp_slot, sci);

            push_data[i] = SkiaPush{ {0.f, 0.f, 1.f, 1.f}, heap->image_index(ui_slot), heap->sampler_index(smp_slot) };
        }
    };
    assign_descriptors(*surface);
    
    while(!w->should_close())
    {
        auto res = w->acquire();
        if(res.resized_and_waited)
        {
            surface = skia->create_surface(w->get_size().first, w->get_size().second);
            if (!surface)
            {
                std::println("[ERROR] Failed to create SkiaSurface");
                return 1;
            }

            assign_descriptors(*surface);
        }

        skia->draw_rectangle(*surface, res.frame, 20, 20, 200, 60, 0.2f, 0.5f, 1.f, 1.f);
        skia->draw_text(*surface, res.frame, 30, 50, "Hello Skia!", 1.f, 1.f, 1.f, 1.f);
        skia->submit(*surface, VK_NULL_HANDLE, res.frame);
        skia->finalize();

        auto cmd = w->cmd_start();
        w->draw_start(cmd, go::vf4{0.1f, 0.1f, 0.1f, 1.0f});
        w->draw_end(cmd);
        {
            VkImageMemoryBarrier2 overlay_barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            overlay_barrier.srcStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            overlay_barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            overlay_barrier.dstStageMask  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            overlay_barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            overlay_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            overlay_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            overlay_barrier.image = surface->image(res.frame);
            overlay_barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

            VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            dep.imageMemoryBarrierCount = 1;
            dep.pImageMemoryBarriers = &overlay_barrier;
            vkCmdPipelineBarrier2(cmd, &dep);
        }

        w->draw_start(cmd, { {0, 0}, {800, 600} }, go::vf4{0.1f, 0.1f, 0.1f, 1.0f},
            false, false,
            VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
            VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_DONT_CARE);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->get());

        heap->bind(cmd);   // skip if already bound earlier in this command buffer
        heap->push(cmd, push_data[res.frame], 0);
        vkCmdDraw(cmd, 3, 1, 0, 0);

        w->draw_end(cmd);

        w->cmd_end();
        w->submit_present();
        w->check_events(true);
    }

    w->wait_idle();
    return 0;
}