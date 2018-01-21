#include "lua.h"
#include <memory>
#include <boost/filesystem.hpp>
#include <windows.h>
#include <math.h>
#include "lua_callback.h"
#include "lua_game.h"
#include "lua_io.h"
#include "../halo_data/map.h"
#include "../halo_data/server.h"
#include "../halo_data/table.h"
#include "../messaging/messaging.h"
#include "../path.h"
#include "../../version.h"

#include <vector>
#include <memory>

std::vector<std::unique_ptr<LuaScript>> scripts;

bool disable_safeguards = false;

void refresh_variables(lua_State *state) noexcept {
    lua_pushnumber(state, client_player_index());
    lua_setglobal(state, "local_player_index");

    lua_pushstring(state, get_map_header().name);
    lua_setglobal(state, "map");

    const char *server;
    bool on_server = true;
    switch(server_type()) {
        case SERVER_NONE:
            server = "none";
            on_server = false;
            break;
        case SERVER_DEDICATED:
            server = "dedicated";
            break;
        case SERVER_LOCAL:
            server = "local";
            break;
    }
    lua_pushstring(state, server);
    lua_setglobal(state, "server_type");

    if(on_server) {
        const char *current_gametype;
        switch(gametype()) {
            case GAMETYPE_CTF:
                current_gametype = "ctf";
                break;
            case GAMETYPE_SLAYER:
                current_gametype = "slayer";
                break;
            case GAMETYPE_KING:
                current_gametype = "king";
                break;
            case GAMETYPE_ODDBALL:
                current_gametype = "oddball";
                break;
            case GAMETYPE_RACE:
                current_gametype = "race";
                break;
        }
        lua_pushstring(state, server);
    }
    else {
        lua_pushnil(state);
    }
    lua_setglobal(state, "gametype");
}

static void load_lua_script(const char *script_name, const char *lua_script_data, size_t lua_script_data_size, bool unlocked, bool global) noexcept {
    auto *state = luaL_newstate();

    luaL_openlibs(state);

    if(!unlocked) {
        const char *sandbox =  "io = nil\
                                dofile = nil\
                                getfenv = nil\
                                load = nil\
                                loadfile = nil\
                                loadstring = nil\
                                require = nil\
                                os.execute = nil\
                                os.exit = nil\
                                os.remove = nil\
                                os.rename = nil\
                                os.tmpname = nil\
                                require = nil";
        if(luaL_loadbuffer(state, sandbox, strlen(sandbox), script_name) != LUA_OK || lua_pcall(state, 0, 0, 0) != LUA_OK) {
            print_error(state);
            lua_close(state);
            return;
        }
    }

    set_io_functions(state);
    set_game_functions(state);

    refresh_variables(state);
    lua_pushboolean(state, !unlocked);
    lua_setglobal(state, "sandboxed");

    lua_pushstring(state, global ? "global" : "map");
    lua_setglobal(state, "script_type");

    #ifdef CHIMERA_ALPHA_VERSION
    lua_pushinteger(state, CHIMERA_ALPHA_VERSION);
    #else
    lua_pushinteger(state, CHIMERA_BUILD_NUMBER);
    #endif
    lua_setglobal(state, "build");

    lua_pushinteger(state, CHIMERA_BUILD_NUMBER);
    lua_setglobal(state, "full_build");

    scripts.push_back(std::make_unique<LuaScript>(state, script_name, global, unlocked));

    if(luaL_loadbuffer(state, lua_script_data, lua_script_data_size, script_name) != LUA_OK || lua_pcall(state, 0, 0, 0) != LUA_OK) {
        console_out_error(std::string("Failed to load ") + script_name + ".");
        print_error(state);
        lua_close(state);
        scripts.erase(scripts.begin() + scripts.size() - 1);
        return;
    }

    lua_getglobal(state, "clua_version");
    if(lua_isnumber(state, -1)) {
        double version = lua_tonumber(state, -1);
        if(version > CHIMERA_LUA_INTERPRETER) {
            console_out_warning(std::string(script_name) + " was made for a newer version of Chimera.");
            console_out_warning("It may possibly not work as intended.");
        }
        else if(static_cast<int>(version) < static_cast<int>(CHIMERA_LUA_INTERPRETER)) {
            console_out_warning(std::string(script_name) + " was made for a much older version of Chimera.");
            console_out_warning("It may possibly not work as intended.");
        }
    }
    else {
        console_out_warning(std::string(script_name) + " does not have clua_version defined.");
        console_out_warning("It may possibly not work as intended.");
    }
    lua_pop(state, 1);

    script_from_state(state).loaded = true;
}

void load_map_script() noexcept {
    auto path = std::string(halo_path()) + "\\chimera\\lua\\map\\" + get_map_header().name + ".lua";
    auto map = std::string(get_map_header().name) + ".map";
    FILE *f = fopen(path.data(), "rb+");
    if(f) {
        fseek(f, 0, SEEK_END);
        size_t size = ftell(f);
        fseek(f, 0, SEEK_SET);
        auto d = std::make_unique<char[]>(size);
        fread(d.get(), size, 1, f);
        load_lua_script(map.data(), d.get(), size, false, false);
        fclose(f);
    }
    else {
        auto *header = reinterpret_cast<char *>(&get_map_header());
        auto *scripting_data = *reinterpret_cast<char **>(header + 0x310);
        uint32_t scripting_data_size = *reinterpret_cast<uint32_t *>(header + 0x314);
        if(scripting_data_size && scripting_data) {
            load_lua_script(map.data(), scripting_data, scripting_data_size, false, false);
        }
    }
}

static void setup_lua_folder() {
    char z[512] = {};
    sprintf(z,"%s\\chimera\\lua", halo_path());
    CreateDirectory(z, nullptr);
    sprintf(z,"%s\\chimera\\lua\\map", halo_path());
    CreateDirectory(z, nullptr);
    sprintf(z,"%s\\chimera\\lua\\global", halo_path());
    CreateDirectory(z, nullptr);
}

namespace fs = boost::filesystem;

static void open_lua_scripts() {
    fs::path f = std::string(halo_path()) + "\\chimera\\lua\\global";
    fs::directory_iterator iterator{f};
    while (iterator != fs::directory_iterator{}) {
        auto path = (*iterator).path();
        if(path.extension() == ".lua") {
            FILE *f = fopen(path.string().data(), "rb+");
            if(f) {
                fseek(f, 0, SEEK_END);
                size_t size = ftell(f);
                fseek(f, 0, SEEK_SET);
                auto d = std::make_unique<char[]>(size);
                fread(d.get(), size, 1, f);
                load_lua_script(path.filename().string().data(), d.get(), size, true, true);
                fclose(f);
            }
        }
        iterator++;
    }
    load_map_script();
}

static bool set_up = false;

void setup_lua() {
    set_up = true;
    setup_callbacks();
    setup_lua_folder();
    open_lua_scripts();
}

void destroy_lua() {
    scripts.clear();
}

LuaScript &script_from_state(lua_State *state) noexcept {
    for(size_t i=0;i<scripts.size();i++) {
        auto &s = *scripts[i].get();
        if(s.state == state) return s;
    }
    std::terminate();
}

void print_error(lua_State *state) noexcept {
    console_out_error(lua_tostring(state, -1));
    lua_pop(state, 1);
}

LuaScript::LuaScript(lua_State *state, const char *name, const bool &global, const bool &unlocked) noexcept : state(state), name(name), unlocked(unlocked), global(global) {}

LuaScript::~LuaScript() noexcept {
    if(this->loaded) {
        lua_getglobal(this->state, this->c_unload.callback_function.data());
        if(!lua_isnil(this->state, -1) && lua_pcall(this->state, 0, 0, 0) != LUA_OK) {
            print_error(this->state);
        }
        lua_close(this->state);
    }
}

ChimeraCommandError reload_lua_command(size_t argc, const char **argv) noexcept {
    scripts.clear();
    open_lua_scripts();
    console_out("chimera_reload_lua: Scripts were reloaded.");
    return CHIMERA_COMMAND_ERROR_SUCCESS;
}