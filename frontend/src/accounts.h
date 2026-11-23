// The accounts this console knows, and which one it is acting as.
//
// No screens, no drawing. The switcher's picture is somewhere else; this is the
// list, the tokens, and the rules about what may be removed.
//
// docs/PROJECT.md open question 26 has the decisions and the reasoning. The
// four that shape this file:
//
//   1. ONE SERVER PER CONSOLE. Every account is a user on the one paired
//      server, so `/etc/cabinetos/session.env` stays machine-wide and an
//      account carries no address of its own.
//   2. The console boots as whoever played last.
//   3. Switch, add, remove and an optional PIN all ship together.
//   4. The PIN is off by default.
//
// --- WHY THE KEY IS RomM's OWN USER ID -------------------------------------
//
// The reference implementation keys a profile on a locally-generated UUID, and
// has to: `TVProfile` carries its own server URL, so two profiles can sit on
// one host and collide on a host-keyed token. Deciding on one server per
// console removes that case entirely, and the id RomM already issues is then
// unique across everything this machine will ever see.
//
// The payoff is not tidiness. `storage::userDir` is `users/<id> - <name>`,
// keyed on that same id, so the account list and the save tree agree without
// anything translating between them — and the save tree was built before this
// feature was discussed.
//
// **IF ONE SERVER PER CONSOLE IS EVER REVERSED, THE FOLDER NAME BREAKS FIRST.**
// User 1 on two different servers is one directory, and `romm_types.h`
// already records the underlying limit: two RomM instances both have a User:1
// and RomM exposes no instance identity to tell them apart. Say so out loud
// before changing this.
//
// --- WHY THE TOKEN AND THE LIST LIVE IN DIFFERENT PLACES --------------------
//
// A token is a credential and lives under `$HOME/.config/cabinetos/`, 0600, the
// same tier the single token already used. The list is not a credential — it is
// cached identity, the same kind of thing as `config/user.json` — so it lives
// beside that, under the storage root, where it survives with the save tree
// rather than with the session user's home directory.
//
// That split is the reference implementation's too: profiles in UserDefaults,
// tokens in the Keychain.

#pragma once

#include <string>
#include <vector>

namespace romm { class Client; }

namespace accounts {

// One RomM account this console has been paired to.
//
// No server address, deliberately: see the header comment. If a field ever
// needs to be added here, ask whether it belongs to the ACCOUNT or to the
// MACHINE first — that is the distinction this whole feature rests on.
struct Account {
    // RomM's own user id, from /api/users/me. The key, and the same id
    // `storage::User` uses for the save directory.
    int id = 0;
    std::string name;
    // The endpoint that actually serves the picture, not RomM's reported
    // `avatar_path` — see `romm::User::avatarPath` for why those differ and
    // which one is a 404. Empty is normal and means a lettered disc.
    std::string avatar;
    // Their colour for the menus, as look::colourWord writes it. Empty is
    // purple. The account's and not the machine's: MMagTech, 2026-09-24, the
    // console takes on the colour of whoever is signed in (#75).
    std::string colour;

    bool valid() const { return id > 0; }
};

// Where things are. Exposed for the probes and for tests; nothing else should
// need to build these paths itself.
std::string listPath();              // <storage config>/accounts.json
std::string tokenPath(int id);       // ~/.config/cabinetos/accounts/<id>.json

// --- Reading ---------------------------------------------------------------

// Every account, in the order they were added. Re-read from disk each call;
// this list is small and never hot.
std::vector<Account> all();

// Which account the console is acting as, or 0 when it has never been set.
int activeId();

// WHO OWNS THIS CONSOLE: the first account from setup, which is the first in
// the list, since the list is kept in the order accounts were added. 0 when
// there is none. docs/SETTINGS.md, Accounts: the owner is who sets the PIN,
// and the PIN stops anybody else switching into them.
//
// Read from the order rather than written down as its own field, because
// "first" is the whole rule and a second record of it could disagree.
// Removing an account (#58) must refuse the owner, or ownership would pass
// silently to whoever was added next.
int ownerId();

// One account by id, or nullptr. The pointer is into a caller-owned vector, so
// this takes the list rather than hiding a static.
const Account* find(const std::vector<Account>& list, int id);

// --- Writing ---------------------------------------------------------------

// Records a freshly-paired account and its token. Does NOT make it active:
// adding somebody must not sign out whoever is already playing, which is the
// reference implementation's rule and the right one.
//
// Fails if `a` is not valid or the token is empty, because a half-written
// account is worse than none: see firstrun::completion() (firstrun.h), which
// decides whether this console has been configured by asking whether an
// account is active.
bool add(const Account& a, const std::string& token, std::string* err);

// Forgets an account and its token.
//
// REFUSES THE ACTIVE ACCOUNT, and that is not a UI convenience: removing who
// the console is signed in as leaves it holding an identity whose token has
// just been deleted, with no way back except re-pairing. The switcher disables
// that row too; this is the enforcement rather than the hint.
//
// REFUSES THE OWNER too (the first account; see ownerId), for the same kind
// of reason: ownership would pass silently to whoever was added next.
//
// Does NOT touch `users/<id> - <name>/`. Forgetting a login is not the same as
// throwing away somebody's saves; Settings' Remove (#194) deletes the folder
// itself, after sending what is owed, and calls this last.
bool remove(int id, std::string* err);

// Whether remove() would agree, asked BEFORE anything of theirs is deleted.
bool canRemove(int id, std::string* err);

// Makes `id` the account the console acts as. Writes the choice down; it does
// NOT re-point the client or reload anything — that is `activate`.
bool setActive(int id, std::string* err);

// Their colour for the menus. Silent no-op for an unknown id.
bool setColour(int id, const std::string& colour);

// --- Switching -------------------------------------------------------------

// Puts `id`'s token into `client` and records it as active.
//
// **THIS IS HALF OF A SWITCH AND THE SMALLER HALF.** It changes who the console
// will be the next time anything asks. It does not re-resolve the user, does
// not refetch the library, and above all does not clear what the previous
// account put on the screen. See open question 26: Home, favourites and recents
// are the server's and belong to the account, and this console also holds a
// per-user download queue, pending uploads and a keep list. The caller owns
// that teardown, and it has to happen before anything is drawn again.
bool activate(int id, romm::Client& client, std::string* err);

// --- Pairing, and the token the console starts with -------------------------

// Puts the ACTIVE account's token into `client`. False when this console has
// no account, which is a machine that has not been paired — first run's job,
// not this file's.
bool loadActiveToken(romm::Client& client);

// WHAT A PAIRING TURNED OUT TO BE. The caller has to tell somebody, and the
// two outcomes look identical from the inside — both are a successful pairing
// that wrote a valid token.
//
// **THIS EXISTS BECAUSE THE CONSOLE LIED.** On 2026-09-22 MMagTech paired on
// the television, was told "Added. Choose them to switch." and found nobody
// new to choose. The approval had been made in a browser still signed in as
// him, so `add` correctly replaced his own row rather than creating one — and
// the journal said `added, now 1 accounts`, which is a success message for a
// no-op. Reporting a thing that did not happen is worse than failing.
struct Paired {
    int id = 0;
    std::string name;
    // False when this id was already on the console. Not an error — re-pairing
    // somebody is a legitimate way to refresh a token — but it is NOT adding
    // anybody, and the difference is the whole point of this struct.
    bool isNew = false;
};

// Turns a client that has just been through pairing into an account.
//
// THIS IS THE ONE PLACE A PAIRING BECOMES AN ACCOUNT, and it asks the server
// who the token belongs to rather than being told: the id it files the
// credential under is the same id `storage::userDir` builds a save path from,
// and filing it under a guess is how somebody writes into another person's
// directory. `client` must already hold a token.
//
// The first account on a console with none becomes active. A later one does
// not, because adding somebody must not sign out whoever is playing.
bool recordPairing(romm::Client& client, Paired* out, std::string* err);

// THERE IS NO MIGRATION FROM THE SINGLE TOKEN THAT CAME BEFORE THIS, AND THAT
// IS DELIBERATE — MMagTech, 2026-09-21: nobody else is running this. The two
// machines here were written into `accounts.json` by hand, once. It is the same
// call the folder layout made: "there is no migration tool in the tree and
// there should not be — the one machine that needed moving has been moved."
//
// If that ever stops being true, the honest fix is a one-shot probe that is
// deleted afterwards, not a permanent branch in the startup path serving users
// who do not exist.

// --- The PIN ---------------------------------------------------------------
//
// WHAT THIS IS AND IS NOT WORTH, stated plainly because the file it writes is
// readable. There is no crypto library in this binary's link line, so the PIN
// is stored as it was typed, in a 0600 file under the session user's home.
//
// **It is a deterrent against a sibling with a controller, which is the threat
// it exists for.** It is NOT a secret against somebody with a shell, and it was
// never going to be: Developer access hands out SSH deliberately, and open
// question 9 gives a person their own files over SFTP on purpose. Anybody who
// can read this file could read the token beside it, which is worth more.
//
// Do not "fix" this with a hand-rolled hash. An unreviewed digest in this tree
// would buy nothing against that threat model and would look like it did.

bool pinIsSet();
// An empty string clears it, which is how the PIN is turned off.
bool setPin(const std::string& pin, std::string* err);
// False when no PIN is set, so a caller cannot accidentally gate on a console
// that has none — check `pinIsSet` to decide whether to ask at all.
bool checkPin(const std::string& pin);

// --- RetroAchievements (#74) -----------------------------------------------
//
// A person's RetroAchievements sign-in: their username and the token the
// server gave in exchange for the password, which is never kept. Beside their
// RomM token and as private (0600 in a 0700 folder), because it is a
// credential for their RetroAchievements account. Removed with the account,
// and by forgetEveryone with the folder.
std::string raPath(int id);   // ~/.config/cabinetos/accounts/<id>.retroachievements.json
bool raLogin(int id, std::string* username, std::string* token);
// The person's RetroAchievements answers kept for playing offline, and the
// unlocks still to send (achievements.cpp). Made 0700 on first use.
std::string raDir(int id);    // ~/.config/cabinetos/accounts/<id>.retroachievements/
bool setRaLogin(int id, const std::string& username, const std::string& token);
void clearRaLogin(int id);

// --- Signing out -----------------------------------------------------------

// Every account on this console, their tokens and the PIN, for signing out of
// the server they belong to (issue #61). The next account paired, in first
// run, owns the console and sets its own PIN. True when nothing is left.
bool forgetEveryone();

}  // namespace accounts

// Who the console acts as, settled before anything writes a save: asked of
// RomM online, read from accounts.json offline (#88). Outside the namespace,
// as their callers name them. Moved out of main.cpp in the restructure (#398).
bool adoptUser(romm::Client& client);
bool adoptAccountOffline(int id);
