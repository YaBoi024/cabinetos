// Settings' row ids (#398, restructure batch e): handed out by the row
// builders and tested by the presses, in settingsflow.cpp and the feature
// files. Out of the App word for word; app.h includes it.

#pragma once

enum SettingId { SetAddAccount = 1, SetInterfaceSounds, SetPinSet, SetPinChange,
                 SetPinOff, SetRemoveAccount, SetScreenOff, SetWifi, SetServer,
                 SetUpdate, SetUpdateCheck, SetCredits, SetFiles, SetDownloads,
                 SetShortcuts, SetShortcutButton, SetAppearance,
                 SetDarkHours, SetColour, SetRumble, SetPictureQuality,
                 SetWiiRemotes, SetSteam, SetSteamShow, SetRetroAchievements,
                 SetAchievementSound, SetDeveloper, SetVersion, SetRemotePlay,
                 SetPairedDevices, SetTailscale, SetReport, SetHowTo, SetPads,
                 SetWhatsNew,
                 SetSleepAfter, SetChargeLimit, SetDimAfter, SetFriend, SetJoin };   // FORK (handheld)
// One Eject row per USB drive: this plus the drive's index in
// storage::locations() when the rows were built.
static constexpr int kSetEject = 100;
// One Format row per drive whose row formats it: this plus its index in
// `formatable`, the drives as they were when the rows were built.
static constexpr int kSetFormat = 200;
