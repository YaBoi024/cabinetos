#include "optcheck.h"

#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <vector>

#include "catalog.h"
#include "quality.h"
#include "screenview.h"
#include "storage.h"
#include "sysopts.h"
#include "vectrexsheets.h"
#include "wii.h"

namespace optcheck {

namespace {

// key -> accepted values, from one "key\tdefault\tv|v|v" file.
std::map<std::string, std::set<std::string>> readList(const std::string& path) {
    std::map<std::string, std::set<std::string>> out;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        const size_t a = line.find('\t');
        if (a == std::string::npos) continue;
        const size_t b = line.find('\t', a + 1);
        std::set<std::string>& values = out[line.substr(0, a)];
        if (b == std::string::npos) continue;
        std::stringstream ss(line.substr(b + 1));
        std::string v;
        while (std::getline(ss, v, '|')) values.insert(v);
    }
    return out;
}

// Options whose values are files the core finds on the console, so a listing
// made with an empty system folder shows none of them. Opera lists the BIOS
// images it finds; with none, only "disabled". Only the key is checked.
bool valuesAreFiles(const std::string& key) { return key == "opera_bios"; }

}  // namespace

int run(const std::string& dir) {
    // EVERY LIBRETRO CORE THE CATALOG PLAYS A SYSTEM ON, and those systems,
    // rather than the lists that happen to be in the folder: a core with no
    // list had nothing checked and passed (#398).
    const std::map<std::string, std::vector<std::string>> cores = catalog::libretroCores();
    int bad = 0, checked = 0;
    for (const auto& [core, slugs] : cores) {
        const std::string list = dir + "/" + core + ".txt";
        if (!storage::exists(list)) {
            std::fprintf(stderr, "[optcheck] %s has no option list (%s)\n", core.c_str(),
                         list.c_str());
            ++bad;
            continue;
        }
        const auto declared = readList(list);
        // Everything the console can set on this core, with where it came from.
        std::vector<std::pair<std::pair<std::string, std::string>, std::string>> sets;
        // The catalog's choices on any machine: with Vulkan and without (the
        // N64's renderer), the sensor bar above the TV and below. Each value
        // once.
        std::set<std::pair<std::string, std::string>> fromCatalog;
        for (bool vk : {true, false})
            for (bool above : {true, false})
                for (const auto& kv : catalog::optionOverrides(core, vk, above))
                    fromCatalog.emplace(kv.first, kv.second);
        for (const auto& kv : fromCatalog) sets.push_back({kv, "catalog"});
        // And a DS or 3DS game's screens, every view (screenview.h).
        for (int v = 0; v < screenview::kViewCount; ++v)
            for (const auto& kv : screenview::coreOptions(core, static_cast<screenview::View>(v)))
                sets.push_back({kv, "screen view"});
        for (const std::string& slug : slugs) {
            for (int l = 0; l < quality::kLevelCount; ++l)
                for (bool vk : {true, false})
                    for (bool desktopGL : {true, false})
                        for (const auto& kv : quality::coreOptions(
                                 core, slug, static_cast<quality::Level>(l), vk, desktopGL))
                            sets.push_back({kv, std::string("quality ") +
                                                    quality::levelWord(
                                                        static_cast<quality::Level>(l))});
            for (const sysopts::Option& o : sysopts::forSystem(slug))
                for (const sysopts::Choice& c : o.choices)
                    for (const auto& kv : c.sets)
                        sets.push_back({{kv.first, kv.second}, std::string("pause menu ") + o.id});
            // And the Wii's rumble switch, both ways (#274).
            for (bool on : {true, false})
                for (const auto& kv : wii::coreOptions(core, slug, on))
                    sets.push_back({kv, "wii rumble"});
            // And the Vectrex sheets' fit, both ways (#224).
            for (bool fitted : {true, false})
                for (const auto& kv : vectrexsheets::coreOptions(core, slug, fitted))
                    sets.push_back({kv, "vectrex sheet fit"});
        }
        for (const auto& [kv, from] : sets) {
            ++checked;
            auto it = declared.find(kv.first);
            if (it == declared.end()) {
                std::fprintf(stderr, "[optcheck] %s does not offer %s (%s sets it to %s)\n",
                             core.c_str(), kv.first.c_str(), from.c_str(), kv.second.c_str());
                ++bad;
            } else if (!it->second.empty() && !it->second.count(kv.second) &&
                       !valuesAreFiles(kv.first)) {
                std::fprintf(stderr, "[optcheck] %s: %s does not accept %s (%s)\n",
                             core.c_str(), kv.first.c_str(), kv.second.c_str(), from.c_str());
                ++bad;
            }
        }
    }
    std::fprintf(stderr, "[optcheck] %d values on %zu cores, %d that do not fit\n", checked,
                 cores.size(), bad);
    return bad == 0 ? 0 : 1;
}

}  // namespace optcheck
