// The self-tests that need no network, server, pad or screen, run from
// earlyProbe (probes.cpp): --first-run-writes, --accounts-test,
// --first-run-rules and --files-test; and --save-notes-test, run from
// cli.cpp. Each works in a scratch root of its own. Moved out of main.cpp in
// the restructure (#398).

#pragma once

int firstRunWriteTest();
int accountsTest();
int firstRunRules();
int filesTest();
int saveNotesTest();
