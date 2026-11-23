// Every message the pill can show, for --notice-gallery. Moved out of
// main.cpp in the restructure (#398).

#pragma once

#include "notice.h"

// EVERY MESSAGE THE PILL CAN SHOW, for `--notice-gallery`, which walks through
// them on the television one every four seconds so their words and their look
// can be judged together — including the ones that are hard to cause for real.
// A copy of the strings at their call sites: add a message there, add it here.
struct GalleryNotice { const char* text; Tone tone; };
constexpr GalleryNotice kNoticeGallery[] = {
    {"State saved", Tone::Done},
    {"Saved on this console only", Tone::Info},
    {"No save states for this system", Tone::Info},
    {"No screenshots for this system", Tone::Info},
    {"No fast forward for this system", Tone::Info},
    {"No rewind for this system", Tone::Info},
    {"Couldn't save the state", Tone::Problem},
    {"Can't restart right now", Tone::Problem},
    {"God of War III is still downloading", Tone::Info},
    {"Couldn't forget Pro Controller", Tone::Problem},
    {"8BitDo Lite 2 is player 2", Tone::Done},
    {"Loading\xE2\x80\xA6", Tone::Busy},
    {"State loaded", Tone::Done},
    {"No saved state for this game", Tone::Info},
    {"Couldn't reach RomM", Tone::Problem},
    {"Couldn't load that state", Tone::Problem},
    {"Continuing from another device", Tone::Info},
    {"Couldn't use the newest save", Tone::Problem},
    {"That state is on Disc 3", Tone::Problem},
    {"Download removed", Tone::Done},
    {"Freeing space\xE2\x80\xA6", Tone::Busy},
    {"Couldn't set up Steam", Tone::Problem},
    {"Couldn't start Steam", Tone::Problem},
    {"Steam now has 600 GB", Tone::Done},
    {"Couldn't change Steam's storage", Tone::Problem},
    {"Removing Steam\xE2\x80\xA6", Tone::Busy},
    {"Steam removed", Tone::Done},
    {"Couldn't remove Steam", Tone::Problem},
    {"Removed. Another account still keeps it", Tone::Info},
    {"Removed. Other accounts still keep it", Tone::Info},
    {"Removed. The space frees when the game closes", Tone::Info},
    {"Removed, but its files couldn't be deleted", Tone::Problem},
    {"Update available", Tone::Info},
    {"Updated to 2026.09.28", Tone::Done},
    {"Update didn't apply", Tone::Problem},
    {"Couldn't check for updates", Tone::Problem},
    {"Couldn't update", Tone::Problem},
    {"External drive connected", Tone::Done},
    {"Couldn't use the external drive", Tone::Problem},
    {"Safe to unplug", Tone::Done},
    {"Couldn't eject the external drive", Tone::Problem},
    {"External drive removed", Tone::Info},
    {"External drive isn't exFAT, NTFS or ext4", Tone::Problem},
    {"Storage almost full", Tone::Info},
    {"Couldn't format the drive", Tone::Problem},
    {"External drive formatted", Tone::Done},
};
