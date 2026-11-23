#include "wiimii.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

#include "catalog.h"
#include "fileio.h"
#include "prefs.h"
#include "saves.h"
#include "storage.h"
#include "wii.h"

// THE MII CHANNEL ON THIS SERVER (#275): the Wii game whose title is the
// Mii Channel's, asked of RomM online and remembered for when it is away.
// Online and not there, it is forgotten: nobody's Miis go into games.
bool findMiiChannel(romm::Client& client, int platformId, int* romId, std::string* title) {
    if (!romm::serverAway() && client.haveToken() && platformId > 0) {
        std::vector<romm::Game> games;
        std::string err;
        if (client.fetchRoms("platform_ids=" + std::to_string(platformId), 1000, &games, &err)) {
            std::string id, name;
            for (const romm::Game& g : games)
                if (wii::isMiiChannel(g.titleId)) {
                    id = std::to_string(g.id);
                    name = g.name.empty() ? g.fsName : g.name;
                    break;
                }
            if (prefs::get("wii_mii_channel", "") != id) prefs::set("wii_mii_channel", id);
            if (prefs::get("wii_mii_channel_title", "") != name)
                prefs::set("wii_mii_channel_title", name);
        } else {
            std::fprintf(stderr, "[mii] could not list the Wii games: %s\n", err.c_str());
        }
    }
    *romId = std::atoi(prefs::get("wii_mii_channel", "").c_str());
    *title = prefs::get("wii_mii_channel_title", "");
    return *romId > 0 && !title->empty();
}

// THIS PERSON'S MIIS INTO A WII GAME'S NAND, before it boots (#275). The
// master copy is the Mii Channel's own `RFL_DB.dat`, brought up to date first
// exactly as the Mii Channel's own launch would (restoreDirSave: the save
// the launch's rule picks, savepick.h, from RomM online or its copy as last
// seen offline, and never over a save still owed to the server), from what
// the launch worker fetched for it. It replaces whatever
// the game's NAND held, so a change a game makes to the Mii list lasts until
// its next launch; the game's own zip never carries it
// (catalog::inDirectorySave). No Mii Channel, or no Miis made in it yet:
// nothing is copied, and the game is as it was.
void copyMiisInto(const SaveFetch& mii, const std::string& platformFsSlug,
                  const std::string& core, const std::string& saveDir) {
    const int miiRom = mii.romId;
    if (miiRom <= 0 || mii.title.empty()) {
        std::fprintf(stderr, "[mii] no Mii Channel on the server; the game keeps its own Miis\n");
        return;
    }
    const std::string miiWii =
        storage::savesDir(storage::currentUser(), platformFsSlug, miiRom, core) + "/User/Wii";
    if (const char* tag = catalog::saveTag(core.c_str()); tag && mii.asked)
        restoreDirSave(mii, tag, miiWii, mii.title);
    const std::vector<uint8_t> db = cab::readBytes(miiWii + "/" + wii::kMiiDatabase);
    if (db.empty()) {
        std::fprintf(stderr, "[mii] no Miis made in the Mii Channel yet; nothing copied\n");
        return;
    }
    const std::string to = saveDir + "/User/Wii/" + wii::kMiiDatabase;
    storage::makeDirs(to.substr(0, to.find_last_of('/')));
    const std::string tmp = to + ".new";
    if (!cab::writeBytes(tmp, db) || std::rename(tmp.c_str(), to.c_str()) != 0) {
        std::fprintf(stderr, "[mii] could not copy the Miis into %s\n", to.c_str());
        std::remove(tmp.c_str());
        return;
    }
    std::fprintf(stderr, "[mii] %zu bytes of Miis from the Mii Channel (rom %d) copied in\n",
                 db.size(), miiRom);
}
