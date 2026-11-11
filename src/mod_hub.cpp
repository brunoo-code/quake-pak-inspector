#include "pak.hpp"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

void limit_script(lua_State* state, lua_Debug*) {
    luaL_error(state, "instruction limit exceeded");
}

class Rules {
public:
    explicit Rules(const char* file) : state_(luaL_newstate(), lua_close) {
        if (!state_) throw std::runtime_error("Cannot create Lua state");
        auto* vm = state_.get();
        luaL_requiref(vm, "string", luaopen_string, 1);
        lua_pop(vm, 1);
        lua_sethook(vm, limit_script, LUA_MASKCOUNT, 100000);
        check(luaL_loadfilex(vm, file, "t"));
        check(lua_pcall(vm, 0, 1, 0));
        if (!lua_isfunction(vm, -1)) throw std::runtime_error("Rules must return a function");
        classifier_ = luaL_ref(vm, LUA_REGISTRYINDEX);
    }

    std::string classify(const std::string& path, bool changed) {
        auto* vm = state_.get();
        lua_rawgeti(vm, LUA_REGISTRYINDEX, classifier_);
        lua_pushlstring(vm, path.data(), path.size());
        lua_pushboolean(vm, changed);
        lua_sethook(vm, limit_script, LUA_MASKCOUNT, 100000);
        check(lua_pcall(vm, 2, 1, 0));
        std::size_t size = 0;
        const auto* value = lua_type(vm, -1) == LUA_TSTRING ? lua_tolstring(vm, -1, &size) : nullptr;
        if (!value || size == 0 || size > 32) throw std::runtime_error("Invalid category from Lua");
        std::string category(value, size);
        lua_pop(vm, 1);
        if (category.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") !=
            std::string::npos) {
            throw std::runtime_error("Lua category must be a short ASCII identifier");
        }
        return category;
    }

private:
    void check(int status) {
        if (status == LUA_OK) return;
        const auto* message = lua_tostring(state_.get(), -1);
        throw std::runtime_error(message ? message : "Lua execution failed");
    }

    std::unique_ptr<lua_State, decltype(&lua_close)> state_;
    int classifier_ = LUA_NOREF;
};

struct Layer {
    std::filesystem::path path;
    pak::Archive archive;
    explicit Layer(const char* filename) : path(filename), archive(path) {}
};

struct Provider {
    std::size_t layer;
    pak::Entry entry;
};

void audit(Rules& rules, std::vector<Layer>& layers) {
    std::map<std::string, std::vector<Provider>> paths;
    for (std::size_t i = 0; i < layers.size(); ++i) {
        for (const auto& [name, entry] : layers[i].archive.entries()) {
            paths[name].push_back({i, entry});
        }
    }
    std::size_t collisions = 0;
    for (const auto& [name, providers] : paths) {
        if (providers.size() < 2) continue;
        ++collisions;
        const auto& winner = providers.back();
        const auto& active = layers[winner.layer].path;
        for (auto it = providers.begin(); it != providers.end() - 1; ++it) {
            const bool changed = !pak::same_content(layers[it->layer].archive, it->entry,
                                                    layers[winner.layer].archive, winner.entry);
            std::cout << rules.classify(name, changed) << '\t'
                      << (changed ? "OVERRIDE" : "DUPLICATE") << '\t' << name << '\t'
                      << layers[it->layer].path.filename().string() << " -> "
                      << active.filename().string() << '\n';
        }
    }
    std::cout << paths.size() << " indexed paths; " << collisions << " with multiple providers\n";
}
}

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: pak-mod-hub RULES.lua BASE.pak MOD.pak [MOD.pak ...]\n";
        return 2;
    }
    try {
        Rules rules(argv[1]);
        std::vector<Layer> layers;
        layers.reserve(static_cast<std::size_t>(argc - 2));
        for (int i = 2; i < argc; ++i) layers.emplace_back(argv[i]);
        audit(rules, layers);
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
