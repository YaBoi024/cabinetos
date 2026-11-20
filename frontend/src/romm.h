// The RomM client for the CabinetOS frontend.
//
// See docs/PROJECT.md, Phase 4. What matters here, in the order it matters:
//
// NO PASSWORD EVER REACHES THIS PROGRAM. RomM has a device-authorisation flow:
// we ask the server to start a pairing, it returns a short code, and the person
// approves it in a browser they are already signed in to. We poll until it is
// approved and receive a scoped token we can be given and can have revoked.
// Cabinet solved first-run this way on tvOS for the same reason a console has:
// typing a password with a controller is miserable. So the whole typing burden
// on a television is ONE HOSTNAME.
//
// PLAIN HTTP MUST WORK. A self-hosted RomM on a home LAN very often speaks only
// http, and Apple's App Transport Security is the reason Cabinet had to fight
// this. Nothing on Linux forbids it, so the bug is simply avoidable — but only
// if it is not designed back in. setAddress() accepts a bare host, and probes
// rather than assumes.
//
// IT MUST NEVER BLOCK THE FRAME LOOP. Every call here is synchronous and
// blocking, which is deliberate: this class does not own a thread. Callers run
// it on workers, exactly as ImageCache does with its loader. Making it async
// internally would hide the cost and duplicate a threading model that already
// exists.
//
// ONE CLIENT IS CALLED FROM MANY THREADS AT ONCE, AND WRITTEN WHILE THEY RUN.
// The app's own client is read by the image loaders, the cover and grid
// workers, the game page's workers, the uploader and the launch and state
// workers while the frame thread changes its token (an account switch, a
// pairing) or its address (the server coming back). Each call uses its own
// CURL handle; the address, the token and the server's version are read and
// written only under `mu_`, and a request takes the address and the token
// together (`where`), so each goes out whole, to one server as one person. A
// request already on its way finishes as what it started as. Until the
// restructure (#398, batch i) the three had no lock.
//
// THE DATA IT HANDS BACK IS IN romm_types.h (a platform, a game and the
// rest), so a header that needs only the types does not take the client. The
// client's own code is in six files by job: romm_http.cpp (every request),
// romm_pairing.cpp, romm_library.cpp, romm_playdata.cpp, romm_snapshots.cpp
// (save channels and snapshots) and romm_codes.cpp, with what they share in
// romm_internal.h.

#pragma once

#include <functional>
#include <mutex>

#include "romm_types.h"

namespace romm {

class Client {
public:
    Client();
    ~Client();

    // Accepts "romm.local:8080", "192.168.0.20:8080", "http://host", "host".
    // With no scheme it tries both and keeps whichever answers, preferring
    // http for an address that is obviously local. Returns false and fills
    // `err` if neither answers.
    bool setAddress(const std::string& address, std::string* err);
    // A copy, as are the two below: the address can change while somebody
    // holds what this returned (see the head of this file).
    std::string baseUrl() const {
        std::lock_guard<std::mutex> lock(mu_);
        return base_;
    }

    // The server's version, filled in by setAddress from /api/heartbeat.
    std::string serverVersion() const {
        std::lock_guard<std::mutex> lock(mu_);
        return serverVersion_;
    }

    bool haveToken() const {
        std::lock_guard<std::mutex> lock(mu_);
        return !token_.empty();
    }
    // For `accounts::recordPairing`, which files this under the id the server
    // says it belongs to. A credential: it is never printed and never logged.
    std::string token() const {
        std::lock_guard<std::mutex> lock(mu_);
        return token_;
    }

    // A token read back from a file an account keeps (accounts.h). It is a
    // credential, so it does not belong anywhere near the repository.
    bool loadToken(const std::string& path);

    // Step one of pairing. Asks for the scopes in kScopes (romm_pairing.cpp):
    // the reads, and the three writes the console makes (its saves, states
    // and screenshots as assets, time played and favourites).
    bool beginPairing(Pairing* out, std::string* err);

    // Step two, called on the pairing's own interval.
    //   1  approved — the token is now held by this client
    //   0  still pending
    //  -1  failed; `err` says why
    //  -2  this code is over: it expired, or was refused on the phone;
    //      `err` says which. A new code is the way on.
    int pollPairing(const Pairing& p, std::string* err);

    // `/api/users/me` — the id and name behind the token. Needs the `me.read`
    // scope, which pairing already asks for.
    bool fetchCurrentUser(User* out, std::string* err);

    bool fetchPlatforms(std::vector<Platform>* out, std::string* err);

    // The person's collections, which the Library shows beside the platforms.
    // Empty is a normal answer — plenty of libraries have none — and the
    // switcher says so rather than showing a blank grid.
    bool fetchCollections(std::vector<Collection>* out, std::string* err);
    // The smart ones, `/api/collections/smart`: RomM 4.4 and later. An older
    // server answers 404, which is an empty list, not a failure.
    bool fetchSmartCollections(std::vector<Collection>* out, std::string* err);

    // platformId <= 0 fetches across every platform. Pages internally: RomM
    // caps a response and a library of thousands would otherwise arrive
    // truncated, silently.
    // `onPage` is called after each page arrives, with how many games are in
    // hand so far. It exists so the startup screen can show a number that
    // moves: this call takes several seconds on a real library and a screen
    // that says the same thing throughout is indistinguishable from a hang.
    // Optional, and the pages are fetched on the calling thread either way.
    bool fetchGames(int platformId, std::vector<Game>* out, std::string* err,
                    const std::function<void(int)>& onPage = {});

    // ONE page of roms under whatever filter the caller needs, given as a raw
    // query fragment: "platform_ids=5", "collection_id=2", "search_term=mario".
    //
    // WHY A FRAGMENT RATHER THAN A TYPED QUERY. Three callers want three
    // different filters, each of them a single key the server already
    // understands. A struct of optional fields would be the same string with
    // more ceremony, and a fourth filter would have to edit it.
    //
    // AN UNKNOWN KEY IS IGNORED, NOT REFUSED, and that is the trap in this
    // whole area. `?search=mario` returns the ENTIRE library with a 200 and is
    // indistinguishable from a filter that worked if you count rows — it was
    // very nearly reported as working. Anything added here is checked against
    // `total` and against the names, never against the row count. See
    // docs/PROJECT.md open question 28.
    //
    // `total` is what the server says matched, which is not what came back
    // when `limit` cut it short. Search shows it so a person who typed three
    // letters knows there are more than the twenty on screen.
    bool fetchRoms(const std::string& filter, int limit,
                   std::vector<Game>* out, std::string* err, int* total = nullptr);

    // ONE game, by its id, whatever list it is or is not on. For `--launch`,
    // which since 2026-09-22 can no longer find a game in the library unless
    // it is on Home: boot stopped fetching the whole catalogue.
    //
    // `*notFound`, on a false return, says RomM answered 404 for the game's
    // own entry: read off the answer's status, never off `err`'s words. A
    // server away, a timeout, a 401 or 403, a 5xx or any other answer leaves
    // it false. On RomM 5.4.0 a 404 also means the game is hidden from the
    // person asking (a hidden platform or game, or an age limit), in the same
    // words, so it is "not there for this person" (gonegames.h).
    bool fetchGame(int romId, Game* out, std::string* err, bool* notFound = nullptr);

    // EVERY GAME ID THIS PERSON CAN SEE, in one answer (`GET
    // /api/roms/identifiers`, RomM 5.0 and later, a JSON array of ids): 10.8 KB
    // and 30 ms for 2,343 games on RomM 5.4.0. Hidden games are left out, as
    // everywhere. False when it did not answer as a list: a server before
    // 5.0 answers its `/api/roms/{id}` route, which refuses the word.
    bool fetchGameIds(std::vector<int>* out, std::string* err);

    // A few facts about a game from RomM's merged metadata (`metadatum`), for
    // the launch screen: the year, the first company named, how many can
    // play. Any can be missing; a game RomM never matched has none.
    using Facts = romm::Facts;   // romm_types.h
    bool fetchFacts(int romId, Facts* out, std::string* err);

    // TIME PLAYED (#128), playtime.h. `POST /api/play-sessions` takes up to a
    // hundred at once; `outcome` says, per entry, 1 RomM has it (new, or a
    // duplicate of one already sent), -1 RomM refused it for good (its end is
    // in the future), 0 not known. RomM spots a duplicate by game and start
    // time, so sending one twice after a lost answer counts it once.
    using PlaySession = romm::PlaySession;   // romm_types.h
    bool postPlaySessions(const std::vector<PlaySession>& list, std::vector<int>* outcome,
                          std::string* err) const;
    // The sum of this person's sessions for a game, as this console's device
    // sees them (playtime.h, "whose time RomM returns"). The request names
    // that device (deviceId), which RomM 5.4.0 requires (#401).
    bool fetchPlayedMs(int romId, int64_t* ms, std::string* err) const;
    // The device this token is, RomM's `current_device_id` from
    // /api/users/me, asked once per token and kept.
    bool deviceId(std::string* out, std::string* err) const;

    // A value going INTO a query fragment, percent-encoded strictly. A search
    // term is whatever somebody typed on a television keyboard, so it can hold
    // a space, an ampersand or an apostrophe — all of which would otherwise
    // end the value or start another parameter.
    static std::string encodeQueryValue(const std::string& in);

    // The games with play history, most recent first — the same query RomM's
    // own web home screen makes, so CabinetOS agrees with the web UI (as it did
    // with Cabinet) about what you were last playing.
    //
    // PLAY HISTORY LIVES ON THE SERVER, not on the console. A game played on
    // another device is recent here the moment this console is paired, which is what
    // fills Recent on a machine that has never launched anything.
    bool fetchRecent(int limit, std::vector<Game>* out, std::string* err);

    // The games marked favourite, in RomM's title order: RomM keeps no date
    // for a favourite. Home's second shelf, in the console's own order
    // (favorites.cpp, keepFavorites), and only drawn when there are any — an
    // empty Favorites row is worse than none.
    bool fetchFavorites(int limit, std::vector<Game>* out, std::string* err);

    // Every firmware file a platform carries. Empty is a normal answer: most
    // platforms need none.
    bool fetchFirmware(int platformId, std::vector<Firmware>* out, std::string* err);

    // A game's own files (`GET /api/roms/{id}`), the one its save channels
    // belong to marked (RomFile::channelFile), and the server path that
    // fetches one of them as it is, for fetchToFile.
    bool fetchRomFiles(int romId, std::vector<RomFile>* out, std::string* err);
    static std::string romFilePath(int romId, const RomFile& f);

    bool fetchSaves(int romId, std::vector<Asset>* out, std::string* err);
    // Many games' saves at once (#88): RomM 5.3 takes `rom_ids` repeated, up
    // to 500 a call. When that call fails, on a server before 5.3 or for any
    // other reason, it asks one game at a time and skips a game whose own
    // request fails, naming it in `skipped` (its saves are unknown, not
    // none); false when one fails before any request has answered (the
    // server is not there).
    bool fetchSavesFor(const std::vector<int>& romIds, std::vector<Asset>* out,
                       std::string* err, std::vector<int>* skipped = nullptr);
    // EVERY STATE OF THE GAME THIS PERSON HAS ON RomM, one row each: the
    // ones some snapshot holds (a channel's current, its history, a branch),
    // with their channel and bank name (Asset::core), and the ones from
    // before channels. RomM's rows carry no content hash
    // (fetchChannelHistory does).
    bool fetchStates(int romId, std::vector<Asset>* out, std::string* err);

    // `/api/saves/{id}/content` or `/api/states/{id}/content`. Small enough to
    // hold: a state is hundreds of kilobytes, a memory card is 128.
    std::vector<uint8_t> fetchAsset(const char* kind, int assetId) const;
    // A SAVE IN ONE EMULATOR'S OWN FORM (#346, step 8): `/api/saves/{id}/
    // content?<query>`, the query naming the emulator as Sigil spells it, its
    // options and its profile, already escaped. For a system RomM converts it
    // answers with the files that emulator reads, through Sigil: one file
    // raw, with its path in the emulator's save folder in `X-Save-Path`, or
    // several as a zip of those paths. A cartridge system's save comes as
    // stored. Every answer is kept, a refusal with RomM's code for it.
    SaveAnswer fetchSaveAs(int saveId, const std::string& query) const;

    // A state and a save go up inside a snapshot (pushSnapshot, below).

    // SAVE CHANNELS AND SNAPSHOTS (RomM 5.5, #346; the words are in
    // romm_types.h). Both in romm_snapshots.cpp, as the person the token is
    // and from the device it is bound to, which RomM records as holding what
    // it pushes.
    //
    // This person's channels on these files (`GET /api/channels`, a
    // `rom_file_id` for each, a hundred to a call), each with its current
    // snapshot. Empty is an answer: no game of these was saved in a channel.
    // False when a call fails; RomM refuses the whole call (404) when one of
    // the files is gone or hidden from this person, which `*notFound` says,
    // read off the answer's status as fetchGame's is.
    bool fetchChannels(const std::vector<int>& romFileIds, std::vector<Channel>* out,
                       std::string* err, bool* notFound = nullptr) const;
    // ONE CHANNEL'S HISTORY, newest first (`GET /api/snapshots`, its first
    // page of a hundred): every snapshot in it, branches too, each with its
    // states and their content hashes, which the game's list of states
    // (fetchStates) does not carry. False when the call fails.
    bool fetchChannelHistory(const std::string& channelId, std::vector<Snapshot>* out,
                             std::string* err) const;
    // One push (`POST /api/snapshots`): the manifest, and the bytes `push`
    // carries. True when RomM answered, whatever it answered (`*answer`);
    // false when no answer came.
    bool pushSnapshot(const SnapshotPush& push, PushAnswer* answer, std::string* err) const;

    // MAKES A GAME ONE OF THIS PERSON'S FAVOURITES, OR NOT (#267). On RomM 5
    // a favourite is a member of their collection flagged `is_favorite`, so
    // this adds the game to it or takes it out (`POST`/`DELETE
    // /api/collections/<id>/roms`), making the collection first if they have
    // none. Needs the `collections.write` scope; `*forbidden` says the
    // pairing lacks it, which only pairing again can mend. `*cannotMake`
    // says RomM refused to make that collection (#474): it answers 500 on
    // 5.3.1 and 409 on 5.4.0 when the person already owns an ordinary
    // collection named Favorites, which asking again does not mend.
    bool setFavorite(int romId, bool on, bool* forbidden, std::string* err,
                     bool* cannotMake = nullptr);

    // A screenshot into this person's gallery on RomM 5.1 (`POST
    // /api/screenshots?rom_id=`: stored under the user, private until shared),
    // not among the pictures RomM's metadata sources give a game (#79).
    bool uploadScreenshot(int romId, const std::string& fileName,
                          const std::vector<uint8_t>& png, std::string* err) const;

    // For ImageCache::Loader. Returns empty on any failure, because a cover
    // that will not load is not an error the frame loop can do anything about.
    //
    // For COVERS, not for ROMs: it holds the whole body in memory, which is
    // right for 50 KB of PNG and catastrophic for the 1.78 GB arcade set in the
    // reference library. Anything that might be a game goes through
    // fetchToFile.
    std::vector<uint8_t> fetchBytes(const std::string& path) const;

    // The first `bytes` of a game's file, by an HTTP range: how the console
    // reads a Wii game's code itself when RomM has none (wii.h). Empty on any
    // failure, which the caller must not take for an answer.
    std::vector<uint8_t> fetchHead(const Game& g, size_t bytes) const;
    // `bytes` from `offset`, the same way: how the console reads a Wii U
    // game's product code off the end of its file (wiiu.h). A server that
    // ignores the range gets no answer here unless `offset` is 0.
    std::vector<uint8_t> fetchRange(const Game& g, uint64_t offset, size_t bytes) const;

    // Streams a body straight to disk, never holding more than a buffer of it.
    //
    // `onProgress` is called from inside the transfer with bytes-so-far and the
    // total the server declared, which may be 0 when it declines to say. It
    // returns false to abort — that is how a cancel reaches a download that is
    // already running. It is called on whatever thread drove the request, so it
    // must not touch the UI directly.
    using ProgressFn = std::function<bool(int64_t got, int64_t total)>;
    bool fetchToFile(const std::string& path, const std::string& destPath,
                     const ProgressFn& onProgress, std::string* err) const;

private:
    // Gives every Wii and GameCube game in `games` from `first` on a code:
    // RomM's, else one this console read before, else read now off the file
    // (wii.h). And every
    // Wii U game its product code, which RomM does not have (fillWiiUCode).
    void fillWiiCodes(std::vector<Game>& games, size_t first = 0) const;
    void fillWiiUCode(Game& g) const;
    bool fetchFiltered(const char* filter, int limit, std::vector<Game>* out,
                       std::string* err);
    // One part of a multipart/form-data body: a file, or a plain field when
    // `fileName` is empty (a push's manifest). `data` is held by the caller
    // until the request returns.
    struct Part {
        std::string name;
        std::string fileName;
        const char* type = "application/octet-stream";
        const void* data = nullptr;
        size_t size = 0;
    };
    // Every part in one POST (romm_http.cpp). True only for a 2xx; `*reply`
    // and `*status` are what came back either way, status 0 when nothing
    // did.
    bool postMultipart(const std::string& path, const std::vector<Part>& parts,
                       std::string* err, std::string* reply = nullptr,
                       long* status = nullptr) const;
    // `*status` is the answer's HTTP status, 0 when none came.
    bool get(const std::string& path, std::string* body, std::string* err,
             long* status = nullptr) const;
    bool postJson(const std::string& path, const std::string& json,
                  std::string* body, long* status, std::string* err) const;
    // postJson with another method: DELETE carries a body on RomM's
    // collection calls.
    bool sendJson(const char* method, const std::string& path, const std::string& json,
                  std::string* body, long* status, std::string* err) const;
    // `report` false: a failure to connect is not told to serverAway, for
    // the first of setAddress's two schemes (#479); an answer still is.
    bool probe(const std::string& candidate, bool report = true);

    // What a request is sent with, the address and the token, copied
    // together under the lock (romm_http.cpp).
    struct Where {
        std::string base;
        std::string token;
    };
    Where where() const;
    // get, sent to whatever `at` names. probe asks a candidate this way
    // without making it the address until it has answered, and says whether
    // what it met is reported (`report`, as probe's). A redirect off the
    // server is not followed (#380); with `movedTo` its address is put there
    // instead of logged. `*status` as get's.
    bool getAt(const Where& at, const std::string& path, std::string* body,
               std::string* err, bool report = true, std::string* movedTo = nullptr,
               long* status = nullptr) const;

    // GUARDS THE THREE BELOW. Nothing is done while holding it but a copy or
    // an assignment, never a request (see the head of this file).
    mutable std::mutex mu_;
    std::string base_;
    std::string token_;
    mutable std::string deviceId_;   // the token's device, once asked (deviceId)
    std::string serverVersion_;
};

// WHETHER THE SERVER IS AWAY (#88, offline play). Every request reports what
// it met: one that could not reach the server at all (no connection, no name,
// or a deadline before any connection) sets it, and any answer clears it.
// Read from any thread. `serverAwaySince` is when it last went away, in
// seconds since the epoch. `noteTransport` is the requests' own hook, taking
// a CURL handle and a CURLcode so this header needs no curl.
bool serverAway();
void setServerAway(bool away);
long long serverAwaySince();
void noteTransport(void* curlHandle, int curlCode);

// THE SERVER'S VERSION AS LAST HEARD: the newest heartbeat this run that
// answered with one, from any client ("5.5.0-alpha.2"); empty before the
// first. Read from any thread. Each client's own serverVersion() is only
// what that client was told; the once-a-minute heartbeat (offline_sync.cpp)
// is a client of its own, so a RomM updated while the console runs shows
// here within a minute. Change server address asks the address typed
// (server::check), which may be another server's; the next minute's
// heartbeat puts this console's back. System update writes it down
// (updateflow.cpp).
std::string heardServerVersion();
// WHETHER THAT SERVER HAS SAVE CHANNELS (#346): its heartbeat carries RomM's
// `SAVE_SYNC` block, which RomM 5.5 added (5.4.1's has none; Argosy reads
// the same block). Heard with the version, from the same heartbeat; false
// before the first.
bool heardServerChannels();

// RomM'S CONTENT HASH of a save's or a state's bytes, the name a snapshot
// gives each (RomM 5.5, `hash_save_content`): the MD5 of the bytes in lower
// case hex; for a zip, the MD5 of one line per file in it, sorted by name,
// "<name>:<that file's MD5>", joined by "\n", folders left out. Each name is
// read as RomM reads it: as UTF-8 when the zip marks it so, as CP437 when it
// does not. A zip libarchive cannot read, or whose names it reads otherwise
// than the zip's central directory holds them, is hashed as bytes. A hash
// that is not RomM's makes RomM refuse the push (romm_snapshots.cpp).
std::string contentHash(const std::vector<uint8_t>& bytes);

// A LIST OF CHANNELS IN RomM'S OWN FORM, as `GET /api/channels` answers it
// (romm_snapshots.cpp). readChannels reads RomM's answer; channelsJson writes
// every field it reads, under RomM's names, so the copy the console keeps of
// a game's channels (savemirror.h) reads back through the same reader and
// there is one spelling of a channel. readChannels adds to `*out`, and is
// false when the text is not a list.
bool readChannels(const std::string& body, std::vector<Channel>* out, std::string* err);
std::string channelsJson(const std::vector<Channel>& channels);

// `--romm-snapshots-test`: the snapshot client's reading of RomM's answers,
// recorded from the write-test RomM 5.5.0-alpha.2, and its content hash
// against RomM's own (romm_snapshots_test.cpp). No network. 0 when it passes.
int snapshotsTest();

// True for addresses that are obviously on a local network, which decides
// whether http or https is tried first. A hostname with no dots is local by
// construction, and so is anything in the private ranges, Tailscale's
// 100.64.0.0/10, or .local/.lan.
bool looksLocal(const std::string& host);

// One path SEGMENT, a file name RomM gave us, escaped whole: everything but
// the unreserved characters becomes %XX, so `#`, `?`, `&`, `%2F` and a space
// in a name are the name and never a delimiter (#402: a one-file game whose
// name began with # or ? was cut off before the server saw it). Every
// download URL that carries a file name uses this.
std::string encodeSegment(const std::string& name);

// `--romm-race-test <address> <seconds>`: one client called from many threads
// while its address and its token are written, against a stand-in server at
// `address`, never RomM (romm_racetest.cpp says what the stand-in answers).
// Prints what came back and returns 0 when every request went out whole, 3
// when some could not reach the server, 1 when one went out wrong.
int raceTest(const std::string& address, int seconds);

}  // namespace romm
