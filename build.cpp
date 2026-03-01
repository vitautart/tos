// How to use:
// > g++ -O2 -o bld build.cpp
// > ./bld

#include <cstdlib>
#include <stdio.h>
#include <string>
#include <string_view>
#include <filesystem>
#include <iostream>
#include <vector>
#include <optional>
#include <array>

namespace fs = std::filesystem;

static const fs::path BUILD_FOLDER = fs::path("build");
static const fs::path BUILD_TEMP_FOLDER = BUILD_FOLDER / fs::path("temp");
static const fs::path BUILD_SHADER_FOLDER = BUILD_FOLDER / fs::path("shaders");
static const fs::path APP_NAME = fs::path("engi");
static const fs::path APP_PATH = BUILD_FOLDER / APP_NAME;
static const fs::path GLFW_LIB_PATH = BUILD_FOLDER / fs::path("libglfw.a");
static const char* ADDITIONAL_COMPILER_FLAGS = "-std=c++23 -DENGI_RENDER_DEBUG"" " 
                                                "-Isrc -Iexternal/glfw/include"" "
                                                "-Iexternal/vma -Iexternal/stb" " ";
static const char* ADDITIONAL_COMPILER_FLAGS_GLFW = "-w -O3 -D_GLFW_X11=1 -Iexternal/glfw/src -Iexternal/glfw/include -fpermissive"" ";
static const char* ADDITIONAL_LINK_FLAGS = "-Lbuild -lvulkan -ldl -lpthread -lX11 -lglfw"; 
//static const char* ADDITIONAL_LINK_FLAGS_GLFW = "-ldl -lm -lpthread";

#define RENDER_DIR "src/render"
#define PIPELINES_DIR RENDER_DIR"/pipelines"
static const std::array CPP_FILES = 
{
    "src/main.cpp",
    "src/app.cpp",
    "src/rendering.cpp",
    "src/rendering_overlay.cpp",
    "src/pipeline.cpp",
    "src/layout.cpp",
    "src/uniform_buffer.cpp",
    "src/static_buffer.cpp",
    "src/dynamic_buffer.cpp",
    "src/font_atlas.cpp",
    "src/ui_system.cpp"
};

#define GLFW_SRC "external/glfw/src/"
static const std::array GLFW_CPP_FILES = 
{
    GLFW_SRC"window.c",
    GLFW_SRC"xkb_unicode.c",
    GLFW_SRC"glx_context.c",
    GLFW_SRC"x11_window.c", 
    GLFW_SRC"monitor.c", 
    GLFW_SRC"posix_thread.c", 
    GLFW_SRC"x11_init.c", 
    GLFW_SRC"context.c", 
    GLFW_SRC"posix_time.c", 
    GLFW_SRC"x11_monitor.c", 
    GLFW_SRC"linux_joystick.c", 
    GLFW_SRC"input.c", 
    GLFW_SRC"init.c", 
    GLFW_SRC"vulkan.c", 
    GLFW_SRC"osmesa_context.c", 
    GLFW_SRC"egl_context.c",
};

static const std::array SHADERS =
{
    "src/shaders/cube.vert",
    "src/shaders/cube.frag",
    "src/shaders/text.vert",
    "src/shaders/text.frag",
    "src/shaders/rect.vert",
    "src/shaders/rect.frag",
    "src/shaders/rect_wire.vert",
    "src/shaders/rect_wire.frag",
};

static const fs::path IOSEVKA_TTF = fs::path("external/iosevka/iosevka-fixed-regular.ttf");
static const fs::path BUILD_RESOURCE_FONTS = BUILD_FOLDER / fs::path("resources") / fs::path("fonts");

auto linux_call(const char* command) -> std::string 
{
    constexpr size_t MAX_STR_ALLOC = 1024;
    std::string data = {};
    FILE* cmd = popen(command, "r");
    if (!cmd)
        return data;

    char result[MAX_STR_ALLOC]={0x0};
    while (fgets(result, MAX_STR_ALLOC - 1, cmd) != nullptr)
        data += &result[0];
    pclose(cmd);
    return data;
}

std::time_t to_time_t(fs::file_time_type tp)
{
    using namespace std::chrono;
    auto sctp = time_point_cast<system_clock::duration>(
            tp - fs::file_time_type::clock::now() + system_clock::now());
    return system_clock::to_time_t(sctp);
}

auto get_write_time(const fs::path& filename) -> time_t
{
    return to_time_t(fs::last_write_time(filename));
}

// Format to parse is <obj>: cpp heade1 header2 ...
auto parse_deps_str_and_check_is_changed(const std::string_view deps, const std::string_view target) -> bool
{
    auto target_time = get_write_time(target);
    enum class State 
    {
        NONE,
        PREFIX,
        WORD,
    } state = State::PREFIX;
    size_t start;
    for (size_t i = 0; i < deps.size(); i++)
    {
        auto c = deps[i];
        if (state == State::PREFIX && c == ':')
        {
            state = State::NONE;
        }
        else if (state == State::NONE && !(std::isspace(c) || c == '\\'))
        {
            state = State::WORD;
            start = i;
        }
        else if (state == State::WORD && (std::isspace(c) || c == '\\'))
        {
            state = State::NONE;
            auto dep = deps.substr(start, i - start);
            if (!fs::exists(dep))
            {
                std::cout << "[BUILD][WARNING] Could not find file: " << dep << std::endl;
                return true;
            }
            auto time = get_write_time(dep);
            if (time > target_time)
                return true;
        }
    }

    return false;
}

auto get_output_filepath(const fs::path& input) -> fs::path
{
    const auto rel = input.relative_path();
    std::string output = {};
    for (auto it = rel.begin(); it != rel.end(); it++)
    {
        if (*it != "..")
            output += *it;
        else
            output += "xx";
    }
    output += ".o";

    return BUILD_TEMP_FOLDER / output;
}

auto compile_file(const fs::path& filename_cpp, const char* flags) -> std::optional<fs::path>
{
    auto output = get_output_filepath(filename_cpp).string();
    bool should_rebuild = true;

    std::string compiler = "g++";
    if (filename_cpp.extension() == ".c")
        compiler = "gcc";

    if (fs::exists(output))
    {
        std::string command_to_get_deps  = compiler + " -MM ";
        command_to_get_deps += filename_cpp.string() + " " + flags;
        auto deps_str = linux_call(command_to_get_deps.c_str());

        should_rebuild = parse_deps_str_and_check_is_changed(deps_str, output);
    }

    if (should_rebuild)
    {
        std::string compile_command = compiler + " -c " + filename_cpp.c_str() 
            + " -o " + output + " " 
            + flags; 
        std::cout << "[COMPILE] " << compile_command << std::endl;
        int res = system(compile_command.c_str());
        if (res != 0)
            return std::nullopt;
    }
    return output;
}

auto link_files_dynamic(const std::vector<std::optional<fs::path>>& files, const fs::path output_path, const char* flags) -> bool
{
    bool has_app = fs::exists(output_path);
    time_t app_time = has_app ? get_write_time(output_path) : time_t{};

    bool should_rebuild = !has_app;
    auto link_command = std::string("g++ -o ") + output_path.string() + " "; 
    for (const auto& f: files)
    {
        if (!f.has_value())
            return false;
        should_rebuild = should_rebuild || (get_write_time(f.value()) > app_time);
        link_command += f->string() + " ";
    }
    // LINKING flags -L and -l should be at the end
    // https://stackoverflow.com/questions/18827938/strange-g-linking-behavior-depending-on-arguments-order
    link_command += flags;
    bool result = true;
    if (should_rebuild)
    {
        std::cout << "[LINK] " << link_command << std::endl;
        if (files.empty())
        {
            printf("[ERROR] No files to link!\n");
            return false;
        }
        result = system(link_command.c_str()) == 0;
    }
    return result;
}

auto link_files_static(const std::vector<std::optional<fs::path>>& files, const fs::path output_path) -> bool
{
    bool has_app = fs::exists(output_path);
    time_t app_time = has_app ? get_write_time(output_path) : time_t{};

    bool should_rebuild = !has_app;
    auto link_command = std::string("ar rsv ") + output_path.string() + " "; 
    for (const auto& f: files)
    {
        if (!f.has_value())
            return false;
        should_rebuild = should_rebuild || (get_write_time(f.value()) > app_time);
        link_command += f->string() + " ";
    }

    bool result = true;
    if (should_rebuild)
    {
        std::cout << "[LINK] " << link_command << std::endl;
        result = system(link_command.c_str()) == 0;
    }
    return result;
}

auto compile_shader(const fs::path& shader_txt) -> void
{
    fs::path output = BUILD_SHADER_FOLDER / 
        fs::path{ shader_txt.stem().string() + "_" + shader_txt.extension().string().substr(1) + ".spv" };

    bool should_rebuild = false;
    if (fs::exists(output))
    {
        if (fs::exists(shader_txt))
            should_rebuild = get_write_time(shader_txt) > get_write_time(output);
        else
            should_rebuild = true;
    }
    else
        should_rebuild = true;

    if (should_rebuild)
    {
        std::string compile_command  = "glslc ";
        compile_command += shader_txt.string() + " -o ";
        compile_command += output.string();

        std::cout << "[COMPILE] " << compile_command << std::endl;
        system(compile_command.c_str());
    }
}

auto copy_resource_if_changed(const fs::path& src, const fs::path& dest_dir) -> bool
{
    if (!fs::exists(src))
    {
        std::cout << "[BUILD][WARNING] Resource not found: " << src << std::endl;
        return false;
    }

    if (!fs::exists(dest_dir))
        fs::create_directories(dest_dir);

    fs::path dest = dest_dir / src.filename();

    bool should_copy = true;
    if (fs::exists(dest))
        should_copy = get_write_time(src) > get_write_time(dest);

    if (should_copy)
    {
        std::cout << "[COPY] " << src << " -> " << dest << std::endl;
        try
        {
            fs::copy_file(src, dest, fs::copy_options::overwrite_existing);
        }
        catch (const std::exception& e)
        {
            std::cout << "[BUILD][ERROR] Failed to copy resource: " << e.what() << std::endl;
            return false;
        }
    }
    return true;
}

auto run_app()
{
    std::string run_command  = std::string("cd ") + BUILD_FOLDER.string() 
        + " && " + "./" + APP_NAME.string()
        + " && cd ..";
    system(run_command.c_str());
}

int main(int argc, char *argv[])
{
    if (!fs::exists(BUILD_FOLDER))
        fs::create_directory(BUILD_FOLDER);

    if (!fs::exists(BUILD_TEMP_FOLDER))
        fs::create_directory(BUILD_TEMP_FOLDER);

    if (!fs::exists(BUILD_SHADER_FOLDER))
        fs::create_directory(BUILD_SHADER_FOLDER);

    std::vector<std::optional<fs::path>> o_files = {};
    std::vector<std::optional<fs::path>> o_glfw_files = {};

    for (auto cpp :GLFW_CPP_FILES)
        o_glfw_files.push_back(compile_file(cpp, ADDITIONAL_COMPILER_FLAGS_GLFW));

    if (!link_files_static(o_glfw_files, GLFW_LIB_PATH))
        return -1;

    for (auto cpp : CPP_FILES)
        o_files.push_back(compile_file(cpp, ADDITIONAL_COMPILER_FLAGS));

    for (auto shader : SHADERS)
        compile_shader(shader);

    // Copy font resource into build/resources/fonts if source changed
    copy_resource_if_changed(IOSEVKA_TTF, BUILD_RESOURCE_FONTS);

    bool link_success = link_files_dynamic(o_files, APP_PATH, ADDITIONAL_LINK_FLAGS);
    
    if (link_success && (argc > 1) && (std::string(argv[1]) == "run"))
        run_app();

    return link_success ? 0 : -1;
}
