#include <string>

#include "client.h"
#include "../version.h"

#include "update_notifier/update_notifier.h"

#include "client_signature.h"
#include "path.h"
#include "settings.h"
#include "command/console.h"
#include "command/command.h"

#include "controller_fix/magnetism_fix.h"

#include "debug/budget.h"
#include "debug/wireframe.h"

#include "enhancements/anisotropic_filtering.h"
#include "enhancements/auto_center.h"
#include "enhancements/firing_particle.h"
#include "enhancements/multitexture_overlay.h"
#include "enhancements/safe_zone.h"
#include "enhancements/show_spawn.h"
#include "enhancements/skip_loading.h"
#include "enhancements/throttle_fps.h"
#include "enhancements/uncap_cinematic.h"
#include "enhancements/zoom_blur.h"

#include "hac2/scope_fix.h"

#include "hooks/camera.h"
#include "hooks/frame.h"
#include "hooks/rcon_message.h"
#include "hooks/tick.h"

#include "interpolation/interpolation.h"

#include "lua/lua.h"

#include "messaging/messaging.h"

extern std::vector<ChimeraSignature> *signatures;
extern std::vector<std::string> *missing_signatures;
std::vector<ChimeraCommand> *commands;

bool initial_tick = true;

// workaround for wine users - disable the update notifier because it crashes
static bool wine_used() {
    HMODULE hntdll = GetModuleHandle("ntdll.dll");
    if(!hntdll) {
        return false;
    }

    return GetProcAddress(hntdll, "wine_get_version") != nullptr;
}

static void init() {
    extern bool save_settings;
    extern bool autosave;
    extern bool already_set;
    auto &enabled = **reinterpret_cast<char **>(get_signature("enable_console_sig").address() + 1);
    already_set = enabled != 0;
    if(!already_set)
        enabled = 1;
    remove_tick_event(init);
    auto settings_before = save_settings;
    save_settings = false;
    read_init_file("chimerainit.txt");
    save_settings = true;
    char z[512] = {};
    sprintf(z,"%s\\chimera", halo_path());
    CreateDirectory(z, nullptr);
    sprintf(z,"%s\\chimera\\chimerasave.txt", halo_path());
    autosave = false;
    read_init_file(z);
    autosave = true;
    save_all_changes();
    save_settings = settings_before;
    initial_tick = false;

    setup_lua();
}

void initialize_client() noexcept {
    commands = new std::vector<ChimeraCommand>;
    signatures = new std::vector<ChimeraSignature>;
    missing_signatures = new std::vector<std::string>;
    if(!find_required_signatures()) {
        for(size_t i=0;i<(*missing_signatures).size();i++) {
            char message[256] = {};
            sprintf(message, "Could not find %s signature. Make sure you're using Halo Custom Edition version 1.10.", (*missing_signatures)[i].data());
            MessageBox(NULL, message, "Chimera cannot load", MB_OK);
        }
        return;
    }
    initialize_console();
    initialize_rcon_message();
    add_tick_event(init);
    if(!wine_used()) add_tick_event(check_updater);

    if(find_magnetism_signatures()) {
        fix_magnetism();
    }

    (*commands).emplace_back("chimera", chimera_command, nullptr,
        "This command is the commands directory for Chimera.\n\n"
        "Syntax:\n"
        "  - chimera\n    Display version and a list of command categories.\n"
        "  - chimera <category>\n    Display a list of commands in category.\n"
        "  - chimera <command>\n    Display help for a command.\n"
    , 0, 1, true);

    (*commands).emplace_back("chimera_block_update_notifier", block_update_notifier_command, nullptr,
        "Get or set whether or not to block update notifications.\n\n"
        "Syntax:\n"
        "  - chimera_block_update_notifier [true/false]"
    , 0, 1, !wine_used(), true);

    (*commands).emplace_back("chimera_verbose_init", verbose_init_command, nullptr,
        "Set whether or not chimerainit.txt commands should output messages.\n\n"
        "Syntax:\n"
        "  - chimera_verbose_init [max FPS]"
    , 0, 1, true);

    (*commands).emplace_back("chimera_reload_lua", reload_lua_command, "lua",
        "Reload all Lua scripts.\n\n"
        "Syntax:\n"
        "  - chimera_reload_lua"
    , 0, 0, true);

    // Interpolation

    (*commands).emplace_back("chimera_interpolate", interpolate_command, "interpolation",
        "Get or set the interpolation level. Interpolation smoothens out object movement between\n"
        "ticks, providing a substantial visual improvement. Higher levels incur greater CPU usage and\n"
        "may impact framerate on slower CPUs.\n\n"
        "Syntax:\n"
        "  - chimera_interpolate [off / low / medium / high / ultra]"
    , 0, 1, find_interpolation_signatures(), true);

    (*commands).emplace_back("chimera_interpolate_predict", interpolate_predict_command, "interpolation",
        "Get or set whether the next tick should be predicted when interpolating. This will prevent\n"
        "objects from appearing as if they are one tick behind, but sudden object movement may\n"
        "cause jitteriness.\n\n"
        "Syntax:\n"
        "  - chimera_interpolate_predict [true/false]"
    , 0, 1, find_interpolation_signatures(), true);

    // Enhancements

    (*commands).emplace_back("chimera_af", af_command, "enhancements",
        "Get or set whether or not to enable anisotropic filtering.\n\n"
        "Syntax:\n"
        "  - chimera_af [true/false]"
    , 0, 1, find_anisotropic_filtering_signature(), true);

    (*commands).emplace_back("chimera_auto_center", auto_center_command, "enhancements",
        "Get or set how auto centering of vehicle cameras should behave.\n"
        "Options:\n"
        "  0: Broken stock behavior (default)\n"
        "  1: Fixed behavior\n"
        "  2: Disable automatic centering\n\n"
        "Syntax:\n"
        "  - chimera_auto_center [0-2]"
    , 0, 1, find_auto_center_signature(), true);

    (*commands).emplace_back("chimera_block_firing_particles", block_firing_particles_command, "enhancements",
        "Get or set whether or not to block firing particles.\n\n"
        "Syntax:\n"
        "  - block_firing_particles [true/false]"
    , 0, 1, true, true);

    (*commands).emplace_back("chimera_block_mo", block_mo_command, "enhancements",
        "Get or set whether or not to disable multitexture overlays. This feature is intended to fixthe\n"
        "buggy HUD on the stock sniper rifle, but multitexture overlays may be used correctly on\n"
        "some maps.\n\n"
        "Syntax:\n"
        "  - chimera_block_mo [true/false]"
    , 0, 1, find_multitexture_overlay_signature(), true);

    (*commands).emplace_back("chimera_block_zoom_blur", block_zoom_blur_command, "enhancements",
        "Get or set whether or not to disable the zoom blur.\n\n"
        "Syntax:\n"
        "  - chimera_block_zoom_blur [true/false]"
    , 0, 1, find_zoom_blur_signatures(), true);

    (*commands).emplace_back("chimera_enable_console", enable_console_command, "enhancements",
        "Get or set whether or not to automatically enable the console.\n"
        "Unlike most other features, this feature is enabled by default.\n\n"
        "Syntax:\n"
        "  - chimera_enable_console [true/false]"
    , 0, 1, true, true);

    (*commands).emplace_back("chimera_safe_zones", safe_zones_command, "enhancements",
        "Get or set whether or not to emulate Xbox safe zones.\n\n"
        "Syntax:\n"
        "  - chimera_safe_zones [true/false]"
    , 0, 1, true, true);

    (*commands).emplace_back("chimera_show_spawns", show_spawns_command, "enhancements",
        "Get or set whether or not to show spawns.\n\n"
        "Syntax:\n"
        "  - chimera_show_spawns [true/false]"
    , 0, 1, true, true);

    (*commands).emplace_back("chimera_skip_loading", skip_loading_command, "enhancements",
        "Get or set whether or not to skip the multiplayer loading screen.\n\n"
        "Syntax:\n"
        "  - chimera_skip_loading [true/false]"
    , 0, 1, find_loading_screen_signatures(), true);

    (*commands).emplace_back("chimera_throttle_fps", throttle_fps_command, "enhancements",
        "Throttle Halo's framerate. This uses Halo's built-in throttler, but modifies the minimum\n"
        "frame time. Enabling this will also enable chimera_uncap_cinematic.\n\n"
        "Syntax:\n"
        "  - chimera_throttle_fps [max FPS]"
    , 0, 1, find_uncap_cinematic_signatures(), true);

    (*commands).emplace_back("chimera_uncap_cinematic", uncap_cinematic_command, "enhancements",
        "Get or set whether or not to remove the 30 FPS framerate cap in cinematics. This may result\n"
        "in objects jittering during cutscenes if chimera_interpolate is not enabled.\n\n"
        "Syntax:\n"
        "  - chimera_uncap_cinematic [true/false]"
    , 0, 1, find_uncap_cinematic_signatures(), true);

    // HAC2

    (*commands).emplace_back("chimera_widescreen_scope_mask", widescreen_scope_fix_command, "hac2",
        "Enhance HAC2's widescreen fix by also fixing the scope mask.\n\n"
        "Syntax:\n"
        "  - chimera_widescreen_scope_mask [true/false]"
    , 0, 1, find_widescreen_signatures(), true);

    // Debug

    (*commands).emplace_back("chimera_budget", budget_command, "debug",
        "Get or set whether or show or hide various budgets.\n"
        "Options:\n"
        "  0: off\n"
        "  1: on (used custom maximums if a mod is used [e.g. HAC2])\n"
        "  2: on (use stock maximums regardless of if a mod is used)\n\n"
        "Syntax:\n"
        "  - chimera_budget [0-2]"
    , 0, 1, find_debug_signatures(), false);

    (*commands).emplace_back("chimera_wireframe", wireframe_command, "debug",
        "Get or set whether or enable or disable wireframe mode. This will not work while in a server.\n\n"
        "Syntax:\n"
        "  - chimera_wireframe [true/false]"
    , 0, 1, find_debug_signatures(), false);
}

void uninitialize_client() noexcept {
    destroy_lua();
    for(size_t i=0;i<signatures->size();i++) {
        (*signatures)[i].undo();
    }
    delete signatures;
    signatures = nullptr;
    delete missing_signatures;
    missing_signatures = nullptr;
    delete commands;
    commands = nullptr;
}