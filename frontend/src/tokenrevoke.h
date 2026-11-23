// The tokens this console is done with, revoked on RomM (#545): the one a
// re-pairing replaces, a removed person's, and everybody's at Sign out. Before
// this RomM kept every token the console was ever given, so one copied off a
// console before its Sign out went on working.
//
// A TOKEN REVOKES ITSELF, which is all RomM 5.5 lets the console's narrow
// token do (romm::Client::revokeOwnToken). It goes by the number RomM gave it
// with the pairing, kept beside it (accounts::Login). One paired before that
// has no number and is never revoked, only said: RomM lists a person's tokens
// to any of them and marks none as the one asking, so there is nothing to go
// by but a guess. The next pairing of that person has one.
//
// NOTHING STILL USES ONE WHEN IT GOES. A request reaching RomM with a revoked
// token is answered 401, so each waits until no client here has it and no
// request is on its way with it (romm::tokenHeld), and until nobody here
// signs in with it.
//
// OWED ON THE DISK (`revoke.json`, beside the logins and as private, 0600),
// written before the login itself changes. A power cut, a server that is away
// or the end of the program leaves it there, and the next start sends it. Sign
// out alone gives each one try: its files go with everything else.
//
// NOTHING HERE STOPS ANYTHING OR SHOWS ANYTHING. A re-pairing, a Remove and a
// Sign out go on whatever RomM says, and what it said is in the journal, with
// the person's name and the token's number, never the token.

#pragma once

#include <atomic>
#include <string>

#include "accounts.h"

namespace tokenrevoke {

// `who`'s login is done with: on the list, to be revoked once nothing uses
// it. Called before the login changes. Nothing to do for a login with no
// token; one with no number is said and left to RomM's own list of tokens.
// Writes a file, reaches no network.
void owe(const accounts::Login& login, const std::string& who);

// Revokes what is owed and no longer used, at `address`. From the reconnect
// thread (offline_sync.cpp), every few seconds while the server is there, so
// nothing owed costs reading a file that is not there. False when RomM did
// not answer: ask again in a minute. `stop` is looked at between tokens, and a
// server that hangs is given kSendOwedSeconds, since the exit joins the thread.
constexpr int kSendOwedSeconds = 5;
bool sendOwed(const std::string& address, const std::atomic<bool>* stop = nullptr);

// SIGN OUT (server::finishSignOut), before the logins go: what is owed and
// every account's own, one try each, and none after the first with no answer.
// Over in kSignOutSeconds however the server behaves, the address of one that
// is gone included.
constexpr int kSignOutSeconds = 3;
void signOut(const std::string& address);

// `--token-revoke-test`: all of it against a stand-in RomM on loopback inside
// the test, no other network (romm_tokens_test.cpp). 0 when it passes.
int test();

}  // namespace tokenrevoke
