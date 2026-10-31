// Settings > About's credits. Moved out of main.cpp in the restructure (#398).

#pragma once

// CREDITS AND LICENCES, Settings > About. One line per project whose work
// this console is: what it does, and its licence. The same facts as
// docs/LICENCES.md, which is the record; change one, change both. One list,
// not a Credits list and a Licences list naming the same projects twice
// (MMagTech, 2026-09-25). Full licence texts are in the image under
// /usr/share/licenses/, not on the television.
struct Credit { const char* name; const char* what; };
constexpr Credit kCredits[] = {
    {"CabinetOS", "This console \xC2\xB7 MIT"},
    {"Bazzite", "The base system \xC2\xB7 Apache 2.0"},
    {"Universal Blue", "Image tooling \xC2\xB7 Apache 2.0"},
    {"Fedora", "Under Bazzite \xC2\xB7 Per package"},
    {"gamescope", "Compositor, by Valve \xC2\xB7 BSD 2-clause"},
    {"gamescope-session", "Steam's session \xC2\xB7 MIT"},
    {"FinalBurn Neo", "Arcade \xC2\xB7 Non-commercial"},
    {"MAME 2003-Plus", "Arcade \xC2\xB7 Non-commercial"},
    {"Snes9x", "SNES \xC2\xB7 Non-commercial"},
    {"Genesis Plus GX", "Sega 8 and 16-bit \xC2\xB7 Non-commercial"},
    {"PicoDrive", "32X \xC2\xB7 Non-commercial"},
    {"Opera", "3DO \xC2\xB7 Non-commercial"},
    {"Vectrex system ROM", "Inside vecx \xC2\xB7 Non-commercial"},
    {"Vectrex screen sheets", "THK and Gigapig \xC2\xB7 MIT, art non-commercial"},
    {"Flycast", "Dreamcast, Naomi \xC2\xB7 GPL v2"},
    {"MAME 0.289", "Arcade \xC2\xB7 GPL v2"},
    {"PPSSPP", "PSP \xC2\xB7 GPL v2+"},
    {"PCSX2", "PlayStation 2 \xC2\xB7 GPL v3+"},
    {"Dolphin", "GameCube \xC2\xB7 GPL v2+"},
    {"mupen64plus-next", "Nintendo 64 \xC2\xB7 GPL v2"},
    {"PCSX ReARMed", "PlayStation \xC2\xB7 GPL v2+"},
    {"Beetle Saturn", "Saturn \xC2\xB7 GPL v2+"},
    {"Beetle PCE Fast", "TurboGrafx-16 \xC2\xB7 GPL v2+"},
    {"Beetle NeoPop", "Neo Geo Pocket \xC2\xB7 GPL v2+"},
    {"Beetle VB", "Virtual Boy \xC2\xB7 GPL v2+"},
    {"FCEUmm", "NES \xC2\xB7 GPL v2+"},
    {"Gambatte", "Game Boy \xC2\xB7 GPL v2"},
    {"ProSystem", "Atari 7800 \xC2\xB7 GPL v2+"},
    {"Stella 2014", "Atari 2600 \xC2\xB7 GPL v2"},
    {"melonDS", "Nintendo DS \xC2\xB7 GPL v3+"},
    {"Azahar", "Nintendo 3DS \xC2\xB7 GPL v3"},
    {"DraStic FreeBIOS", "DS BIOS \xC2\xB7 BSD 2-clause"},
    {"vecx", "Vectrex \xC2\xB7 GPL v3"},
    {"mGBA", "Game Boy Advance \xC2\xB7 MPL 2.0"},
    {"RPCS3", "PlayStation 3 \xC2\xB7 GPL v2"},
    {"Xenia Edge", "Xbox 360 \xC2\xB7 BSD 3-clause"},
    {"Cemu", "Wii U \xC2\xB7 MPL 2.0"},
    // Its own About screen, which its licence's section 7 asks to keep its
    // copyright line on, is never shown here, so the line is here.
    {"sm2-emu", "Model 2, Daniel Martin (dmanlfc) \xC2\xB7 GPL v3+"},
    {"Supermodel", "Model 3 \xC2\xB7 GPL v3+"},
    {"SDL2_net", "Inside Supermodel \xC2\xB7 zlib"},
    {"xemu-dashboard", "The Xbox drive \xC2\xB7 MIT"},
    {"Sunshine", "Remote Play \xC2\xB7 GPL v3"},
    {"Tailscale", "Remote Play away \xC2\xB7 BSD 3-clause"},
    {"miniupnpc", "Inside Sunshine \xC2\xB7 BSD 3-clause"},
    {"RetroArch shaders", "Screen looks \xC2\xB7 GPL, public domain"},
    {"FFmpeg", "Inside PPSSPP \xC2\xB7 LGPL v2.1+"},
    {"rapidyaml, c4core", "Inside PCSX2 \xC2\xB7 MIT"},
    {"SDL3", "Input and audio \xC2\xB7 zlib"},
    {"SDL_GameControllerDB", "The controller list \xC2\xB7 zlib"},
    {"GameTDB", "Wii and Wii U pads \xC2\xB7 Its own terms"},
    {"Mesa", "Graphics \xC2\xB7 MIT"},
    {"FreeType", "Text \xC2\xB7 FreeType License"},
    {"libjpeg-turbo, libpng", "Cover art \xC2\xB7 BSD, libpng"},
    {"libcurl", "Talking to RomM \xC2\xB7 curl"},
    {"json-c", "RomM's answers \xC2\xB7 MIT"},
    {"libarchive", "Game archives \xC2\xB7 BSD 2-clause"},
    {"rcheevos", "RetroAchievements \xC2\xB7 MIT"},
    {"zlib", "Compression \xC2\xB7 zlib"},
    {"Noto Sans", "The type \xC2\xB7 OFL 1.1"},
};
