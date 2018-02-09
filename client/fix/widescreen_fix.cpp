#include "widescreen_fix.h"

#include <string.h>
#include "../client_signature.h"
#include "../halo_data/resolution.h"
#include "../hooks/map_load.h"
#include "../hooks/frame.h"
#include "../hooks/tick.h"
#include "../hud_mod/offset_hud_elements.h"
#include "../messaging/messaging.h"
#include "../open_sauce.h"

int widescreen_fix_active = 0;

OffsetterIndex index = OFFSETTER_INDEX_NULL;

static float width_scale = 0;

extern void apply_scope_fix();
extern void undo_scope_fix();

static float **letterbox;

static int menu_extra_width = 0;
static int *cursor_x;

static void check_cursor() noexcept {
    static bool firstrun = true;
    int min = menu_extra_width/2 * -1 - 1;
    int max = menu_extra_width/2 + 640 - 1;
    if(firstrun) {
        *cursor_x = min;
        firstrun = false;
    }
    if(*cursor_x > max) {
        *cursor_x = max;
    }
    else if(*cursor_x < min) {
        *cursor_x = min;
    }
}

static void set_mod(bool force) noexcept {
    auto &resolution = get_resolution();
    float aspect_ratio = static_cast<float>(resolution.width) / resolution.height;
    float new_width_scale = aspect_ratio / (4.0 / 3.0);
    auto scale_changed = width_scale != new_width_scale;
    if(scale_changed || force) {
        static float adder = 1.0;
        static float adder_negative = -1.0;
        width_scale = new_width_scale;
        float p_scale = 1.0/(320.0 * new_width_scale);
        adder = 1.0 / new_width_scale;
        adder_negative = -(1.0 + 1.0 / 640.0) / new_width_scale;
        menu_extra_width = 640 * width_scale - 640;

        auto *hud_element_widescreen_sig_address = get_signature("hud_element_widescreen_sig").address();
        write_code_any_value(hud_element_widescreen_sig_address + 0x7, p_scale);
        write_code_any_value(hud_element_widescreen_sig_address + 0xC0 + 2, &adder);

        auto *hud_element_motion_sensor_blip_widescreen_address = get_signature("hud_element_motion_sensor_blip_widescreen_sig").address();
        write_code_any_value(hud_element_motion_sensor_blip_widescreen_address + 0x4, p_scale);
        write_code_any_value(hud_element_motion_sensor_blip_widescreen_address + 0x1B + 3 + 4, adder_negative);

        auto *hud_text_widescreen_sig = get_signature("hud_text_widescreen_sig").address();
        write_code_any_value(hud_text_widescreen_sig + 2 + 4, p_scale);
        write_code_any_value(hud_text_widescreen_sig + 0x1E + 2 + 4, adder_negative);

        auto *hud_nav_widescreen_sig_address = get_signature("hud_nav_widescreen_sig").address();
        write_code_any_value(reinterpret_cast<unsigned char *>(*reinterpret_cast<float **>(hud_nav_widescreen_sig_address + 2)), static_cast<float>(640.0 * width_scale));

        static float nav_scale = 320.0;
        nav_scale = -320.0 * (width_scale - 1);
        static unsigned char instructions[] = {0xD8, 0x05, 0xFF, 0xFF, 0xFF, 0xFF, 0xDA, 0x04, 0x24, 0xD9, 0x19, 0xE9, 0xFF, 0xFF, 0xFF, 0xFF};

        *reinterpret_cast<float **>(instructions + 2) = &nav_scale;
        auto *ins = instructions + 11;
        *reinterpret_cast<uint32_t *>(ins + 1) = hud_nav_widescreen_sig_address + 11 - (ins + 5);
        write_code_any_value(hud_nav_widescreen_sig_address + 6, static_cast<unsigned char>(0xE9));
        write_code_any_value(hud_nav_widescreen_sig_address + 7, instructions - (hud_nav_widescreen_sig_address + 11));

        if(widescreen_fix_active == 1) {
            auto offset_sig = [](ChimeraSignature &signature) {
                const auto &offset = *reinterpret_cast<const int16_t *>(signature.signature() + 5);
                write_code_any_value(signature.address() + 5, static_cast<int16_t>(offset - 320 + 320 * width_scale));
            };

            offset_sig(get_signature("team_icon_ctf_sig"));
            offset_sig(get_signature("team_icon_slayer_sig"));
            offset_sig(get_signature("team_icon_king_sig"));
            offset_sig(get_signature("team_icon_race_sig"));
            offset_sig(get_signature("team_icon_oddball_sig"));
            offset_sig(get_signature("team_icon_background_sig"));

            destroy_offsetter(index);
            index = create_offsetter(320.0 - 320.0 * width_scale, 0, true);
        }

        apply_scope_fix();
    }
}

static void on_map_load() noexcept {
    set_mod(true);
}

static void apply_offsets() noexcept {
    set_mod(false);
    **letterbox = -1;
}

ChimeraCommandError widescreen_fix_command(size_t argc, const char **argv) noexcept {
    extern bool widescreen_scope_mask_active;
    if(argc == 1) {
        int new_value = bool_value(argv[0]);
        if(new_value == false) new_value = atol(argv[0]);
        if(new_value < 0 || new_value > 2) {
            console_out_error("chimera_widescreen_fix: Expected a value between 0 and 2");
            return CHIMERA_COMMAND_ERROR_FAILURE;
        }

        if(new_value != widescreen_fix_active) {
            destroy_offsetter(index);
            index = OFFSETTER_INDEX_NULL;

            remove_tick_event(apply_offsets);
            remove_map_load_event(on_map_load);

            get_signature("team_icon_ctf_sig").undo();
            get_signature("team_icon_slayer_sig").undo();
            get_signature("team_icon_king_sig").undo();
            get_signature("team_icon_race_sig").undo();
            get_signature("team_icon_oddball_sig").undo();
            get_signature("team_icon_background_sig").undo();
            get_signature("cursor_sig").undo();
            get_signature("hud_text_fix_1_sig").undo();
            get_signature("hud_text_fix_2_sig").undo();
            get_signature("hud_text_fix_3_sig").undo();

            letterbox = *reinterpret_cast<float ***>(get_signature("letterbox_sig").address() + 2);
            switch(new_value) {
                case 0: {
                    get_signature("hud_element_widescreen_sig").undo();
                    get_signature("hud_element_motion_sensor_blip_widescreen_sig").undo();
                    get_signature("hud_text_widescreen_sig").undo();
                    get_signature("hud_nav_widescreen_sig").undo();

                    auto &hud_nav_widescreen_sig = get_signature("hud_nav_widescreen_sig");
                    hud_nav_widescreen_sig.undo();
                    write_code_any_value(reinterpret_cast<unsigned char *>(*reinterpret_cast<float **>(hud_nav_widescreen_sig.address() + 2)), static_cast<float>(640.0));

                    undo_scope_fix();
                    width_scale = 0;
                    break;
                }
                case 1:
                case 2: {
                    if(open_sauce_present()) {
                        console_out_warning("chimera_widescreen_fix is not compatible with Open Sauce.");
                        console_out_warning("Using chimera_widescreen_scope_fix, instead...");
                        execute_chimera_command("chimera_widescreen_scope_fix 1");
                        return CHIMERA_COMMAND_ERROR_SUCCESS;
                    }
                    if(widescreen_scope_mask_active) {
                        execute_chimera_command("chimera_widescreen_scope 0", true);
                    }

                    auto &cursor_sig = get_signature("cursor_sig");
                    auto *cursor_sig_address = cursor_sig.address();
                    cursor_x = *reinterpret_cast<int **>(cursor_sig_address + 4);
                    unsigned char nope[256];
                    memset(nope, 0x90, sizeof(nope));
                    write_code_any_array(cursor_sig_address, nope, cursor_sig.size());

                    write_code_any_value(get_signature("hud_text_fix_1_sig").address(), static_cast<unsigned char>(0xEB));
                    write_code_any_value(get_signature("hud_text_fix_2_sig").address(), static_cast<unsigned char>(0xEB));
                    write_code_any_value(get_signature("hud_text_fix_3_sig").address(), static_cast<unsigned char>(0xEB));

                    add_tick_event(apply_offsets);
                    add_map_load_event(on_map_load);
                    add_preframe_event(check_cursor);
                    break;
                }
                default: std::terminate();
            }
            widescreen_fix_active = new_value;
            if(widescreen_fix_active > 0) on_map_load();
        }
    }
    console_out(std::to_string(widescreen_fix_active));
    return CHIMERA_COMMAND_ERROR_SUCCESS;
}
