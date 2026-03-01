/**
 * Moza Steering Wheel – Linux evdev interface
 * Uses universal-pidff kernel module for full HID PID force-feedback support.
 *
 * Build:
 *   g++ -std=c++17 -o moza main.cpp
 *
 * Run:
 *   sudo ./moza              – read-only, FFB disabled (default)
 *   sudo ./moza --ffb        – enable force-feedback demo
 *   sudo ./moza /dev/input/eventX          – explicit device, no FFB
 *   sudo ./moza /dev/input/eventX --ffb    – explicit device + FFB
 *
 * Permissions shortcut (no sudo after relogin):
 *   sudo usermod -aG input $USER
 *
 * universal-pidff:
 *   https://github.com/berarma/universal-pidff
 *   lsmod | grep pidff   ← check if loaded
 */

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>
#include <linux/input.h>

namespace fs = std::filesystem;

// ── helpers ──────────────────────────────────────────────────────────────────

static bool test_bit(const uint8_t* bitmask, int bit)
{
    return (bitmask[bit / 8] >> (bit % 8)) & 1;
}

// ── device discovery ─────────────────────────────────────────────────────────

struct DeviceInfo {
    std::string path;
    std::string name;
    uint16_t    vendor  = 0;
    uint16_t    product = 0;
};

static std::vector<DeviceInfo> find_moza_devices()
{
    constexpr uint16_t MOZA_VENDOR = 0x346E;
    std::vector<DeviceInfo> result;

    for (auto& entry : fs::directory_iterator("/dev/input")) {
        const std::string path = entry.path().string();
        if (path.find("event") == std::string::npos) continue;

        int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        char name[256] = {};
        struct input_id id{};
        ::ioctl(fd, EVIOCGNAME(sizeof(name)), name);
        ::ioctl(fd, EVIOCGID, &id);
        ::close(fd);

        std::string lower(name);
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        if (lower.find("moza") != std::string::npos || id.vendor == MOZA_VENDOR)
            result.push_back({path, name, id.vendor, id.product});
    }
    return result;
}

// ── capability reporting ─────────────────────────────────────────────────────

static const char* abs_name(int code)
{
    switch (code) {
        case ABS_X:     return "Steering";
        case ABS_Y:     return "Throttle";
        case ABS_Z:     return "Brake";
        case ABS_RZ:    return "Clutch";
        case ABS_HAT0X: return "DPad-X";
        case ABS_HAT0Y: return "DPad-Y";
        default:        return "ABS_?";
    }
}

static void print_abs_info(int fd, int code)
{
    struct input_absinfo info{};
    if (::ioctl(fd, EVIOCGABS(code), &info) == 0) {
        std::cout << "    " << abs_name(code)
                  << "  [" << info.minimum << " .. " << info.maximum << "]"
                  << "  flat=" << info.flat
                  << "  fuzz=" << info.fuzz << '\n';
    }
}

static void print_ff_capabilities(int fd)
{
    uint8_t ffbits[(FF_MAX + 7) / 8] = {};
    if (::ioctl(fd, EVIOCGBIT(EV_FF, sizeof(ffbits)), ffbits) < 0) {
        std::cout << "  (could not query FF capabilities)\n";
        return;
    }

    struct { int code; const char* name; } effects[] = {
        { FF_CONSTANT,   "Constant"   },
        { FF_RAMP,       "Ramp"       },
        { FF_SPRING,     "Spring"     },
        { FF_DAMPER,     "Damper"     },
        { FF_INERTIA,    "Inertia"    },
        { FF_FRICTION,   "Friction"   },
        { FF_SINE,       "Sine"       },
        { FF_SQUARE,     "Square"     },
        { FF_TRIANGLE,   "Triangle"   },
        { FF_SAW_UP,     "Saw-Up"     },
        { FF_SAW_DOWN,   "Saw-Down"   },
        { FF_CUSTOM,     "Custom"     },
        { FF_GAIN,       "Gain"       },
        { FF_AUTOCENTER, "Autocenter" },
    };

    std::cout << "  Supported FF effects: ";
    bool any = false;
    for (auto& e : effects) {
        if (test_bit(ffbits, e.code)) {
            if (any) std::cout << ", ";
            std::cout << e.name;
            any = true;
        }
    }
    if (!any) std::cout << "none";
    std::cout << '\n';

    // Hint about universal-pidff if the list looks sparse
    if (!test_bit(ffbits, FF_SPRING) && !test_bit(ffbits, FF_DAMPER)) {
        std::cout << "  [hint] Spring/Damper missing – is universal-pidff loaded?\n"
                  << "         lsmod | grep pidff\n"
                  << "         https://github.com/berarma/universal-pidff\n";
    }
}

// ── force-feedback effects ────────────────────────────────────────────────────

static int ff_play(int fd, int id)
{
    struct input_event ev{};
    ev.type  = EV_FF;
    ev.code  = static_cast<uint16_t>(id);
    ev.value = 1;
    if (::write(fd, &ev, sizeof(ev)) < 0) { std::perror("ff_play"); return -1; }
    return id;
}

static int ff_stop(int fd, int id)
{
    if (id < 0) return -1;
    struct input_event ev{};
    ev.type  = EV_FF;
    ev.code  = static_cast<uint16_t>(id);
    ev.value = 0;
    ::write(fd, &ev, sizeof(ev));
    ::ioctl(fd, EVIOCRMFF, id);
    return -1;
}

/** Constant force. level: −0x7FFF (left) … +0x7FFF (right) */
static int ff_constant(int fd, int16_t level)
{
    struct ff_effect e{};
    e.type               = FF_CONSTANT;
    e.id                 = -1;
    e.direction          = 0x4000;   // 90° = rightward
    e.replay.length      = 0xFFFF;
    e.u.constant.level   = level;
    e.u.constant.envelope = {};

    if (::ioctl(fd, EVIOCSFF, &e) < 0) { std::perror("EVIOCSFF constant"); return -1; }
    return ff_play(fd, e.id);
}

/** Spring (centering). saturation controls max restoring force. */
static int ff_spring(int fd, int16_t saturation = 0x7FFF, int16_t deadband = 0)
{
    struct ff_effect e{};
    e.type          = FF_SPRING;
    e.id            = -1;
    e.replay.length = 0xFFFF;

    e.u.condition[0].right_saturation = saturation;
    e.u.condition[0].left_saturation  = saturation;
    e.u.condition[0].right_coeff      = 0x7FFF;
    e.u.condition[0].left_coeff       = 0x7FFF;
    e.u.condition[0].deadband         = deadband;
    e.u.condition[0].center           = 0;
    e.u.condition[1]                  = e.u.condition[0];

    if (::ioctl(fd, EVIOCSFF, &e) < 0) { std::perror("EVIOCSFF spring"); return -1; }
    return ff_play(fd, e.id);
}

/** Damper – resists wheel movement (velocity-based). */
static int ff_damper(int fd, int16_t coeff = 0x3000)
{
    struct ff_effect e{};
    e.type          = FF_DAMPER;
    e.id            = -1;
    e.replay.length = 0xFFFF;

    e.u.condition[0].right_saturation = 0x7FFF;
    e.u.condition[0].left_saturation  = 0x7FFF;
    e.u.condition[0].right_coeff      = coeff;
    e.u.condition[0].left_coeff       = coeff;
    e.u.condition[1]                  = e.u.condition[0];

    if (::ioctl(fd, EVIOCSFF, &e) < 0) { std::perror("EVIOCSFF damper"); return -1; }
    return ff_play(fd, e.id);
}

/** Inertia – simulates rotational mass (acceleration-based). */
static int ff_inertia(int fd, int16_t coeff = 0x2000)
{
    struct ff_effect e{};
    e.type          = FF_INERTIA;
    e.id            = -1;
    e.replay.length = 0xFFFF;

    e.u.condition[0].right_saturation = 0x7FFF;
    e.u.condition[0].left_saturation  = 0x7FFF;
    e.u.condition[0].right_coeff      = coeff;
    e.u.condition[0].left_coeff       = coeff;
    e.u.condition[1]                  = e.u.condition[0];

    if (::ioctl(fd, EVIOCSFF, &e) < 0) { std::perror("EVIOCSFF inertia"); return -1; }
    return ff_play(fd, e.id);
}

/** Friction – resists position change (position-based drag). */
static int ff_friction(int fd, int16_t coeff = 0x2000)
{
    struct ff_effect e{};
    e.type          = FF_FRICTION;
    e.id            = -1;
    e.replay.length = 0xFFFF;

    e.u.condition[0].right_saturation = 0x7FFF;
    e.u.condition[0].left_saturation  = 0x7FFF;
    e.u.condition[0].right_coeff      = coeff;
    e.u.condition[0].left_coeff       = coeff;
    e.u.condition[1]                  = e.u.condition[0];

    if (::ioctl(fd, EVIOCSFF, &e) < 0) { std::perror("EVIOCSFF friction"); return -1; }
    return ff_play(fd, e.id);
}

/** Periodic sine – road rumble / vibration. period_ms controls frequency. */
static int ff_sine(int fd, uint16_t period_ms = 100, int16_t magnitude = 0x4000)
{
    struct ff_effect e{};
    e.type                 = FF_PERIODIC;
    e.id                   = -1;
    e.direction            = 0x4000;
    e.replay.length        = 0xFFFF;
    e.u.periodic.waveform  = FF_SINE;
    e.u.periodic.period    = period_ms;
    e.u.periodic.magnitude = magnitude;
    e.u.periodic.offset    = 0;
    e.u.periodic.phase     = 0;
    e.u.periodic.envelope  = {};

    if (::ioctl(fd, EVIOCSFF, &e) < 0) { std::perror("EVIOCSFF sine"); return -1; }
    return ff_play(fd, e.id);
}

// ── FFB demo ──────────────────────────────────────────────────────────────────

static void run_ffb_demo(int fd)
{
    std::cout << "\n[FFB] Demo sequence – each effect lasts 2 seconds\n";

    auto run = [&](const char* label, int id) {
        if (id < 0) { std::cout << "  " << label << " – SKIPPED\n"; return; }
        std::cout << "  " << label << " (id=" << id << ") …" << std::flush;
        ::sleep(2);
        ff_stop(fd, id);
        std::cout << " done\n";
    };

    run("Constant +25%",  ff_constant(fd,  0x2000));
    run("Constant −25%",  ff_constant(fd, -0x2000));
    run("Spring",         ff_spring  (fd,  0x4000));
    run("Damper",         ff_damper  (fd,  0x3000));
    run("Inertia",        ff_inertia (fd,  0x2000));
    run("Friction",       ff_friction(fd,  0x2000));
    run("Sine 100 ms",    ff_sine    (fd, 100, 0x4000));
    run("Sine  50 ms",    ff_sine    (fd,  50, 0x3000));

    std::cout << "[FFB] Demo done\n\n";
}

// ── event loop ────────────────────────────────────────────────────────────────

static void event_loop(const std::string& path, bool ffb_enabled)
{
    int flags = ffb_enabled ? (O_RDWR | O_NONBLOCK) : (O_RDONLY | O_NONBLOCK);
    int fd    = ::open(path.c_str(), flags);

    if (fd < 0 && ffb_enabled) {
        std::cout << "[warn] O_RDWR failed, falling back to read-only (FFB disabled)\n";
        fd          = ::open(path.c_str(), O_RDONLY | O_NONBLOCK);
        ffb_enabled = false;
    }
    if (fd < 0) { std::perror("open"); return; }

    // ---- device banner ----
    char name[256] = {};
    struct input_id devid{};
    ::ioctl(fd, EVIOCGNAME(sizeof(name)), name);
    ::ioctl(fd, EVIOCGID, &devid);
    std::cout << "\nDevice  : " << name
              << "\nVID:PID : 0x" << std::hex << devid.vendor
              << ":0x"            << devid.product << std::dec << '\n';

    // ---- axes ----
    uint8_t absbits[(ABS_MAX + 7) / 8] = {};
    ::ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absbits)), absbits);
    std::cout << "\nAbsolute axes:\n";
    bool any_abs = false;
    for (int c : {ABS_X, ABS_Y, ABS_Z, ABS_RZ, ABS_HAT0X, ABS_HAT0Y}) {
        if (test_bit(absbits, c)) { print_abs_info(fd, c); any_abs = true; }
    }
    if (!any_abs) std::cout << "  (none detected)\n";

    // ---- FF capabilities ----
    std::cout << "\nForce-feedback:\n";
    print_ff_capabilities(fd);

    if (!ffb_enabled)
        std::cout << "\n[FFB disabled – pass --ffb to run the demo]\n";
    else
        run_ffb_demo(fd);

    // ---- main read loop ----
    std::cout << "\nListening for input events (Ctrl-C to quit)…\n\n";

    while (true) {
        struct input_event ev{};
        ssize_t n = ::read(fd, &ev, sizeof(ev));

        if (n < 0) {
            if (errno == EAGAIN) { ::usleep(1000); continue; }
            std::perror("read");
            break;
        }
        if (n < static_cast<ssize_t>(sizeof(ev))) continue;

        switch (ev.type) {
            case EV_ABS:
                std::cout << "[ABS] " << abs_name(ev.code)
                          << " = " << ev.value << '\n';
                break;
            case EV_KEY:
                std::cout << "[BTN] code=" << ev.code
                          << (ev.value ? " PRESSED" : " RELEASED") << '\n';
                break;
            case EV_REL:
                std::cout << "[REL] code=" << ev.code
                          << " delta=" << ev.value << '\n';
                break;
            case EV_SYN:
                break;
            default:
                break;
        }
    }

    ::close(fd);
}

// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[])
{
    std::string device_path;
    bool ffb_enabled = false;   // ← off by default

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--ffb")
            ffb_enabled = true;
        else
            device_path = arg;
    }

    if (device_path.empty()) {
        auto devices = find_moza_devices();
        if (devices.empty()) {
            std::cerr
                << "No Moza device found under /dev/input/\n\n"
                << "Diagnostics:\n"
                << "  ls /dev/input/by-id/ | grep -i moza\n"
                << "  cat /proc/bus/input/devices | grep -A8 -i moza\n"
                << "  lsmod | grep pidff          # universal-pidff loaded?\n"
                << "\nOr pass the path explicitly:\n"
                << "  ./moza /dev/input/eventX\n";
            return 1;
        }

        std::cout << "Found " << devices.size() << " Moza device(s):\n";
        for (size_t i = 0; i < devices.size(); ++i) {
            std::cout << "  [" << i << "] " << devices[i].path
                      << "  \"" << devices[i].name << "\""
                      << "  vid=0x" << std::hex << devices[i].vendor
                      << "  pid=0x" << devices[i].product << std::dec << '\n';
        }
        device_path = devices[0].path;
        std::cout << "Using:  " << device_path << '\n';
    }

    event_loop(device_path, ffb_enabled);
    return 0;
}