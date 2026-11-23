// The core's options: the table a core declares, what each key is answered
// with, our overrides and the live change, and the option calls of the
// environment callback (environmentOptions). Moved out of core.cpp in the
// restructure (#398); resetOptions and checkOverrides are called from
// Core::load and Core::loadGame there, and what the host's files share is in
// core_state.h.

#include "core.h"
#include "core_state.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "libretro.h"

namespace cab {
namespace corestate {

// --- Core options -----------------------------------------------------------
//
// AN UNANSWERED OPTION IS NOT THE DEFAULT. It is the worst value in the list,
// and it fails silently.
//
// A core reads its settings by asking the frontend for each variable in turn.
// When the frontend does not answer, the core does NOT fall back to the default
// printed in its own option table — the whole case is skipped and the C global
// keeps whatever it was initialised to, which is zero. Zero means silence for a
// sample rate, black for brightness, and off for every toggle whose useful
// state is on. This cost the reference implementation eight separate evenings,
// one option at a time.
//
// core.cpp used to say the opposite, in as many words, above a map that
// nothing ever wrote to. So every option of every core went unanswered, and
// twenty cores have been running on zeroes.
//
// THE FIX IS TO CAPTURE THE TABLE THE CORE DECLARES and answer every key in it.
// A core announces its options through one of three generations of the same
// idea, and a frontend has to take whichever it is offered:
//
//   SET_VARIABLES          the original. "Description; first|second|third",
//                          and the FIRST value is the default by convention.
//   SET_CORE_OPTIONS       adds an explicit `default_value` rather than relying
//                          on ordering, plus per-value labels.
//   SET_CORE_OPTIONS_V2    adds categories. Same defaults.
//
// Each has an _INTL variant that wraps a US table and a localised one; the US
// table is the one with the keys in it, so that is the one read.
//
// We report version 2, because the explicit `default_value` is a fact the core
// states rather than a convention we infer from ordering.
struct Option {
    std::string key;
    std::string desc;
    std::vector<std::string> values;
    std::string defaultValue;   // what the core says
    std::string chosen;         // what we answer, which may be an override
    bool overridden = false;
    bool asked = false;         // did the core actually come back for it
};

std::vector<Option> gDeclared;                              // declaration order
std::unordered_map<std::string, size_t> gByKey;             // key -> gDeclared
std::unordered_map<std::string, std::string> gOverrides;    // our deliberate choices
std::map<std::string, std::string> gCommandLine;            // --core-option, on top of them
// Keys a core asked for that it never declared. Not answerable, and worth
// counting: it means either the core has a bug or we missed a table.
std::vector<std::string> gUndeclaredAsks;

// THE CONTROL. With this on, GET_VARIABLE answers nothing, which is exactly
// what this host did before the table was captured — so the difference the fix
// makes can be measured rather than asserted.
//
// docs/PROJECT.md: "Run the control before believing a comparison." The
// backend-diff tool exists for the same reason, and it earned its keep by
// showing that a difference everyone believed in was the build id.
bool gAnswerOptions = true;

// Set by Core::setOptionLive, answered once by GET_VARIABLE_UPDATE.
bool gOptionsUpdated = false;

void resetOptions() {
    gOptionsUpdated = false;
    gDeclared.clear();
    gByKey.clear();
    gUndeclaredAsks.clear();
}

// AN UPGRADE THAT RENAMED A SETTING IS SILENT WITHOUT THIS (#63, MMagTech
// 2026-10-02). A value we give for a key the core never declares is simply
// never asked for, and a value outside its list is handed over as it is; a
// core bump that renamed `dolphin_efb_scale` would quietly play every game at
// native resolution. So once a game is in, every value we set is checked
// against what the core declared, and anything that does not fit is logged.
// It reaches us through the diagnostic report (#195). The pin-moving workflow
// catches the same thing before an image is built; this covers the cores that
// declare their options only with a game loaded, and every console after.
void checkOverrides(const std::string& core) {
    int bad = 0;
    for (const auto& [key, value] : gOverrides) {
        auto it = gByKey.find(key);
        if (it == gByKey.end()) {
            std::fprintf(stderr, "[options] %s does not offer %s (we set it to %s)\n",
                         core.c_str(), key.c_str(), value.c_str());
            ++bad;
            continue;
        }
        const Option& o = gDeclared[it->second];
        if (!o.values.empty() &&
            std::find(o.values.begin(), o.values.end(), value) == o.values.end()) {
            std::fprintf(stderr, "[options] %s: %s does not accept %s\n", core.c_str(),
                         key.c_str(), value.c_str());
            ++bad;
        }
    }
    if (!gOverrides.empty() && bad == 0)
        std::fprintf(stderr, "[options] %s: all %zu of our settings fit\n", core.c_str(),
                     gOverrides.size());
}

void declareOption(const char* key, const char* desc,
                   std::vector<std::string> values, const char* defaultValue) {
    if (!key || !*key) return;
    // A core may declare its table more than once — the API explicitly allows
    // re-declaration to update descriptions — so a repeat replaces rather than
    // duplicates.
    auto it = gByKey.find(key);
    const size_t idx = (it == gByKey.end()) ? gDeclared.size() : it->second;
    if (it == gByKey.end()) {
        gDeclared.emplace_back();
        gByKey[key] = idx;
    }
    Option& o = gDeclared[idx];
    o.key = key;
    o.desc = desc ? desc : "";
    o.values = std::move(values);
    // An explicit default wins; otherwise the first listed value, which is what
    // the original API documents: "First entry should be treated as a default."
    if (defaultValue && *defaultValue) o.defaultValue = defaultValue;
    else if (!o.values.empty()) o.defaultValue = o.values.front();
    else o.defaultValue.clear();

    auto ov = gOverrides.find(o.key);
    o.overridden = (ov != gOverrides.end());
    o.chosen = o.overridden ? ov->second : o.defaultValue;
}

// "Description; first|second|third" — the original format. Everything before
// the first ';' is prose; the rest is the value list.
void captureVariables(const retro_variable* vars) {
    if (!vars) return;
    for (; vars->key; ++vars) {
        std::string desc, list;
        if (vars->value) {
            const std::string v = vars->value;
            const size_t semi = v.find(';');
            if (semi == std::string::npos) {
                list = v;
            } else {
                desc = v.substr(0, semi);
                list = v.substr(semi + 1);
                // The spec says the ';' is followed by a space. Trim whatever
                // whitespace is actually there rather than assuming exactly one.
                size_t b = list.find_first_not_of(" \t");
                list = (b == std::string::npos) ? std::string() : list.substr(b);
            }
        }
        std::vector<std::string> values;
        size_t start = 0;
        while (start <= list.size() && !list.empty()) {
            const size_t bar = list.find('|', start);
            values.push_back(list.substr(start, bar == std::string::npos
                                                    ? std::string::npos
                                                    : bar - start));
            if (bar == std::string::npos) break;
            start = bar + 1;
        }
        declareOption(vars->key, desc.c_str(), std::move(values), nullptr);
    }
}

void captureDefinitions(const retro_core_option_definition* defs) {
    if (!defs) return;
    for (; defs->key; ++defs) {
        std::vector<std::string> values;
        for (const retro_core_option_value* v = defs->values; v && v->value; ++v)
            values.emplace_back(v->value);
        declareOption(defs->key, defs->desc, std::move(values), defs->default_value);
    }
}

void captureV2(const retro_core_options_v2* opts) {
    if (!opts || !opts->definitions) return;
    for (const retro_core_option_v2_definition* d = opts->definitions; d->key; ++d) {
        std::vector<std::string> values;
        for (const retro_core_option_value* v = d->values; v && v->value; ++v)
            values.emplace_back(v->value);
        declareOption(d->key, d->desc, std::move(values), d->default_value);
    }
}

// The option calls of the environment callback. core.cpp's environment asks
// here before its own switch; nothing for any other call.
std::optional<bool> environmentOptions(unsigned cmd, void* data) {
    switch (cmd) {
        case RETRO_ENVIRONMENT_GET_VARIABLE: {
            auto* var = static_cast<retro_variable*>(data);
            const std::string key = var->key ? var->key : "";
            if (!gAnswerOptions) {
                // The control: the old behaviour, where every option went
                // unanswered and every core silently ran on zeroes.
                var->value = nullptr;
                return false;
            }
            auto it = gByKey.find(key);
            if (it == gByKey.end()) {
                // A key the core never declared. There is nothing honest to
                // answer with — we do not know its values, let alone its
                // default — so it is recorded and reported rather than guessed
                // at. This is the one case that stays unanswered, and RetroArch
                // leaves it unanswered too.
                //
                // MAME 2003-PLUS DOES THIS ON PURPOSE, and it is the only core
                // here that does it every launch (#89, read in its source at
                // the pinned commit, src/mame2003/core_options.c): it declares
                // only the options the loaded game can use (vector settings for
                // a vector game, the Neo Geo BIOS for a Neo Geo game, crosshairs
                // for a light gun game...) but asks for every one, and keeps its
                // own value for any the frontend does not answer. Whatever is
                // relevant to the game is declared and answered; the rest do
                // not apply. So its list of "never declared" is expected, not a
                // table we failed to read. Anywhere else it is worth a look.
                var->value = nullptr;
                if (!key.empty() &&
                    std::find(gUndeclaredAsks.begin(), gUndeclaredAsks.end(), key) ==
                        gUndeclaredAsks.end()) {
                    gUndeclaredAsks.push_back(key);
                    std::fprintf(stderr, "[core] asked for an option it never "
                                         "declared: %s\n", key.c_str());
                }
                return false;
            }
            Option& o = gDeclared[it->second];
            o.asked = true;
            var->value = o.chosen.c_str();
            return true;
        }

        // TRUE ONCE AFTER setOptionLive, so a core that re-reads its options
        // on this picks the change up on its next frame. False otherwise, as
        // it always was: options were only ever set before a game started.
        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
            *static_cast<bool*>(data) = gOptionsUpdated;
            gOptionsUpdated = false;
            return true;

        // The core telling us what it can be configured with. Whichever
        // generation of the API it uses, the table is captured and every key in
        // it gets an answer — see the note above `struct Option`.
        case RETRO_ENVIRONMENT_SET_VARIABLES:
            captureVariables(static_cast<const retro_variable*>(data));
            return true;
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
            captureDefinitions(
                static_cast<const retro_core_option_definition*>(data));
            return true;
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL:
            // The US table is the one carrying the keys; `local` is the same
            // table translated, and may be absent.
            if (auto* intl = static_cast<const retro_core_options_intl*>(data))
                captureDefinitions(intl->us);
            return true;
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
            captureV2(static_cast<const retro_core_options_v2*>(data));
            return true;
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:
            if (auto* intl = static_cast<const retro_core_options_v2_intl*>(data))
                captureV2(intl->us);
            return true;

        case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
            // Version 2, not 0. A core told version 0 falls back to the
            // original API, where the default is "whichever value is listed
            // first" — a convention we would be inferring. At version 2 the
            // core states its default outright, which is a fact rather than an
            // inference, and every core that only speaks the old API still
            // works because it calls SET_VARIABLES regardless.
            *static_cast<unsigned*>(data) = 2;
            return true;

        default:
            return std::nullopt;
    }
}

}  // namespace corestate

using namespace corestate;

void Core::setOptionOverrides(const std::map<std::string, std::string>& overrides) {
    gOverrides.clear();
    for (const auto& [k, v] : overrides) gOverrides[k] = v;
    for (const auto& [k, v] : gCommandLine) {
        std::fprintf(stderr, "[core] option from the command line: %s = %s\n", k.c_str(),
                     v.c_str());
        gOverrides[k] = v;
    }
    // Re-apply to anything already declared, so setting these after a load is
    // not silently a no-op. Before load() is still the correct time to call it,
    // because a core may read its options inside retro_init.
    for (Option& o : gDeclared) {
        auto it = gOverrides.find(o.key);
        o.overridden = (it != gOverrides.end());
        o.chosen = o.overridden ? it->second : o.defaultValue;
    }
}

void Core::setOptionLive(const std::string& key, const std::string& value) {
    gOverrides[key] = value;
    auto it = gByKey.find(key);
    if (it == gByKey.end()) return;
    Option& o = gDeclared[it->second];
    o.overridden = true;
    o.chosen = value;
    gOptionsUpdated = true;
    std::fprintf(stderr, "[options] %s = %s, now\n", key.c_str(), value.c_str());
}

std::vector<Core::OptionReport> Core::options() const {
    std::vector<OptionReport> out;
    out.reserve(gDeclared.size());
    for (const Option& o : gDeclared)
        out.push_back({o.key, o.desc, o.values, o.defaultValue, o.chosen,
                       o.overridden, o.asked});
    return out;
}

std::vector<std::string> Core::undeclaredOptionAsks() const {
    return gUndeclaredAsks;
}

void Core::setAnswerOptions(bool on) { gAnswerOptions = on; }

void Core::setCommandLineOption(const std::string& key, const std::string& value) {
    gCommandLine[key] = value;
}

}  // namespace cab
