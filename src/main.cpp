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
        w->cmd_end();
        //w->present(false);
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