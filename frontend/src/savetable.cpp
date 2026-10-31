// The save table (savetable.h): what each core writes and where, the tags a
// save and a state travel under on RomM, the directory saves, and the
// firmware a core can find. Moved out of catalog.cpp in the restructure
// (#398).
//
// TWO FACTS HERE ARE HALF OF A PAIR, AND CHANGE WITH THE OTHER HALF IN
// coreoptions.cpp (optionOverrides):
// saveFiles' one fixed 3DO save path (opera/shared/nvram.0.srm) needs
// opera_nvram_storage=shared, and the name firmwareAliases stages the 3DO's
// firmware under (panafz10.bin) is the one opera_bios names. The PS2 card's
// name, `<stem>.ps2`, pairs with core_ps2.cpp's startPs2Game instead.

#include "savetable.h"

#include <cctype>
#include <cstring>

#include "catalog_internal.h"
#include "wii.h"

namespace catalog {

const char* emulatorTag(const char* core) {
    if (!core) return nullptr;
    // Cabinet's own strings, from RommApp/RommApp/Native/NativeCore.swift.
    // Every core that has states appears here; the last three by decision,
    // whether or not the build matches (below).
    struct { const char* core; const char* tag; } kTags[] = {
        // Pinned at d9d6cd06, no build arguments on any platform, and the
        // save-state portability result was proved with this core.
        {"gambatte", "gambatte-native"},
        // Pinned at a7985a9c. CabinetOS builds HAVE_CDROM=0, which is what
        // Cabinet's Apple builds got by default, and the object-file
        // comparison showed the flag changes nothing under core/.
        {"genesis_plus_gx", "gpgx-native"},
        // Pinned at ba61a4fd. CabinetOS builds DYNAREC=lightrec where Cabinet
        // ran the interpreter on iOS/tvOS and ari64 on the Mac, and sharing
        // the tag across that difference is what Cabinet ALREADY did between
        // its own two. Justified rather than assumed: the state format
        // tolerates either backend, and the dynarec section is block addresses
        // rather than machine state. docs/PROJECT.md, open question 13.
        {"pcsx_rearmed", "pcsx-rearmed-native"},
        // Pinned at 66b5d263. CabinetOS builds JIT_ARCH=x64 where Cabinet ran
        // the interpreter on iOS/tvOS and aarch64 on the Mac, and this one was
        // MEASURED, not argued: both builds run Tetris DS to an identical video
        // and audio digest over 600 frames, write states of identical size, and
        // every cross-load lands on the same digest as the matching control.
        // Upstream intends it — the only JIT-conditional lines in any
        // DoSavestate are guarded `if (!file->Saving)`.
        {"melonds", "melonds-native"},
        // Pinned at 733c711a, and built with use_sh2drc=0, which is exactly
        // what Cabinet's Apple builds got. Same commit, same argument, so the
        // tag needs no argument at all. The states were checked anyway and came
        // out byte-identical to the recompiler build's.
        {"picodrive", "picodrive-native"},
        // Pinned at e31759b2, no CPU backend anywhere in this core, and the
        // feature set was read off Cabinet's own shipping archives with `nm -u`
        // rather than guessed: zlib and nothing else.
        //
        // One caveat, and it is Cabinet's rather than ours: libmgba_mac.a
        // reports its revision as `e31759b24-dirty`, so Cabinet's Mac build
        // carried a modification no script applies: the same class of problem
        // as Flycast's unscripted edits. The iOS and tvOS archives were clean at
        // this commit, and tvOS was the platform states travelled to and from most.
        {"mgba", "mgba-native"},
        // Pinned at c989c255, and the only core in the set where there is no
        // configuration difference left to justify. The commit is identical on
        // every platform Cabinet shipped it to (the manifest's own
        // diverges_across_platforms is false), both of the patches Cabinet's
        // builder applied travel and are asserted, the two CMake levers that
        // the unix build would otherwise decide differently are matched
        // (USING_GLES2, MOBILE_DEVICE). The CPU engine is an OPTION in this
        // core rather than a build flag, and differs on purpose since #147:
        // the recompiler here, Cabinet's "IR JIT" on Apple (optionOverrides).
        // The state is the emulated PSP, not the engine that ran it.
        //
        // What is not proved, stated because it is true of the other five
        // here as well: no state written by this build has been loaded by
        // Cabinet's. The cross-platform load was proved once, on gambatte, and
        // every tag since rests on configuration parity rather than on its own
        // experiment.
        {"ppsspp", "ppsspp-native"},

        // EIGHT MORE LIBRETRO CORES, 2026-09-23. Their states stayed on the
        // console only because nobody had run the check above, and the check
        // took a minute: every one is pinned at Cabinet's own commit, and all
        // but mupen64plus are built with exactly Cabinet's arguments (none at
        // all, or vecx's HAS_GPU=0 on both sides). The ninth of that group,
        // beetle_saturn, is below with the ones that stay local. MMagTech, rightly:
        // states belong on every libretro core, and open question 25 already
        // said so. Their saves ride the same tags, through saveTag.
        {"fceumm", "fceumm-native"},        // 236ccdfc
        {"snes9x", "snes9x-native"},        // 890b5d44
        {"beetle_pce_fast", "pcefast-native"},  // 2f623abd
        {"beetle_vb", "beetle-vb-native"},  // 83ed4260
        {"prosystem", "prosystem-native"},  // 8a880142
        {"stella2014", "stella2014-native"},  // 4a7da825
        {"vecx", "vecx-native"},            // 8f671cc9, HAS_GPU=0 both sides
        // N64, THE ONE WITH A DIFFERENCE, said plainly. Same commit, f275caf4,
        // on every platform Cabinet shipped. But Cabinet's build flags do not
        // link on Linux (see docs/PROJECT.md, *mupen64plus: it builds*), so
        // this build is -O3 -ffast-math with the unix branch's RSP/RDP
        // choices where Cabinet's was -Ofast with its own. A state holds the
        // machine's memory and registers, which neither changes; if a state
        // from here refused to load on Cabinet's Apple TV, this line was why.
        //
        // Its SAVE is safer than its state: mupen64plus packs EEPROM, four
        // controller paks, SRAM and flash into one 296960-byte struct at this
        // commit, and the seven N64 saves on the reference server each hold
        // data exactly where their game's chip is.
        {"mupen64plus", "mupen64plus-native"},

        // AND THREE OF THE FIVE THAT HAD SAVES ONLY, the same day, for the
        // same reason: same commit as Cabinet, no patches, no build arguments
        // on either side. Their saves travelled already through saveTag.
        {"opera", "opera-native"},            // a501a278
        {"fbneo_libretro", "fbneo-native"},   // 2444fbe3
        {"beetle_ngp", "ngp-native"},         // a50d5ac2, Cabinet's iOS commit

        // AND THE LAST THREE, BY DECISION, 2026-09-23. MMagTech: every core
        // that is supposed to have states uploads them under the tag Cabinet
        // recognised, "regardless of if it will actually load". A state that
        // does not load on the other device is refused there and costs
        // nothing else; a state that never leaves the console is lost with
        // it. So the tag says which emulator made it, and loading is the
        // other side's question.
        //
        // What is known about each, so a refusal is not a surprise:
        //   beetle_saturn  An Apple TV state (Daytona USA, 2026-08-07) was
        //                  REFUSED here. Cabinet's Apple TV Saturn build was not
        //                  at the commit its manifest pins, and was replaced
        //                  on 2026-08-20, after that state was made. Which of
        //                  the two refused it is not known.
        //   mame2003_plus  Pinned at Cabinet's Mac commit, 21256d24; its Apple
        //                  TV build was at 93159c0c. Never tested: there are no
        //                  MAME states on the reference server.
        //   flycast        Cabinet's build carried unscripted working-tree
        //                  edits, so there is no revision to match. Never
        //                  tested either.
        {"beetle_saturn", "saturn-native"},        // ed549bda
        {"mame2003_plus", "mame2003plus-native"},  // 21256d24
        {"flycast", "flycast-native"},             // a172e000

        // NINTENDO 3DS, IN AZAHAR, BY DECISION, 2026-10-20 (MMagTech: the 3DS
        // gets states like every other handheld). No Cabinet app plays 3DS,
        // so the tag is the emulator's own name, as the separate emulators'
        // save tags are; its saves keep the same tag through saveTag. A state
        // loads only in the Azahar commit that made it (its core/savestate.cpp
        // refuses any other revision), so an emulator update can strand older
        // 3DS states: accepted, docs/PROJECT.md question 50. A refused one is
        // the console's plain "Couldn't load that state". No rewind
        // (rewindAllowed).
        {"azahar", "azahar"},                      // 219d743a, 2126.2
        // MAME 0.289, libretro's own build (docs/decisions/
        // 2026-10-28-prebuilt-mame-core.md). No Cabinet app plays it, so the
        // tag is the emulator's name, as Azahar's is. A state loads only in
        // the MAME that made it; a pin move is taken to break them
        // (docs/decisions/2026-10-28-emulator-updates-and-saves.md).
        {"mame_libretro", "mame"},                 // 5f5a0774, 0.289
        //
        // PS2 and GameCube have no states at all (snapshotsAllowed), by open
        // question 25, so they need no tag here.
    };
    for (const auto& t : kTags)
        if (std::strcmp(t.core, core) == 0) return t.tag;
    return nullptr;
}

bool snapshotsAllowed(const char* core) {
    if (!core) return true;
    // True to the machines: memory-card consoles, and everything after them.
    // Not the 3DS, a handheld, by MMagTech's decision of 2026-10-20, though
    // its states may not survive an emulator update (emulatorTag). And every
    // emulator run as a program of its own, sm2-emu's Model 2 and
    // Supermodel's Model 3 among them, whose pause menu is Resume, Screenshot
    // and Exit to Home (MMagTech, 2026-10-29).
    static const char* const kNoSnapshots[] = {"pcsx2", "dolphin", "eden", "rpcs3", "xemu",
                                               "xenia", "cemu", "vita3k", "sm2emu",
                                               "supermodel"};
    for (const char* c : kNoSnapshots)
        if (std::strcmp(c, core) == 0) return false;
    return true;
}

bool rewindAllowed(const char* core) {
    if (!snapshotsAllowed(core)) return false;
    if (!core) return true;
    // NO REWIND WHERE A STATE TAKES LONGER THAN A FRAME: rewind takes one
    // every half second of play (pace.cpp), and each one holds the frame
    // thread. Azahar's takes 270 to 850 ms on the A9 (measured 2026-10-20,
    // docs/PROJECT.md question 50), which would play a 3DS game at 37% to 65%
    // of its speed; RetroArch's core info marks it "basic", with no rewind
    // either. A list, because asking a core its cost means making a state.
    static const char* const kNoRewind[] = {"azahar"};
    for (const char* c : kNoRewind)
        if (std::strcmp(c, core) == 0) return false;
    return true;
}

const char* saveTag(const char* core) {
    if (!core) return nullptr;
    // A state tag is always good enough for a save: it is the stricter test of
    // the two, and every core that passes it writes both.
    if (const char* t = emulatorTag(core)) return t;

    // SINCE 2026-09-23 EVERY LIBRETRO CORE WITH STATES HAS A STATE TAG, so
    // every one of them is answered by the line above, and this table is only
    // for the two cores with no states at all. Until that day it also held
    // Flycast, 3DO, FBNeo, MAME 2003-Plus, Beetle NGP and Beetle Saturn as
    // saves-only, when their states stayed local; their history is in git and
    // in docs/PROJECT.md, *The save audit*.
    //
    // Whatever the tag, a save's bytes are the emulated machine's own (a VMU,
    // a board's NVRAM, a cartridge's battery), which is why saves could travel
    // before states did.
    struct { const char* core; const char* tag; } kSaveTags[] = {
        // PlayStation 2's memory card and GameCube's, and these two tags were
        // READ OFF THE ROWS ALREADY ON THE SERVER rather than derived from the
        // pattern above — they are plainly `pcsx2` and `dolphin`, with no
        // `-native` suffix, because Cabinet for Mac tagged them that way and its
        // comment said why: "PCSX2 rather than Cabinet, because the format is
        // PCSX2's and another PCSX2 could read it."
        //
        // Matching them is the whole point. A card written here has to land in
        // the row Cabinet's Mac app wrote, and RomM matches a row by filename with
        // the tag NOT included — so the tag is what stops two emulators'
        // uploads being confused for one another, and the name is what decides
        // which row is overwritten.
        //
        // NOTE these are not state tags and must never become them. Neither
        // system has states on this console (open question 25), and a state
        // from either would not have loaded in the Mac's build anyway.
        {"pcsx2", "pcsx2"},
        {"dolphin", "dolphin"},
        // Switch, in Eden. No Cabinet app plays Switch, so there is no row to
        // match and the name is this console's to choose: the emulator's own,
        // as the two above are. The save is a zip of the game's save folder,
        // travelling the way PSP's does (open question 32, decision D).
        {"eden", "eden"},
        // PlayStation 3, in RPCS3, the same way and for the same reason: no
        // Cabinet app plays it. A zip of the game's save folders.
        {"rpcs3", "rpcs3"},
        // Original Xbox, in xemu: a zip of UDATA/ and TDATA/ off the drive's
        // E:, which drops onto any xemu drive (xboxhdd.h). No Cabinet app
        // plays Xbox either.
        {"xemu", "xemu"},
        // Xbox 360, in Xenia Edge: a zip of Xenia's own content folder, the
        // profile's folder and the game's saves under the one profile ID
        // (x360profile.h), which drops into any Xenia. No Cabinet app plays
        // Xbox 360 either.
        {"xenia", "xenia"},
        // Wii U, in Cemu: a zip of the person's `usr/save/00050000/` for the
        // game, `<title>/user/` and `<title>/meta/` in Cemu's own layout, which
        // drops into any Cemu's mlc01. No Cabinet app plays Wii U either.
        {"cemu", "cemu"},
        // PlayStation Vita, in Vita3K: a zip of the person's
        // `ux0/user/00/savedata/` for the game, one folder per save directory
        // the game names (usually its title ID), which drops into any
        // Vita3K's. A real Vita's saves are sealed to that Vita and do not
        // load. No Cabinet app plays the Vita either (docs/PROJECT.md, open
        // question 49).
        {"vita3k", "vita3k"},
        // Sega Model 2, in sm2-emu: a zip of the person's `nvram/` for the
        // game, the board's `<set>.nv` and `<set>.eeprom` as sm2-emu names
        // them, which drop into any sm2-emu's saves folder. No Cabinet app
        // plays Model 2 either (docs/decisions/2026-10-28-model-2-and-model-3.md).
        {"sm2emu", "sm2emu"},
        // Sega Model 3, in Supermodel: a zip of the person's `nvram/` for the
        // game, the board's `<set>.nv` as Supermodel names it, which drops
        // into any Supermodel's NVRAM folder. No Cabinet app plays Model 3.
        {"supermodel", "supermodel"},
        //
        // Nintendo 3DS has states, so its save tag comes from emulatorTag
        // above: a zip of the emulated SD card's `Nintendo 3DS` folder
        // (directorySaveRoot), in Azahar's own layout.
    };
    const std::string name = manifestName(core);
    for (const auto& t : kSaveTags)
        if (name == t.core) return t.tag;
    return nullptr;
}

const char* gameCubeRegion(const std::string& titleId) {
    const std::string code = wii::codeFromTitleId(titleId);
    if (code.size() != 4) return nullptr;
    switch (code[3]) {
        case 'E': case 'B': case 'N':
            return "USA";
        case 'J': case 'W': case 'K': case 'Q': case 'T':
            return "JAP";
        case 'D': case 'F': case 'H': case 'I': case 'L': case 'M': case 'P': case 'R':
        case 'S': case 'U': case 'V': case 'X': case 'Y': case 'Z':
            return "EUR";
    }
    return nullptr;
}

const char* directorySaveRoot(const char* core, const std::string& platformSlug,
                              const std::string& titleId) {
    if (!core) return nullptr;
    if (manifestName(core) == "ppsspp") return "PSP/SAVEDATA";
    if (manifestName(core) == "azahar") return "Azahar/sdmc/Nintendo 3DS";
    if (manifestName(core) == "dolphin" && platformSlug == "wii")
        return wii::isMiiChannel(titleId) ? "User/Wii" : "User/Wii/title";
    if (manifestName(core) == "dolphin" && platformSlug == "ngc") {
        const char* region = gameCubeRegion(titleId);
        if (!region) return nullptr;
        if (std::strcmp(region, "USA") == 0) return "User/GC/USA/Card A";
        if (std::strcmp(region, "JAP") == 0) return "User/GC/JAP/Card A";
        return "User/GC/EUR/Card A";
    }
    return nullptr;
}

bool inDirectorySave(const char* core, const std::string& platformSlug,
                     const std::string& titleId, const std::string& relPath) {
    // GameCube: the saves Dolphin keeps in the folder, and only those. A save
    // deleted in a game is renamed `.gci.deleted`, not removed
    // (GCMemcardDirectory), and must not travel as if it were still there.
    if (core && manifestName(core) == "dolphin" && platformSlug == "ngc") {
        const std::string ext = ".gci";
        return relPath.find('/') == std::string::npos && relPath.size() > ext.size() &&
               relPath.compare(relPath.size() - ext.size(), ext.size(), ext) == 0;
    }
    if (!core || manifestName(core) != "dolphin" || platformSlug != "wii") return true;
    // The Mii Channel's root is `User/Wii`: its Mii list, and under `title/`
    // the same rule as every other game.
    std::string rel = relPath;
    if (wii::isMiiChannel(titleId)) {
        if (rel == wii::kMiiDatabase) return true;
        if (rel.compare(0, 6, "title/") != 0) return false;
        rel.erase(0, 6);
    }
    // `<8 hex>/<8 hex>/data/...`, and not the system's own titles.
    auto hex8 = [&](size_t at) {
        if (rel.size() < at + 9 || rel[at + 8] != '/') return false;
        for (size_t i = at; i < at + 8; ++i)
            if (!std::isxdigit(static_cast<unsigned char>(rel[i]))) return false;
        return true;
    };
    return hex8(0) && hex8(9) && rel.compare(0, 9, "00000001/") != 0 &&
           rel.compare(18, 5, "data/") == 0;
}

bool needsBios(const std::string& slug) {
    return slug == "3do" || slug == "saturn" || slug == "segacd" || slug == "ps2" ||
           slug == "turbografx-cd";
}

FirmwareAliases firmwareAliases(const std::string& slug, const std::string& fsSlug) {
    // Sizes and names taken from the reference implementation's own table,
    // which was built against real hardware, and checked against what the
    // reference server actually serves.

    // Saturn: Japan and NA/EU, 512 KB. THE ONE THAT WAS BROKEN — the server
    // calls it `saturn_bios.bin` and the core opens `sega_101.bin`. Both names
    // go down because Beetle Saturn chooses between them from the disc's own
    // region code at load time.
    if (slug == "saturn")
        return {524288, {"sega_101.bin", "mpr-17933.bin"}, nullptr};

    // Sega CD: NTSC-U, PAL and NTSC-J, a fixed 128 KB boot ROM. The reference
    // server already uses these names, so this row changes nothing there and
    // exists for the server that does not.
    if (slug == "segacd")
        return {131072, {"bios_CD_U.bin", "bios_CD_E.bin", "bios_CD_J.bin"}, nullptr};

    // TurboGrafx-CD: Beetle PCE Fast defaults to System Card 3, 256 KB.
    if (slug == "turbografx-cd")
        return {262144, {"syscard3.pce"}, nullptr};

    // 3DO: every retail BIOS Opera knows is exactly 1 MB, and one name is
    // enough because Opera does not scan by region — it opens exactly the file
    // `opera_bios` names, and optionOverrides always answers `panafz10.bin`.
    // So whatever 1 MB firmware the platform carries is staged under the one
    // name the core will be told to load, and the weak joint that comment
    // warns about is closed.
    if (slug == "3do")
        return {1048576, {"panafz10.bin"}, nullptr};

    // Dreamcast: the name already matches, and the DIRECTORY does not.
    // Flycast reads the boot ROM from `dc/` inside the system directory, and
    // when it is not there it falls back to its own HLE BIOS silently — so a
    // console can look entirely healthy while running an approximation of the
    // machine. Copied rather than moved: a flat `bios/dc_boot.bin` is what
    // RomM sent and what anyone looking would expect to find.
    if (slug == "dc")
        return {2097152, {"dc_boot.bin"}, "dc"};

    return {};
}

std::vector<SaveFile> saveFiles(const std::string& slug, const std::string& core,
                                const std::string& fileName, bool arcade) {
    // Straight out of the audit's per-platform table, and each row was read
    // off the core that writes it rather than guessed. docs/PROJECT.md, *The
    // save audit*, has the evidence for every line.
    //
    // Nothing here is keyed on the core, because a core is not a platform:
    // genesis_plus_gx appears once, for Sega CD, and the same core running a
    // Master System cartridge has a real battery this console already syncs.
    // Arcade is the exception, below.
    const size_t dot = fileName.find_last_of('.');
    const std::string stem = dot == std::string::npos ? fileName : fileName.substr(0, dot);

    // Arcade. The emulators disagree about both the folder and the
    // extension and their contents are not interchangeable, which is exactly
    // why this keeps them apart instead of treating "the arcade save" as one
    // thing. The stem is the set name — `lethalen`, `smashtv` — because that
    // is what the core was handed; see the note in launch_routes.cpp about why an
    // arcade entry keeps the server's own file name.
    //
    // BY THE EMULATOR THE GAME'S ZIP PICKED (arcadematch.h), not by its
    // folder, which says nothing about which file a game writes. First, so a
    // Naomi zip in Dreamcast's folder is a board's save, not a VMU.
    if (arcade) {
        SaveFile f;
        f.untouched = Untouched::Uniform;
        if (core == "mame2003_plus") {
            f.coreRowName = "mame2003Plus";
            // ONE DIRECTORY DEEPER THAN THE REFERENCE IMPLEMENTATION SAYS, and
            // this was measured rather than carried across. Cabinet wrote
            // `nvram/<stem>.nv` straight under the save directory; MAME
            // 2003-Plus as this console builds and drives it puts its whole
            // working tree under a `mame2003-plus/` folder first, so the file
            // is `mame2003-plus/nvram/lethalen.nv`. Confirmed twice on the
            // test VM, 2026-09-19: by running Lethal Enforcers and reading the
            // directory afterwards, and by the orphaned NVRAM the old flat
            // save pile left behind, which sits at exactly that path.
            //
            // Placing it at the reference's path is not a harmless miss. The
            // core does not find it, bootstraps a fresh image instead, and
            // writes that — so the restore silently does nothing and a
            // capture aimed at the same wrong path finds nothing either. That
            // is what the first run of this did.
            f.path = "mame2003-plus/nvram/" + stem + ".nv";
            return {f};
        }
        if (core == "fbneo_libretro") {
            f.coreRowName = "fbneo";
            f.path = "fbneo/" + stem + ".fs";
            return {f};
        }
        // MAME 0.289 WRITES SEVERAL FILES A GAME, all in this person's save
        // folder for it: `mame/nvram/<set>/`, `mame/cfg/<set>.cfg`, a hard
        // disk game's `mame/diff/<set>.dif`, and the high scores
        // (mamehome.h). None reaches RomM: a game's one save on RomM is one
        // file, and these wait for RomM's save channels with the other arcade
        // extras (#353). They stay on the console, never in cache/.
        if (core == "mame_libretro") return {};
        // FLYCAST'S BOARD MEMORY, `reicast/<the zip's own name>.nvmem`, named
        // after the whole file it was handed (hostfs::getArcadeFlashPath at
        // our pin; cores/build-core.sh stops when it moves). The one file of
        // a game's that reaches RomM, as FinalBurn Neo and 2003-Plus send one:
        // a board's `.eeprom` and Atomiswave's `.nvmem2` stay on the console
        // with the other arcade extras until RomM's save channels (#353).
        if (core == "flycast") {
            f.coreRowName = "flycast";
            f.path = "reicast/" + fileName + ".nvmem";
            return {f};
        }
        return {};
    }

    // Dreamcast. The one row that is not under the save directory: Flycast
    // never answers RETRO_MEMORY_SAVE_RAM — confirmed against its own
    // retro_get_memory_data, which only ever answers RETRO_MEMORY_SYSTEM_RAM —
    // and writes the card into the system directory instead, beside the BIOS.
    //
    // `per_content_vmus` defaults to 0, so the file is the bare
    // `vmu_save_A1.bin`; the capture scans for the suffix anyway, because with
    // that option on the core prefixes the disc's own game id and a capture
    // that insisted on the exact name would silently find nothing.
    if (slug == "dc") {
        SaveFile vmu;
        vmu.path = "dc/vmu_save_A1.bin";
        vmu.captureSuffix = "vmu_save_A1.bin";
        vmu.untouched = Untouched::VmuDirectory;
        vmu.inSystemDir = true;
        return {vmu};
    }

    // Sega CD, and it is TWO regions rather than one. Games prefer the
    // external RAM cartridge when it is present, so a console that treated
    // "the .brm" as one thing would have one of them overwrite the other —
    // which is why the scan excludes `cart.brm` and the cartridge gets its own
    // row. `4Mbit_cart.brm` is fixed by the core's own defaults for cart_bram
    // and cart_size; per-game separation already comes from the save directory.
    if (slug == "segacd") {
        SaveFile internal;
        internal.path = stem + ".brm";
        internal.captureSuffix = ".brm";
        internal.captureExclude = "cart.brm";
        internal.untouched = Untouched::SegaCDBackup;
        SaveFile cart;
        cart.path = "4Mbit_cart.brm";
        cart.captureSuffix = "cart.brm";
        cart.region = "cart";
        cart.untouched = Untouched::SegaCDBackup;
        return {internal, cart};
    }

    if (slug == "neo-geo-pocket-color") {
        SaveFile f;
        f.path = stem + ".flash";
        f.captureSuffix = ".flash";
        return {f};
    }

    // melonDS writes `<content basename>.sav` through its own SRAM manager,
    // debounce-flushed during play and flushed again at unload.
    if (slug == "nds") {
        SaveFile f;
        f.path = stem + ".sav";
        f.captureSuffix = ".sav";
        return {f};
    }

    // Opera writes to a fixed nested path rather than a flat suffix-named
    // file, so this one is exact in both directions.
    if (slug == "3do") {
        SaveFile f;
        f.path = "opera/shared/nvram.0.srm";
        f.untouched = Untouched::ThreeDONvram;
        return {f};
    }

    // PlayStation 2. The console's PCSX2 host is handed one card, `<stem>.ps2`
    // in the save directory (core_ps2.cpp, startPs2Game), which is already
    // per game. Never a shared `Mcd001.ps2`, the arrangement Cabinet for Mac
    // rejected in as many words ("a shared card belongs to no rom in
    // particular").
    //
    // NAMED THE MAC'S WAY on the server, because the whole point is that the
    // card travels — see SaveFile::macRowName.
    if (slug == "ps2") {
        SaveFile f;
        f.path = stem + ".ps2";
        f.captureSuffix = ".ps2";
        f.region = "ps2";
        f.untouched = Untouched::Ps2Format;
        f.macRowName = true;
        return {f};
    }

    // GameCube is not in this class any more (#344): Dolphin keeps one `.gci`
    // file per save in its own folder, and they travel as a directory save.
    // See directorySaveRoot.
    //
    // PSP is in this class too and is already built, as a tree rather than a
    // file. directorySaveRoot above is its entry.
    return {};
}

}  // namespace catalog
