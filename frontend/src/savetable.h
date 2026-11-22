// The save table: what each core saves and where (saveFiles,
// directorySaveRoot), the tags a save and a state travel under on RomM
// (emulatorTag, saveTag, snapshotsAllowed), and the firmware a core can find
// (needsBios, firmwareAliases). catalog.h includes this; filesave.h includes
// only this. Moved out of catalog.h in the restructure (#398).

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace catalog {

// For the one platform whose save is a DIRECTORY rather than a file: where that
// directory sits under the save directory, or nullptr for everything else.
//
// PSP saves into a memory stick — `PSP/SAVEDATA/<GAMEID><TITLE>/` holding
// PARAM.SFO, DATA.BIN and icons — because that is what PPSSPP reads and writes
// on every platform, and there is no single-file PSP save anywhere. It travels
// to RomM as a zip; see dirsave.h for why zip and where the archive is rooted.
//
// Deliberately NOT the whole `PSP/` tree: NAND, PPSSPP_STATE and SYSTEM/CACHE
// sit beside SAVEDATA and are this machine's own state, save states and
// compiled shaders. Uploading them would put tens of megabytes of nothing on
// the server and mean nothing on the other end — the reference implementation
// says exactly that and it is right.
//
// NINTENDO 3DS THE SAME WAY (docs/PROJECT.md question 50): Azahar keeps a
// whole 3DS under `Azahar/` in the save directory, and every game's save and
// extra data are under its SD card's `Nintendo 3DS/`, rooted there. The save
// directory holds one game, so the whole folder is that game's. Not the rest
// of `Azahar/`: its NAND, system data and shader cache stay here.
//
// WII TOO, from 2026-09-30, rooted at `User/Wii/title` in Dolphin's user
// directory, which is under this person's save directory for this game, so
// it is a NAND of its own. A game saves to `<kind>/<code in hex>/data/`
// (`00010000/53554b45/data/` for Kirby's disc, `00010001/...` for WiiWare).
// The kind is not known from here, so the zip keeps it: entries are relative
// to `title/`, and only `data/` folders go, never `content/` (a WiiWare
// game's installed program, tens of megabytes) and never `00000001/` (the
// emulated system's own settings, rewritten at every boot). See
// inDirectorySave.
//
// THE MII CHANNEL ONE LEVEL UP, at `User/Wii` (#275): its save is also the
// Wii's Mii list, `shared2/menu/FaceLib/RFL_DB.dat`, which is outside
// `title/`. Its zip holds `title/<kind>/<code>/data/...` and that one file.
// No other game's zip changes. `titleId` is RomM's (wii::isMiiChannel).
//
// GAMECUBE TOO, from 2026-10-11 (#344): Dolphin's own default, one `.gci`
// file per save in `User/GC/<region>/Card A/`, where every other Dolphin
// keeps them and what RomM's coming save channels carry (#346). The zip is
// those files, flat. Until then the console kept one whole 16 MB card at a
// path of its own, because Cabinet for Mac did; Dolphin reads that card into
// the folder by itself the first time the folder is missing (saves.cpp,
// restoreDirSave). The region comes from the game's code, so a game whose
// code RomM does not know has no root and its saves stay on this console.
const char* directorySaveRoot(const char* core, const std::string& platformSlug,
                              const std::string& titleId);

// The folder name Dolphin gives a GameCube game's region, `USA`, `JAP` or
// `EUR`, from the last of the four letters in RomM's `title_id`, with
// Dolphin's own table (DiscIO/Enums.cpp, CountryCodeToRegion). Dolphin reads
// the disc's region field rather than the letter; the two disagree only on a
// few Korean discs labelled `E`. Nullptr when there is no code.
const char* gameCubeRegion(const std::string& titleId);

// Whether a file under that root is part of the save, by its path relative
// to the root. Everything is, for PSP. For GameCube, the `.gci` files only.
bool inDirectorySave(const char* core, const std::string& platformSlug,
                     const std::string& titleId, const std::string& relPath);

// --- Saves the CORE writes as a file ---------------------------------------
//
// THE MAJORITY OF THE SAVES ON A REAL SERVER, which is the thing that made
// this worth building. The audit of 2026-09-17 measured 47 of the 81 saves on
// the reference server — 58% — as belonging to platforms whose core never
// exposes RETRO_MEMORY_SAVE_RAM at all. It reads like an edge case and it is
// not one.
//
// A core in this class keeps the game's progress in a FILE it opens itself: a
// Dreamcast VMU image, a Sega CD backup-RAM dump, an arcade board's NVRAM, a
// Neo Geo Pocket flash, a DS cartridge SRAM, a 3DO NVRAM. `[save] battery is 0
// bytes` at launch is this console correctly reporting that there is no
// battery to read — the save is on the disk, not in the core's memory.
//
// So the mechanism is the one PSP already uses and for the same reasons: put
// the file there BEFORE the core loads the game, because these cores read it
// once while the machine is being built and never look again; read it back
// AFTER `retro_unload_game`, because that is where a core flushes what it has
// been buffering. See filesave.h for the halves and docs/PROJECT.md, *The save
// audit*, for the per-platform evidence.
//
// This is the table. filesave.{h,cpp} is the mechanism.

// How to tell a save somebody made from the empty thing a core writes just by
// being switched on.
//
// WITHOUT THIS EVERY LAUNCH UPLOADS. A core formats its own storage the first
// time it runs, the result is identical for everyone who plays that game, and
// it would then be pulled back down onto somebody's other device as if it
// meant something. The reference implementation's `isUntouchedNVRAM` exists
// for exactly this and the rules below are its per-platform refinements.
enum class Untouched {
    // Nothing but 0x00 and 0xFF: no core wrote anything worth keeping.
    Blank,
    // Every byte identical, whatever the byte is. MAME's fresh NVRAM is all
    // 0x01 for the capbowl family and all 0x00 elsewhere, and a seeded
    // bootstrap image is the same for everyone who plays that board.
    Uniform,
    // Genesis Plus GX puts its 64-byte format block at the END of a .brm, and
    // freshly formatted backup RAM carries allocation marks in the first 16
    // bytes — a repeating `ff fa 00 02`. Counting either as data uploads
    // formatted-empty images. Same layout at a bigger size for the cartridge.
    SegaCDBackup,
    // Opera formats a fresh NVRAM with a filesystem header — the volume block
    // and root directory, all inside the first 176 bytes. Data past that is
    // what distinguishes a real save.
    ThreeDONvram,
    // A PlayStation 2 card that has never been written to. MEASURED against
    // the reference server 2026-09-20 and it is not a subtle test: PCSX2
    // creates the file as 8,650,752 bytes of 0xFF and only the console's own
    // BIOS writes the format header, so an untouched card carries no magic at
    // all. Three of the four PS2 cards on that server were exactly this.
    //
    // THE RULE IS THE MAGIC AND NOTHING MORE, ON PURPOSE. The one real card
    // available to measure — Burnout 3 — has its save data out at 8,384 KB
    // while its FAT and directory sit in the first 128 KB, and a threshold
    // invented from ONE sample is how a rule silently refuses somebody's save.
    // A false negative loses a save; a false positive uploads a formatted
    // empty card, which is what the old behaviour did anyway. The weaker rule
    // is the safe direction until there is a second card to measure.
    Ps2Format,
    // A GameCube card with nothing in its directory. Block 0 is the header,
    // blocks 1 and 2 are the directory and its backup: 127 entries of 64
    // bytes, and an entry's first four bytes are the game code, 0xFF when the
    // slot is free. Measured both ways on 2026-09-20 — the three cards on the
    // reference server had zero entries in use, and a card Ikaruga had saved
    // to carried `GIKE`/`70`/`ikaruga_save_data`.
    //
    // No SaveFile uses it since GameCube moved to a folder of `.gci` files
    // (#344). It still judges a whole card that arrives from RomM, made
    // before that, so an empty one is never handed to Dolphin to read in.
    GameCubeDirectory,
    // A VMU's own directory, and the one guard this project did not inherit.
    // MEASURED, 2026-09-19: three of the thirteen Dreamcast cards on the
    // reference server — Cannon Spike, Re-Volt, San Francisco Rush 2049 — hold
    // no file at all. They are formatted cards the reference implementation
    // uploaded because its Dreamcast path applies no freshness rule. A quarter
    // of the rows for the platform with the most of them carry nothing.
    //
    // A VMU is 128 KB of 512-byte blocks. Block 255 is the root and starts
    // with sixteen 0x55 bytes; blocks 253 down to 241 are the directory,
    // sixteen 32-byte entries each, and an entry's first byte is 0x33 for a
    // data file, 0xCC for a game and 0x00 for a free slot. So "this card holds
    // a save" is a fact the bytes state outright.
    VmuDirectory,
    // A Saturn backup cartridge with no save on it (#346, step 11). Beetle
    // Saturn puts one in almost every game (`beetle_saturn_cart` = Auto
    // Detect, libretro.c at our pin) and formats it as the Saturn does: the
    // format's name, `BackUpRam Format`, over the first 512-byte block, and
    // nothing after (CART_Backup_Init, mednafen/ss/cart/backup.c). A save
    // begins at a block from the third on whose first four bytes are 0x80 00
    // 00 00, the mark Sigil lists a volume's saves by (src/card_saturn.c at
    // RomM's pin); a save deleted on the Saturn loses that mark. So "holds a
    // save" is one such block.
    SaturnCartridge,
};

// WHAT ANOTHER EMULATOR'S SAVE MUST BE BEFORE THIS CONSOLE USES IT (#346, step
// 8; decided 2026-10-12). A save a CabinetOS console made is used as RomM
// stores it. Any other is asked of RomM in this console's own form (saveForm
// below) and then checked: the size this core's file has, the marks its
// format carries, and that it holds a save (holdsASave). One that fails is
// not used, and the game starts from this console's own save with "Couldn't
// use the newest save".
struct SaveCheck {
    // The sizes the file may have, in bytes; empty for any. A battery may
    // be no longer than the core's, checked once the core is loaded
    // (saves.h, coreReadsBattery).
    std::vector<size_t> sizes;
    // Bytes the format always carries, at `markAt` from the start, or from
    // the end when `markFromEnd`; empty for a format that carries none.
    std::string mark;
    size_t markAt = 0;
    bool markFromEnd = false;
};

// One file a core writes for itself.
struct SaveFile {
    // Relative to the core's SAVE directory, which is
    // `users/<id> - <name>/saves/<platform>/<romId>/<core>/` and holds this one
    // game's files. Already has the ROM's stem substituted where the core names
    // the file after what it loaded.
    std::string path;
    // A suffix to scan the directory for after the core has shut down, when
    // the name the core chose cannot be predicted from here. Empty means
    // `path` is exact in both directions.
    std::string captureSuffix;
    // A name that ends in `captureSuffix` and belongs to a DIFFERENT region.
    // Sega CD is the case: `4Mbit_cart.brm` also ends in `.brm`, and without
    // this the cartridge and the internal RAM overwrite each other.
    std::string captureExclude;
    // Which of a game's saves this file is: "srm" for the game's own save,
    // "cart" for a cartridge beside the machine's own memory (Sega CD's
    // external RAM cartridge, Saturn's backup cartridge), "ps2" for a
    // PlayStation 2 card. It is the extension of this console's own copy
    // (saveRowName) and of the row a save from before save channels has on
    // RomM, one row a region, which is how a start tells them apart
    // (savepick.h, regionOfRow). A CARTRIDGE IS NEVER A SAVE'S ONE FILE: a
    // snapshot's save that is one file is the machine's own memory, and a
    // cartridge travels only in a zip beside it.
    std::string region = "srm";
    Untouched untouched = Untouched::Blank;
    // What another emulator's save of this file must be (SaveCheck).
    SaveCheck check;
    // THE CORE READS `path` ONLY WHEN NO FILE OF ITS OWN NAMING IS THERE, and
    // always writes under that name. Dreamcast: with one card per game
    // (MMagTech, 2026-11-17; `reicast_per_content_vmus` = `VMU A1`), Flycast
    // reads `<disc's product number>.A1.bin`, else `<content name>.A1.bin`,
    // and writes the first (getVmuPath, shell/libretro/oslib.cpp at our pin).
    // So a start puts the person's card at `path` and takes away any other
    // file ending in `captureSuffix`, which would be read first; a game on
    // several discs, whose discs may have different product numbers, then
    // finds the card from any disc. After the game the file the core wrote is
    // read, moved to `path`, and is what travels.
    bool coreNamesItself = false;
    // THIS PERSON'S CARD FROM BEFORE, copied once to `path` when a start
    // finds none there (MMagTech, 2026-11-17): Dreamcast's card lived in the
    // system directory until #346, and this console kept each person's copy
    // at saveRowName(stem, region) in the save folder (saves.cpp). The old
    // file is left as it is.
    bool fromFormerCopy = false;
    // THE CORE WRITES IT ONLY AS THE GAME CLOSES (read off each core at our
    // pin, 2026-11-19, #346): the save lives inside the core until then, so
    // no moment of saving can be seen while it runs, and the save's picture
    // does not look at it (savepicture.h). Flycast also writes a board's
    // memory whenever a state is made, which is not the game saving.
    bool writtenAtClose = false;
    // ITS NAME IN A ZIP BESIDE THE BATTERY (SaveForm::batteryFirst), Sigil's
    // for the device it is: `cart.ram` for Saturn's cartridge. Empty: the
    // file travels under `path`.
    std::string unitName;
};

// --- Firmware a core can actually find --------------------------------------
//
// THE PROBLEM, PLAINLY: a core looks its BIOS up by a FIXED filename, and RomM
// serves firmware under whatever name the person who uploaded it chose. When
// the two disagree the file is downloaded, sits in `bios/`, and the core says
// it cannot find a BIOS — which is exactly as unhelpful as it sounds.
//
// It is not hypothetical and it is not rare. On the reference server, Saturn's
// BIOS is `saturn_bios.bin` and Beetle Saturn opens `sega_101.bin`, so **no
// Saturn game could start at all** until 2026-09-19. 3DO is the same shape and
// only works because that server happens to use the one name `opera_bios` is
// answered with. Sega CD and TurboGrafx-CD happen to match. Four platforms,
// two of them broken by luck.
//
// THE ONLY SIGNAL AVAILABLE IS THE SIZE. RomM's firmware record carries a
// filename and a length and says nothing about region or purpose, so matching
// by size is not a shortcut — it is the whole of what there is to match on.
//
// AND THE COPY GOES UNDER EVERY NAME, not the one that looks right. Beetle
// Saturn and Genesis Plus GX both pick their CD BIOS from the DISC's region
// code at runtime, with no fallback if that one file is absent — so which name
// is needed is not knowable when the file is being placed. Putting the same
// bytes under both names lets whichever one the disc asks for resolve. That is
// the reference implementation's reasoning and it is right.
struct FirmwareAliases {
    // The exact byte count of the real file. A downloaded firmware file of any
    // other size is a different thing and is left alone.
    int64_t sizeBytes = 0;
    // Every name a core serving this platform might open, in no order.
    std::vector<const char*> names;
    // A subdirectory of `bios/` to place it in as well, or empty. Flycast is
    // the one that wants this: it looks for the Dreamcast boot ROM under
    // `dc/`, which libretro-super's own `flycast_libretro.info` documents and
    // which the reference implementation confirmed the hard way — a flat
    // placement produces a SILENT fallback to the core's built-in HLE BIOS
    // with no error at all, so the console appears to work while running
    // something other than the machine it says it is.
    const char* subDir = nullptr;
};

// THE SYSTEMS THAT CANNOT START WITHOUT THEIR OWN BIOS: 3DO, Saturn, Sega
// CD, PlayStation 2 and TurboGrafx-CD (#90). Every other system either needs
// none or its core carries a stand-in (PlayStation, Dreamcast, GBA, DS,
// GameCube). Arcade is not here on purpose: this console expects non-merged
// sets, which carry each board's BIOS inside the game's own zip (MMagTech,
// 2026-09-27; Metal Slug 2's zip holds the whole Neo Geo BIOS). Used only
// to word a failure, never to refuse a launch.
bool needsBios(const std::string& slug);

// What to copy where, for this platform, or nothing for the platforms whose
// cores need no firmware or already agree with RomM about the name.
FirmwareAliases firmwareAliases(const std::string& slug, const std::string& fsSlug);

// What this platform's core writes, or empty for the cores that expose a
// battery and need none of this. Saturn has both: its battery, and its
// backup cartridge beside it (SaveForm::batteryFirst).
//
// Keyed on the platform rather than the core, because one core serves several
// systems and only some of them are in this class: Genesis Plus GX writes a
// `.brm` for Sega CD and nothing at all for Genesis, Game Gear or Master
// System, whose cartridges have real batteries the core exposes. The audit's
// own table is the source; `fileName` is the basename of the file the core
// was handed, and without its extension it is what these cores name the save
// after.
//
// EXCEPT AN ARCADE GAME (`arcade`, catalog::Coverage::arcade), which is keyed
// on `core`, the manifest name of the emulator its zip picked
// (arcadematch.h): a folder no longer says which file is written.
std::vector<SaveFile> saveFiles(const std::string& slug, const std::string& core,
                                const std::string& fileName, bool arcade);

// The tag a save or state is filed under on RomM, for a given manifest core
// name. This is the compatibility marker: Cabinet greyed out a state whose tag
// does not match the core about to run, which is what stops someone being
// offered a save that cannot load.
//
// **CabinetOS shares Cabinet's tags, and only because of work already done.**
// A Gambatte state is bit-identical between Cabinet's macOS arm64 build and a
// Linux x86-64 build AT THE SAME COMMIT; core-manifest.json pins that commit,
// build-core.sh asserts it, and CI proved the artifact reproducible across two
// machines. Those three together make a shared tag a fact rather than a hope,
// and they are why a state from Cabinet's Apple TV app loaded on this console.
//
// THE RULE, AS DECIDED 2026-09-23: every core that has states uses Cabinet's
// tag for that emulator, so its states upload and Cabinet recognised them (as
// Cabinet Go will), whether or not the other device can load one. MMagTech:
// "regardless of if it will actually load". A state that will not load is
// refused on the other side and costs nothing else; one that never leaves the
// console is lost with it. Where a build is known to differ from Cabinet's,
// the entry in savetable.cpp says how, so a refusal is not a mystery. This
// REPLACES the earlier rule, which shared a tag only where the build was
// proved identical and left most cores' states on the console.
//
// Returns nullptr for a core with no states (PS2, GameCube) or one not yet in
// the table, and a state from those stays on the console.
const char* emulatorTag(const char* manifestCoreName);

// The tag a SAVE travels under, which is not always the tag a STATE travels
// under — and five platforms' worth of saves depend on the difference.
//
// THE PLAIN VERSION: a save state is a photograph of the emulator's own
// insides, so two builds of a core can disagree about it and the tag is what
// stops somebody being handed one that will not load. A file save is not that.
// A Dreamcast VMU image is 128 KB in the VMU's own format, an arcade NVRAM is
// the board's own chip, a Sega CD `.brm` is the machine's backup RAM. Those
// formats are defined by the hardware being emulated and no build flag moves
// them — which the audit proved by reading the bytes: "every other core
// uploads the emulator's own bytes, so anything that can read a save for those
// platforms can read what is on this server."
//
// WHY IT MATTERED. `emulatorTag` used to be silent for five of the cores in
// the file-writing class (Flycast, Opera, FBNeo, MAME 2003-Plus, Beetle NGP),
// and those five held 35 of the 47 file saves on the reference server,
// including all thirteen Dreamcast cards. Saves travelled under this rule
// while their states stayed local.
//
// SINCE 2026-09-23 every libretro core with states has a state tag, by
// MMagTech's decision that states upload under Cabinet's tag whether or not
// the other device can load them (savetable.cpp, emulatorTag). So this answers
// from emulatorTag for all of them, and holds its own tags only for PS2 and
// GameCube, which have no states at all.
//
// Returns nullptr when even a save should stay local.
const char* saveTag(const char* manifestCoreName);

// HOW RomM HANDS THIS CORE A SAVE ANOTHER EMULATOR MADE, and the name the
// console gives the core on everything it sends (#346, step 8; the plan's
// "Checked against Sigil"). RomM converts a save through Sigil, the save
// library it builds in, for whichever emulator a request names, and answers
// with the files that emulator reads, by their paths in its save folder.
struct SaveForm {
    // Sigil's name for the core: its library name without `_libretro`, or
    // the emulator's own id, as Sigil's layout table spells it. Four of this
    // console's names differ: `beetle_*` is `mednafen_*`, `fbneo_libretro`
    // is `fbneo`, and `mupen64plus` is `mupen64plus_next` (our build is
    // libretro's mupen64plus-libretro-nx). Sent with every push as `core`,
    // beside the tag the save travels under, so the same core in another app
    // gets the same bytes (answer 7 of 2026-11-17), and named in every
    // request. Empty for a core whose saves stay on this console.
    std::string sigil;
    // The core options that decide where this core's save lives, as Sigil
    // reads them, sent with a request so RomM writes the file this console's
    // core reads: Dreamcast's card per game, Sega CD's backup memory per
    // game, PlayStation 2's card per game.
    std::vector<std::pair<std::string, std::string>> options;
    // The one profile this console's emulator keeps, for an emulator whose
    // saves Sigil files under a profile (Switch, Wii U, PS Vita, PS3).
    std::string profile;
    // A folder save: what comes before this console's own root (its
    // directorySaveRoot, or the emulator's saveRoot) in the paths of RomM's
    // answer, each `*` one folder of any name. Empty for a file.
    std::string answerRoot;
    // SIGIL NAMES A DIFFERENCE between emulators' files for this system that
    // RomM does not convert yet (MMagTech, 2026-11-17: DS, Game Boy Advance,
    // Mega Drive, Master System, Game Gear, 32X). Another emulator's save is
    // used only when RomM converted it or this core made it (RomM's own
    // `may_load` rule: the core first, else the emulator). When RomM converts
    // these systems, the same rule lets them through with no change here.
    bool sameCoreOnly = false;
    // THE CARTRIDGE'S CLOCK TRAVELS WITH THE SAVE (#346, step 11; the
    // decision of 2026-10-12): Gambatte's, for Game Boy and Game Boy Color,
    // its `RETRO_MEMORY_RTC`, 8 bytes, the time the game's clock counts
    // from. A clock game's save is then a zip of Gambatte's own two files,
    // `<game>.srm` and `<game>.rtc`, a shape Sigil reads as one from before
    // it (Sigil's sync.md, "Units from before sigil") and converts for other
    // emulators; the console writes no Sigil `clock.rtc` of its own. A
    // change to the clock alone is no new save: Sigil's identity covers the
    // RAM only. Snes9x's clock stays on the console (two games): Sigil does
    // not convert it (its snes.md, open items).
    bool clock = false;
    // THE BATTERY IS THE SAVE'S FIRST PART, WITH THE CORE'S FILES BESIDE IT
    // (#346, step 11): Saturn's own memory, which Beetle Saturn exposes, and
    // its backup cartridge, `<game>.bcr`, a file it writes (saveFiles). The
    // save is then a zip of the two under Sigil's names for them,
    // `backup.ram` (kInternalVolume) and `cart.ram` (SaveFile::unitName), as
    // Argosy sends them (MMagTech, 2026-11-20): Sigil reads those for every
    // Saturn emulator it knows, where it matches Beetle Saturn's own names
    // only for Beetle Saturn. The battery goes alone, as `<game>.srm`,
    // while the cartridge holds no save: a formatted, empty cartridge is
    // never sent.
    bool batteryFirst = false;
};

// Sigil's name for a machine's own memory in a zip of its save (its
// sync.md, "What syncs"): the battery's, beside a cartridge.
inline constexpr char kInternalVolume[] = "backup.ram";
SaveForm saveForm(const char* manifestCoreName, const std::string& platformSlug);

// What another emulator's battery save must be for this system (SaveCheck):
// its marks, and its size where every game's is the same. Any other battery
// may be no longer than the core's, checked once it is loaded (saves.h,
// coreReadsBattery).
SaveCheck batteryCheck(const std::string& platformSlug);

// WHETHER THIS SYSTEM OFFERS SAVE STATES AT ALL — docs/PROJECT.md, open
// question 25. False for PlayStation 2, GameCube and anything later, and the
// pause menu then has no Save state or Load latest state to press.
//
// DECIDED 2026-09-21 and made visible 2026-09-23, by MMagTech: *"even though it
// would anger most people we should follow the logic of no snapshots for these
// types of systems and keep them true to the consoles."* The reasons are in the
// question: a PCSX2 state breaks when the image updates the emulator, these
// consoles have memory cards every game uses, and their states open nowhere
// else. PRECISELY: no states where they cannot be trusted to load, because
// they could not travel to Cabinet (GameCube: a different Dolphin on the Mac) or
// break when the emulator updates (PS2: embedded PCSX2, not libretro). PSP and
// DS are later consoles and keep theirs, because theirs are proven portable. A
// new emulator is checked against that rule the day it lands. The 3DS is the
// exception, by MMagTech's decision of 2026-10-20: a handheld, it has states,
// though an Azahar update can strand them (savetable.cpp, emulatorTag).
bool snapshotsAllowed(const char* manifestCoreName);

// Whether a game on this core may rewind: never where snapshotsAllowed is
// false, nor where making a state takes longer than a frame, since rewind
// makes one every half second (Azahar, savetable.cpp). The pause shortcut then
// says "No rewind for this system" (play.cpp).
bool rewindAllowed(const char* manifestCoreName);

}  // namespace catalog
