// A QR code, as a grid of modules. Nothing here draws anything.
//
// WHY A CONSOLE NEEDS ONE. Pairing with RomM is a device authorisation: the
// server hands back a short code and a URL, and somebody has to approve it in a
// browser they are already signed in to. A television has no browser, so the
// URL has to get to a phone, and the only way to move a URL to a phone across a
// room is to put it on the screen in a shape a camera can read. Cabinet's tvOS
// app did exactly this in `PairingView`, and it is the reason the ENTIRE typing
// burden of first run is one hostname — the password is never typed on the
// television at all. See docs/history/CABINET.md and PROJECT.md, question 15b.
//
// WHAT THIS FILE IS AND IS NOT. `encode()` turns a string into a square of
// black and white modules. It has no idea what a pixel is, what colour anything
// should be, or how big it should be drawn. That split is deliberate and it is
// what makes this buildable before the screens are: the matrix is a MEASUREMENT
// — a scanner either reads it or does not — while its size on a television, its
// quiet zone and the panel around it are a picture, and pictures wait.
//
// WHY IT IS WRITTEN OUT RATHER THAN LINKED. A QR encoder is about five hundred
// lines of well-specified arithmetic with a fixed, published answer, and the
// alternative is a dependency in the image and in the build container for one
// screen that runs once in a console's life. The spec is ISO/IEC 18004; the
// tables below are from it and each one says which.
//
// THE SCOPE IS DELIBERATELY SMALL. Byte mode, versions 1 to 10, and that is
// all. A RomM verification URL is around fifty characters — version 4 at error
// correction M holds 62 bytes and version 10 holds 213 — so ten versions is
// roughly four times the headroom anything here will ever need. Longer input is
// REFUSED and says so, rather than silently producing something unscannable.
//
// ERROR CORRECTION LEVEL M, and it is a real choice. L recovers 7% and M
// recovers 15% for about one version of extra size. The thing being
// photographed is a television, at an angle, by a hand-held phone, under a
// lamp; that is the situation the higher level exists for. Cabinet's tvOS app
// used CoreImage's default, which is also M.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace qr {

// A square grid. `size` is the module count per side — 21 for version 1, then
// 4 more per version, so 57 at version 10.
//
// There is NO QUIET ZONE in here. The spec asks for four modules of clear space
// on every side, and it is the single commonest reason a code will not scan —
// but it is margin, which is layout, which belongs to whatever draws this. The
// renderer that uses it must add it, and the test below is what checks that it
// did: a code drawn edge to edge is unreadable and looks perfect.
struct Code {
    int size = 0;
    std::vector<uint8_t> modules;   // size*size, 1 = dark

    bool valid() const { return size > 0; }
    bool at(int x, int y) const {
        if (x < 0 || y < 0 || x >= size || y >= size) return false;
        return modules[static_cast<size_t>(y) * size + x] != 0;
    }
};

// Encodes `text` at error correction level M. Returns an invalid Code and fills
// `err` when the text will not fit in version 10 — which for our use would mean
// a RomM verification URL four times longer than any that exists, and is worth
// an error rather than a guess.
Code encode(const std::string& text, std::string* err = nullptr);

// --- The instruments --------------------------------------------------------
//
// A QR code cannot be checked by looking at it, which is the whole difficulty:
// a wrong one looks exactly like a right one. So there are two ways to check.

// The code as text, two characters per module so it comes out square in a
// terminal, with the four-module quiet zone included.
//
// THIS IS NOT A DEBUG CONVENIENCE, IT IS THE TEST. A phone will scan this
// straight off a terminal window, which means the encoder can be proved end to
// end — string in, URL back out of a real scanner — on a development machine
// with no console, no screen and no RomM server involved at all.
std::string toText(const Code& c, int quietZone = 4);

// A binary PBM, which every image tool reads and which needs no library to
// write. For checking against a known-good encoder, and for scanning at a size
// a terminal cannot show.
bool writePbm(const Code& c, const std::string& path, int scale = 8,
              int quietZone = 4);

}  // namespace qr
