#include "savepick.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>

#include "timewords.h"

namespace savepick {
namespace {

// How many of the hashes this console has had are kept. RomM keeps ten
// snapshots a channel behind the current, and the pinned ones for good, so a
// save of this console's that its website can still put back on top is among
// the last few; this is far past them.
constexpr size_t kHashesKept = 256;

int64_t madeAt(const romm::Channel& c) { return isoMicros(savedAt(c.current)); }

// A channel whose current holds a save with something in it. An empty
// channel, or a snapshot of states alone, is no save to start from.
bool holdsSave(const romm::Channel& c) {
    return c.currentId != 0 && c.current.hasSave && c.current.save.sizeBytes > 0;
}

// The head of savepick.h: this console's own note first, RomM's when it has
// none.
bool notesSayElsewhere(const Facts& f, const romm::Snapshot& s) {
    if (f.haveHeld) {
        const std::string& hash = s.save.contentHash;
        if (hash.empty() || hash == f.held.saveHash) return false;
        return std::find(f.held.hashesHad.begin(), f.held.hashesHad.end(), hash) ==
               f.held.hashesHad.end();
    }
    for (const romm::SnapshotHolder& h : s.heldBy)
        if (h.client == romm::kConsoleClient) return false;
    return s.deviceOwn && !s.deviceClient.empty() && s.deviceClient != romm::kConsoleClient;
}

}  // namespace

Pick pick(const Facts& f) {
    Pick p;
    const romm::Channel* newest = nullptr;   // the newest save in any channel
    const romm::Channel* usable = nullptr;   // the newest this console can use
    for (const romm::Channel& c : f.channels) {
        if (!holdsSave(c)) continue;
        if (!newest || madeAt(c) > madeAt(*newest)) newest = &c;
        if (c.current.save.emulator != f.tag) continue;
        if (!usable || madeAt(c) > madeAt(*usable)) usable = &c;
    }
    const bool oldRows = !f.oldRowAt.empty();
    const int64_t oldAt = isoMicros(f.oldRowAt);
    p.couldNotUseNewest = newest && newest->current.save.emulator != f.tag &&
                          (!usable || madeAt(*newest) > madeAt(*usable)) &&
                          (!oldRows || madeAt(*newest) > oldAt);
    const std::string unusable =
        p.couldNotUseNewest ? "; the newest on RomM, snapshot " +
                                  std::to_string(newest->current.id) + " (" +
                                  newest->current.save.emulator +
                                  "), is another emulator's and is not used"
                            : std::string();
    if (f.owed) {
        p.why = "a save of this game has not reached RomM yet; this console's stands" + unusable;
        return p;
    }
    // A save made before one of this console's own that RomM kept beside a
    // channel's current is never started from (the head of savepick.h).
    const int64_t ownAt = f.haveHeld && f.held.beside ? f.held.madeAt * 1000000 : 0;
    const char* older = "; RomM kept this console's own save beside a channel's current, and "
                        "the newest it could use was made before it";
    // A channel's save against the saves from before channels: the one RomM
    // took later. On a tie the channel's, which is the old save copied in.
    if (usable && (!oldRows || madeAt(*usable) >= oldAt)) {
        if (madeAt(*usable) < ownAt) {
            p.why = "this console's own save stands" + std::string(older) + unusable;
            return p;
        }
        p.from = Pick::From::Channel;
        p.channel = *usable;
        p.madeElsewhere = notesSayElsewhere(f, usable->current);
        p.why = "snapshot " + std::to_string(usable->current.id) + " in channel \"" +
                usable->label + "\"" + (usable->hardcore ? " (hardcore)" : "") +
                " is the newest save on RomM this console can use" + unusable;
        return p;
    }
    if (oldRows) {
        if (oldAt < ownAt) {
            p.why = "this console's own save stands" + std::string(older) + unusable;
            return p;
        }
        p.from = Pick::From::OldRows;
        p.why = "the newest save on RomM this console can use is from before channels" + unusable;
        return p;
    }
    p.why = "RomM holds no save of this game this console can use" + unusable;
    return p;
}

bool continuingElsewhere(const Pick& pick, bool sameAsOwn) {
    return pick.from == Pick::From::Channel && pick.madeElsewhere && !sameAsOwn;
}

savemirror::Held heldAfter(const savemirror::Held& before, const std::string& channelId,
                           const romm::Snapshot& taken, int64_t now) {
    savemirror::Held h = before;
    auto had = [&h](const std::string& hash) {
        if (hash.empty()) return;
        h.hashesHad.erase(std::remove(h.hashesHad.begin(), h.hashesHad.end(), hash),
                          h.hashesHad.end());
        h.hashesHad.push_back(hash);
    };
    had(before.saveHash);
    h.channelId = channelId;
    h.snapshotId = taken.id;
    h.digest = taken.digest;
    h.saveHash = taken.hasSave ? taken.save.contentHash : std::string();
    h.at = now;
    h.madeAt = isoMicros(savedAt(taken)) / 1000000;
    h.beside = false;
    had(h.saveHash);
    if (h.hashesHad.size() > kHashesKept)
        h.hashesHad.erase(h.hashesHad.begin(),
                          h.hashesHad.begin() +
                              static_cast<long>(h.hashesHad.size() - kHashesKept));
    return h;
}

std::string pickedAt(const Pick& pick, const std::string& oldRowAt) {
    switch (pick.from) {
        case Pick::From::Channel: return savedAt(pick.channel.current);
        case Pick::From::OldRows: return oldRowAt;
        case Pick::From::Own: break;
    }
    return std::string();
}

std::string savedAt(const romm::Snapshot& s) { return s.saveAt.empty() ? s.createdAt : s.saveAt; }

void dateSaves(std::vector<romm::Channel>* channels, const std::vector<romm::Asset>& rows) {
    for (romm::Channel& c : *channels) {
        if (c.currentId == 0 || !c.current.hasSave) continue;
        for (const romm::Asset& r : rows)
            if (r.id == c.current.save.id && !r.createdAt.empty()) c.current.saveAt = r.createdAt;
    }
}

// Judged by the extension this console and the reference implementation both
// uploaded under. Anything else (a card somebody made in RomM's web player, a
// file from another emulator) reads as the main save, which is right: that is
// the only region a foreign row could ever be.
std::string regionOfRow(const std::string& fileName) {
    auto endsWith = [&](const char* ext) {
        const size_t n = std::strlen(ext);
        return fileName.size() > n && fileName.compare(fileName.size() - n, n, ext) == 0;
    };
    if (endsWith(".cart")) return "cart";
    if (endsWith(".rtc")) return "rtc";
    // PlayStation 2's card, named the Mac's way (`cabinet-604.ps2`), and a
    // GameCube raw card from before #344. Without these lines either reads as
    // "srm": the PS2 restore would look straight past the card, and the
    // battery restore could take a 16 MB card for a battery.
    if (endsWith(".ps2")) return "ps2";
    if (endsWith(".raw")) return "raw";
    return "srm";
}

// A save from before channels, under `tag`. RomM lists a snapshot's save
// among a game's saves as well, with its channel named, and that one is the
// channels' to weigh, never an old row's.
static bool oldRowUnder(const romm::Asset& a, const std::string& tag) {
    return !tag.empty() && a.emulator == tag && a.channelId.empty();
}

const romm::Asset* newestOldRow(const std::vector<romm::Asset>& rows, const std::string& tag,
                                const char* region) {
    const romm::Asset* newest = nullptr;
    for (const romm::Asset& a : rows) {
        if (!oldRowUnder(a, tag)) continue;
        if (region && regionOfRow(a.fileName) != region) continue;
        if (!newest || a.updatedAt > newest->updatedAt) newest = &a;
    }
    return newest;
}

std::vector<const romm::Asset*> oldRowsAStartMayTake(const std::vector<romm::Asset>& rows,
                                                     const std::string& tag) {
    std::vector<const romm::Asset*> out;
    if (const romm::Asset* any = newestOldRow(rows, tag)) out.push_back(any);
    std::map<std::string, const romm::Asset*> inRegion;
    for (const romm::Asset& a : rows) {
        if (!oldRowUnder(a, tag)) continue;
        const romm::Asset*& r = inRegion[regionOfRow(a.fileName)];
        if (!r || a.updatedAt > r->updatedAt) r = &a;
    }
    for (const auto& [region, a] : inRegion)
        if (std::find(out.begin(), out.end(), a) == out.end()) out.push_back(a);
    return out;
}


// ---- `--save-pick-test`'s table ---------------------------------------------

namespace {

const char kTag[] = "gambatte-native";      // this console's saves
const char kPhoneTag[] = "retroarch-mgba";  // another emulator's

// A channel whose current is snapshot `snapshotId`, holding a save with
// `hash` filed under `tag`, made by a device of `client` and held by each of
// `holders`.
romm::Channel channelWith(const char* label, int snapshotId, const char* at, const char* tag,
                          const char* hash, const char* client,
                          std::vector<const char*> holders = {}) {
    romm::Channel c;
    c.id = std::string("channel-") + label;
    c.label = label;
    c.currentId = snapshotId;
    c.current.id = snapshotId;
    c.current.digest = std::string("sha256:") + hash;
    c.current.channelId = c.id;
    c.current.createdAt = at;
    c.current.deviceOwn = client != nullptr;
    c.current.deviceClient = client ? client : "";
    for (const char* h : holders) c.current.heldBy.push_back({"device", h, at});
    c.current.hasSave = true;
    c.current.save.id = snapshotId * 10;
    c.current.save.fileName = "Game.srm";
    c.current.save.sizeBytes = 32768;
    c.current.save.contentHash = hash;
    c.current.save.emulator = tag;
    return c;
}

savemirror::Held holding(const char* hash, std::vector<std::string> had = {}) {
    savemirror::Held h;
    h.channelId = "channel-default";
    h.snapshotId = 1;
    h.saveHash = hash;
    h.at = 1;
    h.hashesHad = std::move(had);
    h.hashesHad.push_back(hash);
    return h;
}

}  // namespace

int test() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };
    using From = Pick::From;
    const char* console = romm::kConsoleClient;
    const char* t1 = "2026-11-18T20:00:00.000000+00:00";
    const char* t2 = "2026-11-18T21:00:00.500000+00:00";
    const char* t3 = "2026-11-18T22:00:00+00:00";

    std::printf("the rule\n");
    {   // 1. This console's own save is the newest.
        Facts f;
        f.tag = kTag;
        f.channels = {channelWith("default", 12, t2, kTag, "aaaa", console, {console})};
        f.haveHeld = true;
        f.held = holding("aaaa");
        const Pick p = pick(f);
        check(p.from == From::Channel && p.channel.current.id == 12 && !p.madeElsewhere &&
                  !p.couldNotUseNewest && !continuingElsewhere(p, true) &&
                  !continuingElsewhere(p, false),
              "own save newest: started from it, no pill");
    }
    {   // 2. Another console saved later: every console is one device on RomM,
        //    so only this console's own note can tell.
        Facts f;
        f.tag = kTag;
        f.channels = {channelWith("default", 13, t3, kTag, "bbbb", console, {console})};
        f.haveHeld = true;
        f.held = holding("aaaa");
        const Pick p = pick(f);
        check(p.from == From::Channel && p.channel.current.id == 13 && p.madeElsewhere &&
                  continuingElsewhere(p, false),
              "another console newer: started from its save, with the pill");
        check(!continuingElsewhere(p, true),
              "the same bytes this console already had: never the pill");
    }
    {   // 3. Two channels: the newer save wins, whichever channel it is in.
        Facts f;
        f.tag = kTag;
        f.channels = {channelWith("default", 12, t1, kTag, "aaaa", console, {console}),
                      channelWith("second run", 20, t2, kTag, "cccc", console, {console})};
        check(pick(f).channel.label == "second run", "two channels: the newer save's channel");
        f.channels[0].current.createdAt = t3;
        check(pick(f).channel.label == "default", "two channels: and the other when it is newer");
        f.channels[0].current.createdAt = "2026-11-18T21:00:00.400000+00:00";
        check(pick(f).channel.label == "second run",
              "two channels a tenth of a second apart: still the later");
    }
    {   // 4. A hardcore channel holds the newest: picked up from, and the
        //    session knows not to save into it.
        Facts f;
        f.tag = kTag;
        romm::Channel hard = channelWith("default", 30, t3, kTag, "dddd", console, {console});
        hard.hardcore = true;
        hard.current.hardcore = true;
        f.channels = {channelWith("softcore", 12, t1, kTag, "aaaa", console, {console}), hard};
        const Pick p = pick(f);
        check(p.from == From::Channel && p.channel.current.id == 30 && p.channel.hardcore,
              "a hardcore one newest: started from it, marked hardcore");
    }
    {   // 5. A save this console has not sent yet.
        Facts f;
        f.tag = kTag;
        f.channels = {channelWith("default", 13, t3, kTag, "bbbb", console, {console})};
        f.owed = true;
        check(pick(f).from == From::Own, "a local save still owed: this console's stands");
        f.channels.clear();
        f.oldRowAt = t2;
        check(pick(f).from == From::Own,
              "a local save still owed, RomM's from before channels: this console's stands");
        f.channels = {channelWith("default", 40, t3, kPhoneTag, "ffff", "argosy", {"argosy"})};
        check(pick(f).from == From::Own && pick(f).couldNotUseNewest,
              "a local save still owed, another emulator's the newest: this console's stands, "
              "and the session still knows the newest could not be used");
    }
    {   // 5b. A save of this console's own that RomM kept beside a channel's
        //     current (savepush.h): nothing made before it is started from.
        Facts f;
        f.tag = kTag;
        f.haveHeld = true;
        f.held = holding("bbbb", {"aaaa"});
        f.held.beside = true;
        f.held.madeAt = isoMicros(t2) / 1000000;
        f.channels = {channelWith("default", 12, t1, kTag, "aaaa", console, {console})};
        check(pick(f).from == From::Own,
              "a save of its own kept beside the current, an older one still listed: its own "
              "stands");
        f.channels.clear();
        f.oldRowAt = t1;
        check(pick(f).from == From::Own,
              "a save of its own kept beside the current, an older one from before channels: "
              "its own stands");
        f.oldRowAt.clear();
        f.channels = {channelWith("default", 13, t3, kTag, "cccc", console, {console})};
        const Pick newer = pick(f);
        check(newer.from == From::Channel && newer.channel.current.id == 13 && newer.madeElsewhere,
              "a save of its own kept beside the current, the current made later: the current, "
              "with the pill");
        f.channels = {channelWith("default", 40, t3, kPhoneTag, "ffff", "argosy", {"argosy"}),
                      channelWith("softcore", 12, t1, kTag, "aaaa", console, {console})};
        const Pick stays = pick(f);
        check(stays.from == From::Own && stays.couldNotUseNewest,
              "a save of its own kept beside a newest it could not use: its own stands, never "
              "the older one it could use");
        f.held.beside = false;
        check(pick(f).from == From::Channel && pick(f).channel.current.id == 12,
              "the same lists for a console whose save is not beside anything: the newest it "
              "can use, as before");
    }
    {   // 6. An empty save on RomM is no save to start from.
        Facts f;
        f.tag = kTag;
        romm::Channel empty = channelWith("default", 14, t3, kTag, "eeee", console);
        empty.current.save.sizeBytes = 0;
        romm::Channel statesOnly = channelWith("states", 15, t3, kTag, "", console);
        statesOnly.current.hasSave = false;
        romm::Channel none;
        none.id = "channel-new";
        none.label = "new";
        f.channels = {empty, statesOnly, none};
        const Pick alone = pick(f);
        check(alone.from == From::Own && !alone.couldNotUseNewest,
              "an empty save on RomM, alone: this console's stands");
        f.channels.push_back(channelWith("older", 9, t1, kTag, "aaaa", console, {console}));
        check(pick(f).from == From::Channel && pick(f).channel.current.id == 9,
              "an empty save on RomM, newer than a real one: the real one");
    }
    {   // 7. The server away: its channels as last seen decide as they would
        //    online; never seen, nothing is known.
        Facts f;
        f.tag = kTag;
        check(pick(f).from == From::Own, "server away, nothing ever seen: this console's stands");
        f.channels = {channelWith("default", 13, t3, kTag, "bbbb", console, {console})};
        f.oldRowAt = t1;
        check(pick(f).from == From::Channel, "server away: its channels as last seen");
    }
    {   // 8. RomM's saves from before channels against a channel's.
        Facts f;
        f.tag = kTag;
        f.oldRowAt = t2;
        check(pick(f).from == From::OldRows && pickedAt(pick(f), f.oldRowAt) == t2,
              "no channel: RomM's save from before channels, as before");
        f.channels = {channelWith("default", 12, t1, kTag, "aaaa", console, {console})};
        check(pick(f).from == From::OldRows,
              "an old save newer than every channel (a console not yet updated made it)");
        f.channels[0].current.createdAt = t3;
        check(pick(f).from == From::Channel && pickedAt(pick(f), f.oldRowAt) == t3,
              "a channel's save newer than the old one");
        f.channels[0].current.createdAt = t2;
        check(pick(f).from == From::Channel, "stored at the same moment: the channel's");
    }
    {   // 9. The newest is another emulator's save.
        Facts f;
        f.tag = kTag;
        f.channels = {channelWith("default", 40, t3, kPhoneTag, "ffff", "argosy", {"argosy"})};
        const Pick alone = pick(f);
        check(alone.from == From::Own && alone.couldNotUseNewest,
              "another emulator's save, alone: this console's stands, and the session knows");
        f.oldRowAt = t1;
        const Pick withOld = pick(f);
        check(withOld.from == From::OldRows && withOld.couldNotUseNewest,
              "another emulator's save newest: the console's own from before channels");
        f.channels.push_back(channelWith("softcore", 12, t2, kTag, "aaaa", console, {console}));
        const Pick withOwn = pick(f);
        check(withOwn.from == From::Channel && withOwn.channel.current.id == 12 &&
                  withOwn.couldNotUseNewest,
              "another emulator's save newest: the newest this console can use");
        f.channels[0].current.createdAt = t1;
        check(!pick(f).couldNotUseNewest, "another emulator's save, older: nothing to say");
    }

    {   // 10. A current that changed only its states carries an older save
        //     (#346, step 6): its save is dated by its own row, not by when
        //     the snapshot was pushed.
        Facts f;
        f.tag = kTag;
        f.haveHeld = true;
        f.held = holding("bbbb", {"aaaa"});
        f.held.beside = true;
        f.held.madeAt = isoMicros(t2) / 1000000;
        f.channels = {channelWith("default", 12, t3, kTag, "aaaa", console, {console})};
        romm::Asset row;
        row.id = f.channels[0].current.save.id;
        row.createdAt = t1;
        romm::Asset other = row;
        other.id = 999;
        other.createdAt = t3;
        dateSaves(&f.channels, {other, row});
        check(f.channels[0].current.saveAt == t1 && pick(f).from == From::Own,
              "a current pushed after this console's own save kept beside it, its save first "
              "stored before: dated by the save's own row, and its own stands");
        f.held.beside = false;
        check(pick(f).from == From::Channel && pickedAt(pick(f), f.oldRowAt) == t1,
              "and the game's page says when the save was stored, not when its states were");
        f.oldRowAt = t2;
        check(pick(f).from == From::OldRows,
              "a save from before channels newer than the channel's save, though older than "
              "its snapshot: the old one");
        f.oldRowAt.clear();
        dateSaves(&f.channels, {});
        check(f.channels[0].current.saveAt == t1, "a row not listed leaves the date as it was");
    }

    std::printf("\nthe pill\n");
    {   // RomM's website put an older snapshot back on top: a new snapshot
        // with a save this console has had.
        Facts f;
        f.tag = kTag;
        f.channels = {channelWith("default", 77, t3, kTag, "aaaa", console)};
        f.haveHeld = true;
        f.held = holding("bbbb", {"aaaa"});
        check(!pick(f).madeElsewhere,
              "a save of this console's restored on RomM's website: no pill");
        f.channels[0].current.save.contentHash.clear();
        check(!pick(f).madeElsewhere, "a save RomM gave no hash: no pill");
    }
    {   // No note of the game: RomM's own say who made the snapshot and who
        // holds it.
        Facts f;
        f.tag = kTag;
        f.channels = {channelWith("default", 13, t3, kTag, "bbbb", console, {console})};
        check(!pick(f).madeElsewhere, "after a reinstall, held by a CabinetOS console: no pill");
        f.channels = {channelWith("default", 13, t3, kTag, "bbbb", "argosy", {"argosy"})};
        check(pick(f).madeElsewhere, "after a reinstall, another app's and held by it: the pill");
        f.channels = {channelWith("default", 13, t3, kTag, "bbbb", "argosy", {"argosy", console})};
        check(!pick(f).madeElsewhere,
              "after a reinstall, another app's that a console also holds: no pill");
        f.channels = {channelWith("default", 13, t3, kTag, "bbbb", console)};
        check(!pick(f).madeElsewhere,
              "after a reinstall, a console's own put back by RomM's website: no pill");
        f.channels = {channelWith("default", 13, t3, kTag, "bbbb", nullptr)};
        check(!pick(f).madeElsewhere, "after a reinstall, RomM names no device: no pill");
    }
    {
        Pick old;
        old.from = From::OldRows;
        old.madeElsewhere = true;
        check(!continuingElsewhere(old, false) && !continuingElsewhere(Pick{}, false),
              "a save from before channels, or the console's own: never the pill");
    }

    std::printf("\nthe note a start leaves\n");
    {
        const romm::Channel c = channelWith("default", 13, t3, kTag, "bbbb", console);
        savemirror::Held h = heldAfter(savemirror::Held{}, c.id, c.current, 1700);
        check(h.channelId == c.id && h.snapshotId == 13 && h.digest == "sha256:bbbb" &&
                  h.saveHash == "bbbb" && h.at == 1700 && h.hashesHad.size() == 1 &&
                  h.madeAt == isoMicros(t3) / 1000000 && !h.beside,
              "the first: the snapshot, its save's hash, when it was taken, and when RomM "
              "took its save");
        savemirror::Held wasBeside = h;
        wasBeside.beside = true;
        check(!heldAfter(wasBeside, c.id, c.current, 1750).beside,
              "a snapshot taken from RomM is no longer beside a current");
        const romm::Channel next = channelWith("default", 14, t3, kTag, "cccc", console);
        h = heldAfter(h, next.id, next.current, 1800);
        h = heldAfter(h, c.id, c.current, 1900);
        check(h.snapshotId == 13 && h.saveHash == "bbbb" && h.hashesHad.size() == 2 &&
                  h.hashesHad[0] == "cccc" && h.hashesHad[1] == "bbbb",
              "later ones: every hash it has had, once each, the held one last");
        savemirror::Held before3;
        before3.snapshotId = 5;
        before3.saveHash = "aaaa";
        h = heldAfter(before3, next.id, next.current, 2000);
        check(h.hashesHad.size() == 2 && h.hashesHad[0] == "aaaa",
              "a note that lists none keeps the hash it held");
        savemirror::Held many;
        for (int i = 0; i < 400; ++i) many.hashesHad.push_back("h" + std::to_string(i));
        h = heldAfter(many, c.id, c.current, 2100);
        check(h.hashesHad.size() == kHashesKept && h.hashesHad.back() == "bbbb" &&
                  h.hashesHad.front() == "h" + std::to_string(400 + 1 - kHashesKept),
              "the list is cut from its oldest end");
        romm::Snapshot statesOnly = c.current;
        statesOnly.hasSave = false;
        check(heldAfter(savemirror::Held{}, c.id, statesOnly, 1).saveHash.empty(),
              "a snapshot with no save holds no save's hash");
    }

    std::printf("\nRomM's saves from before channels\n");
    {
        check(regionOfRow("Game (Cabinet).srm") == "srm" && regionOfRow("Game.cart") == "cart" &&
                  regionOfRow("Game.rtc") == "rtc" && regionOfRow("cabinet-604.ps2") == "ps2" &&
                  regionOfRow("card.USA.raw") == "raw" && regionOfRow("Game.zip") == "srm",
              "a row's region by its extension");
        // Row 6 is a snapshot's save, which RomM lists with the others.
        auto row = [](int id, const char* name, const char* tag, const char* at) {
            romm::Asset a;
            a.id = id;
            a.fileName = name;
            a.emulator = tag;
            a.updatedAt = at;
            if (id == 6) a.channelId = "channel-default";
            return a;
        };
        const std::vector<romm::Asset> rows = {
            row(1, "Game (Cabinet).srm", kTag, "2026-11-01T10:00:00+00:00"),
            row(2, "Game (Cabinet).srm", kTag, "2026-11-03T10:00:00+00:00"),
            row(3, "Game (Cabinet).cart", kTag, "2026-11-02T10:00:00+00:00"),
            row(4, "Game.srm", kPhoneTag, "2026-11-09T10:00:00+00:00"),
            row(5, "Game (Cabinet).rtc", kTag, "2026-11-05T10:00:00+00:00"),
            row(6, "Game [2026-11-18_22-42-48-649].srm", kTag, "2026-11-18T22:42:48+00:00"),
        };
        const romm::Asset* any = newestOldRow(rows, kTag);
        const romm::Asset* battery = newestOldRow(rows, kTag, "srm");
        check(any && any->id == 5 && battery && battery->id == 2 &&
                  newestOldRow(rows, kTag, "cart")->id == 3 && !newestOldRow(rows, kTag, "ps2") &&
                  !newestOldRow(rows, "", "srm") && !newestOldRow(rows, "other-tag"),
              "the newest under the tag: of any kind, in a region, none for another tag, and "
              "never a snapshot's save, which RomM lists among them");
        const std::vector<const romm::Asset*> take = oldRowsAStartMayTake(rows, kTag);
        check(take.size() == 3 && take[0]->id == 5 &&
                  std::count_if(take.begin(), take.end(),
                                [](const romm::Asset* a) { return a->id == 2 || a->id == 3; }) == 2,
              "the rows a start may take: the newest of any kind and of each region, each once");
        check(isoMicros(t2) - isoMicros(t1) == 3600500000LL && isoMicros(t3) > isoMicros(t2) &&
                  isoMicros("2026-11-18T22:00:00Z") == isoMicros(t3) && isoMicros("soon") == 0,
              "RomM's times read to the microsecond, with or without a fraction");
    }
    return failures;
}

}  // namespace savepick
