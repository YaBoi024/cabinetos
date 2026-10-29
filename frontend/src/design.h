// The design system, as numbers.
//
// Every value here is quoted from docs/PROJECT.md, "The design system". If one
// of them changes there, it changes here, and nowhere else. It lives in a
// header rather than in main.cpp because the screens are separate files now and
// two copies of a focus scale is how a shelf and a grid end up disagreeing
// about what focus looks like.
//
// Three files are built on these numbers and hold the rest of the design
// system's code: motion.h (the animation primitive), card.h (a game's card)
// and panels.h (the in-game overlay's numbers and the small panels' recipe),
// moved out of this file in the restructure (#398).
//
// The numbers that are NOT arbitrary, and that a tidy-up will otherwise undo:
//
//   THE FOCUS SCALE SHRINKS AS THE ELEMENT GROWS — 1.10 for a cover, 1.06 for a
//   pill, 1.03 for a full-width row. A row growing a tenth collides with its
//   neighbours; a pill growing a thirtieth does not read at all.
//
//   RESERVED HEADROOM IS A LAYOUT OBLIGATION. A shelf carries 24pt of vertical
//   padding for no reason except that its cards grow when focused. Every
//   container holding focusable elements has to budget for their focused size.
//
//   180 ms IS THE FOCUS TEMPO and nothing about focus is slower. A controller
//   crosses a shelf faster than that and the animations must not queue up
//   behind the person driving them.

#pragma once

#include <algorithm>
#include <cmath>

#include "ui.h"

namespace design {

// --- The canvas -------------------------------------------------------------

// ONE LEFT MARGIN FOR THE WHOLE PRODUCT — 2026-09-21.
//
// It used to be two: Home inset by 60 and everything pushed by 80, both taken
// from the reference implementation's own `contentInset`, on the reasoning that
// "Home is a wall of artwork and the pushed screens are content to be read".
//
// That reasoning came from tvOS, where nothing spans the screens. It stopped
// holding the moment the top bar became permanent chrome sitting across all of
// them: the bar draws at 60 on every screen, so on the Library the bar, the
// switcher and the tiles each began at a different x and the eye had three
// left edges to choose from. MMagTech: *"the library and collection text dont
// seem to match home now either."* The type was never the problem — the
// switcher is Title 3, the same as Home's shelf headings — the margin was.
//
// A PILL IS PULLED BACK BY ITS OWN PADDING so that its LABEL lands on the
// margin rather than its capsule. Aligning the background of a chip puts its
// text 14 points inside everything else on the screen, which is the same fault
// one level down. See LibraryScreen::drawGlass and GridScreen::drawGlass.
constexpr float kContentInset = 60.0f;
constexpr float kLibraryInset = kContentInset;

// --- Focus ------------------------------------------------------------------

constexpr float kFocusScale = 1.10f;       // artwork
constexpr float kPillFocusScale = 1.06f;   // a text control
constexpr float kRowFocusScale = 1.03f;    // a full-width row
constexpr float kPressScale = 1.02f;
constexpr float kFocusRimWidth = 4.0f;
constexpr float kFocusShadowBlur = 26.0f;
constexpr float kFocusShadowOffsetY = 14.0f;
constexpr float kFocusDuration = 0.180f;

// HOLDING A DIRECTION REPEATS, AND ACCELERATES — new 2026-09-21.
//
// MMagTech: *"holding down should probably do the same as well."* The honest
// answer is better than the question: holding a direction on the pad did
// NOTHING. SDL sends one button-down per press and there was no repeat anywhere
// in this frontend, so a person holding Down on a 141-game platform moved one
// row and then waited. That is not a missing shortcut, it is a missing
// baseline — every console UI has had this since the d-pad was invented.
//
// It repeats rather than jumping by letter, and the distinction matters: the
// triggers already mean "jump to the next letter" and giving a held direction
// the same meaning after some interval makes the first press feel broken while
// you wait to find out which one you are getting. Same meaning, faster.
//
// THE FIRST REPEAT WAITS LONGER THAN THE REST. Without that pause, a normal
// single press on a pad whose contacts are slow reads as two, and focus
// overshoots by one every time — which is worse than no repeat at all.
// HOW LONG WORK HAS TO LAST BEFORE THE CONSOLE SAYS IT IS WORKING.
//
// A progress panel for something that finishes in eighty milliseconds is not
// information, it is a flash. Pressing Play on a game already on the disk still
// checks the firmware and looks for the file, and that was enough to paint one.
//
// 400 ms is the usual threshold for this and it is the right order: below it a
// person reads the result as instant, above it silence reads as a hang.
constexpr float kProgressDelay = 0.400f;

// THE CURTAIN, which is what makes a game arrive rather than appear — 2026-09-21.
//
// MMagTech: *"when a game launches the transition to it is very abrupt any way
// to smooth it."* There was nothing to smooth, and the reason is worth stating:
// `Core::loadGame` BLOCKS THE FRAME THREAD. The sequence was a UI frame, a UI
// frame, several hundred milliseconds of nothing at all, and then a game frame.
// No animation can live in a gap where no frames are drawn.
//
// So the curtain closes FIRST and the load happens behind it. Down over 260 ms
// while the interface is still drawing and still animating, then the blocking
// work, then up over 420 ms onto the game. The slower lift is deliberate: going
// dark is the console acknowledging a press and should be brisk; coming back is
// the game arriving and is the part worth watching.
//
// It is ease-in-out both ways, which the design system reserves for "a change of
// state the user asked for". Pressing Play is exactly that.
constexpr float kCurtainDown = 0.260f;
constexpr float kCurtainUp = 0.420f;
// RESTART AND POWER OFF FADE TO BLACK (#393). MMagTech, 2026-10-14: the
// picture cut off at once, "which can read as a crash"; 2026-10-18: "a fade
// with no sound". The same curtain, slower: this one is the console going
// away, and a second is long enough to read as meant and short enough not
// to feel like waiting. The machine is asked to go down when it is over.
constexpr float kPowerFade = 1.000f;

constexpr float kRepeatDelay = 0.420f;    // before the second move
constexpr float kRepeatStart = 0.120f;    // and between the ones after it
constexpr float kRepeatFast = 0.045f;     // once it has been held a while
constexpr float kRepeatRamp = 1.200f;     // seconds from Start to Fast
// A held trigger, between letters. 0.16 was "a little too fast ... the
// letters are zooming by" (MMagTech, 2026-10-10): each letter has to settle
// in the capsule long enough to be read before the next one arrives.
constexpr float kLetterRepeat = 0.280f;
constexpr float kPressDuration = 0.120f;

// SELECTION IS NOT FOCUS, and both are visible at once. A selected switcher
// pill is tinted white 35%; a focused one is tinted white 25% and scaled. A
// controller UI where focus can move away from the current selection has to
// show both or the person loses their place.
// THE CURSOR IS THE STRONGEST THING ON THE BAR, AND IT WAS THE PALEST.
//
// These were 0.35 selected against 0.25 focused — so the destination you are
// STANDING IN was drawn more solidly than the one the cursor is ON, which is
// backwards, and both were a white wash on a purple gradient. MMagTech,
// 2026-09-22: *"the pill that highlights them is a bit too light."*
//
// It is also the answer to a complaint recorded a day earlier and left open:
// *"0.35 against 0.25 is a difference you can measure and can barely see from
// a sofa."* Two tints ten points apart cannot say two different things.
//
// Focus is now unmistakable and selection is a quiet ground under it. The gap
// is 22 points rather than 10, and the hierarchy is the right way round. Both
// still get full-white text, so selection is never invisible — it is the pill
// that stops competing with the cursor.
constexpr float kSelectedTint = 0.30f;
constexpr float kFocusedTint = 0.55f;

// UNFOCUSED ARTWORK SITS BACK — new 2026-09-21.
//
// MMagTech, on a grid of 3DO covers: *"some of the images are coming off a bit
// harsh."* They were: every cover on a screen was drawn at full strength, so a
// wall of them all shouted equally and focus had to win on a rim and 10% of
// scale alone. Cannon Fodder's orange against the dark backdrop was doing more
// to catch the eye than the focused card was.
//
// So everything that is not focused is laid under a scrim and the focused one
// is not. It is the cheapest possible version of the idea — no desaturation, no
// second texture, one black rectangle at 22% — and it does two jobs at once:
// the wall calms down, and focus gains a signal that works from across a room
// where a 4pt rim does not.
//
// IT IS A SCRIM, NOT AN ALPHA. Drawing the art at 0.78 would blend it into the
// coloured panel underneath, which is a hue generated from the game's title —
// so a dimmed cover would come out tinted a different random colour per game.
// Black over the top dims honestly.
constexpr float kRestArtDim = 0.22f;

// A GREYED GAME: one this console cannot play until a controller is paired
// (Card::unavailable). A heavier scrim than rest, laid over the cover focused
// or not, so it still reads as unavailable with focus on it. A starting value,
// to be judged on the television.
constexpr float kUnavailableDim = 0.60f;

// THE KEPT MARK — new 2026-09-21.
//
// MMagTech asked for the grid to say which games are already on the machine.
// It is the one thing on that screen that was a product gap rather than
// polish: "Download and keep" is a deliberate storage act with a whole design
// behind it, the grid is where a person chooses, and the grid could not say.
//
// A DOT AND NOT A WORD. A label on every kept cover is a second caption on a
// screen that already has one per game, and it says the same thing twelve
// times. A mark is read once and learned; the launch screen still says
// "Remove download" in words, which is where the meaning is taught.
//
// NOT A GLYPH EITHER, and that is a fact about the machine rather than a
// preference: the UI font is Noto Sans, whose SemiBold face carries no check,
// no arrow and no play triangle — those come from the CJK fallback, which is
// a font that may not be installed on every image this ever runs on. A mark
// drawn from the renderer's own rounded rectangle cannot go missing.
//
// The accent, because this is the one place in the product that has wanted an
// accent colour: docs/PROJECT.md lists `accent` as "to be chosen" and cyan as
// the candidate. A dark ring under it so it reads on pale artwork as well as
// dark.
constexpr float kKeptMarkSize = 22.0f;
constexpr float kKeptMarkInset = 12.0f;
constexpr float kKeptMarkRing = 3.0f;

// THE FAVOURITE MARK beside a game's title (#267): the capitals' height times
// this, and how much it grows when focused (a small target grows more than a
// full-width row, or the growth cannot be seen).
constexpr float kFavMarkScale = 1.0f;
constexpr float kFavFocusScale = 1.12f;
// A HEART, picked over a star on the TV (MMagTech, 2026-10-06): a star reads
// as a rating.
constexpr ui::Rect::Shape kFavMarkShape = ui::Rect::Shape::Heart;
// The game page's cup once every achievement is unlocked (#74): a warm gold
// that reads as a reward on any cover's colours.
constexpr ui::Color kTrophyGold = ui::Color::rgb(0xF2C14E);

// --- Home -------------------------------------------------------------------

// THE TOP BAR IS ITS OWN STRIP — 2026-09-21.
//
// It used to be drawn over the hero's top edge, with a black scrim under it to
// keep the destinations legible against artwork. MMagTech: *"the top bar with
// library settings and search should not be on the hero and be its own
// separate thing at the top."* Right, and the capture says why: with the bar
// inside it, the hero stopped reading as a piece of artwork and started
// reading as a header with a picture in it.
//
// It cost the hero 96 points when it was first moved, and then the hero went
// altogether — so the room came back and more with it. Home fits in one screen
// and that is a rule with a name on it: MMagTech, 2026-09-19, "I should not
// have to scroll down to see the favorites."
//
// THE THREE NUMBERS, AND THEY WERE WRONG THE FIRST TIME. 60 / 60 / 16 put the
// bar at the safe inset with a tight gap under it, and MMagTech on the panel:
// *"you also left too much negative space above the top bar and the top
// entries seem to clash with recent row and text below it."* Both halves of
// that are the same mistake — the room was above the bar, where nothing uses
// it, instead of below it, where two rows of left-aligned text at the same
// size were sitting 70 points apart and reading as one crowded block.
//
// So the gap moved from above to below: 44 / 56 / 44.
//
// THE SAFE INSET IS 60 AND THE BAR NOW STARTS AT 44, which is a deliberate
// exception and the only one in the product. Nothing a person must read may
// sit outside the safe area, and the bar's text at 44 puts its ascenders at
// about 52. It was judged on the 65-inch rather than reasoned about; if a set
// with real overscan ever clips it, this is the number that did it, and
// `--home-bar` is how to find that out without a compiler.
constexpr float kBarTop = 44.0f;
constexpr float kBarHeight = 56.0f;
// 44 WAS STILL NOT ENOUGH, and the reason is proximity rather than taste.
// MMagTech, third look: *"something is still off visually on the top row with
// the text below it."* Measured off the capture: the gap from the bar down to
// "Recent" was 55 points, and the gap from "Recent" down to its own covers was
// 46. A heading sat almost exactly HALFWAY between the chrome above it and the
// artwork below it, so it belonged to neither — the eye read bar and heading as
// one block of text and the shelf as something unlabelled underneath.
//
// A heading must be nearer the thing it names than the thing it does not. 68
// above and 34 below is two to one, and the 34 is not free space at all: it is
// the headroom a focused cover grows up into.
constexpr float kBarGapBelow = 68.0f;

// The top of Home's content, under the bar. It was called kHeroTop while there
// was a hero; the hero is gone and the first shelf starts here.
constexpr float kContentTop = kBarTop + kBarHeight + kBarGapBelow;

// THE TWO MATERIAL THICKNESSES. These were spelled kHeroPillBlur and
// kHeroBandBlur and they outlived the hero, because they were never about it:
// they are the design system's own two materials, which docs/PROJECT.md gives
// as "ultraThinMaterial — heavy blur, very light tint" and "regularMaterial —
// heavy blur, mid tint". A pill is the thin one; a panel is the regular one.
constexpr float kThinMaterialBlur = 4.0f;
constexpr float kRegularMaterialBlur = 6.0f;

// The room left under the last shelf. It used to be spelled `kHeroTop`, which
// was the same number by accident and stopped being so the moment the bar
// pushed the content down — at which point Home started scrolling, because the
// scroll extent thought its bottom padding had grown by 96 points.
constexpr float kHomeBottomPad = 40.0f;

// HOME FITS IN ONE SCREEN. MMagTech, 2026-09-19, on the A9 at native 4K: "I
// should not have to scroll down to see the favorites. Everything on home
// should be visible in one 4K screen." The same objection he had to the
// reference implementation, which scrolls.
//
// THE COVERS GREW FROM 158x210 TO 240x320 — 2026-09-21. They were cut to
// 158x210 to fit two shelves under the hero: design.h recorded "about 430
// points had to come out of a 1080 canvas", and the covers paid 137 of them.
// Removing the hero gave 284 back and this is most of where they went.
//
// NOT the reference implementation's 260x347, which was the first attempt and
// did not survive its own focus scale — see kShelfHeadroom. A 347 cover needs
// 34 points of headroom above and below it, two shelves of that is 136, and
// the canvas does not have 136 spare. 320 does fit, headroom and all.
//
// Budget, and it deliberately does not fill the canvas:
//   44 bar top + 56 bar + 68 gap + 2 x (46 header + 0 + 34 + 320 + 34)
//   = 1036 of 1080, leaving 44 as overscan allowance at the foot.
//
// SIX COVERS FIT A ROW instead of ten, and that is the trade: 240 + 40 of
// spacing is 280 a card across 1800 points of content width. Fewer games in
// reach of the eye, each of them legible from a sofa.
//
// A television eats the edges, and this project has resized Home's artwork
// three times over exactly that — once while a simulator showed it fitting.
// Check with --safe-area on a panel before trusting any of it.
// THE BACKDROP FOLLOWS FOCUS — new 2026-09-21.
//
// The lesson taken from Steam's Big Art Mode, which shipped to beta on
// 2026-09-10: *the focused game owns the screen*. Valve drop the selected
// title's hero banner full-bleed behind everything, with no container at all,
// so moving along the row changes the whole picture.
//
// WHAT IS TAKEN IS THE IDEA, NOT THE LOOK. MMagTech, asked to be clear about
// it: *"these are just lessons i want this to still remain cabinet distinct."*
// So the purple gradient stays exactly as it is and the art goes OVER it at
// part strength — Home is a purple room lit by the game you are pointing at,
// rather than Valve's black screen filled with somebody's key art. The
// difference is visible in one glance and it is the whole point.
//
// Three numbers, and each is a judgement to be made on the television:
//
//   FILL is how much of the art shows through. Past about 0.6 the purple stops
//   being the ground and the screen belongs to the cover.
//   SCRIM is what keeps small white captions readable over a bright cover.
//   TEXELS is the blur, expressed as the width the art is averaged down to
//   rather than as a mip bias — see blurToTexels, and the bug that forced it.
//
// 0.45 / 0.25 / 16 was the first attempt and it was too polite: on the panel it
// read as a faint colour wash rather than as the game lighting the room. These
// are the second, chosen against F-Zero X on the 65-inch — and they are what
// `--home-backdrop` exists to argue with.
constexpr float kHomeBackdropFill = 0.65f;
constexpr float kHomeBackdropScrim = 0.22f;
// 28 TEXELS WAS STILL A PICTURE. MMagTech: *"i think the background could use
// more blur."* Right — at 28 across you could still make out the shapes in the
// cover, and a legible picture behind a wall of covers is a second wall of
// covers. At 14 it is a field of the art's colours with the form only implied,
// which is what a backdrop is for.
constexpr float kHomeBackdropTexels = 14.0f;
// It waits before it moves. A controller crosses a shelf faster than the focus
// tempo, and a backdrop that tried to keep up would be a strobe — so running
// along a row leaves the room alone, and it relights when somebody stops on
// something. THE DELAY IS THE FEATURE; without it this idea does not survive a
// real controller.
constexpr float kHomeBackdropDelay = 0.220f;
// 420 MS OF EASE-OUT WAS THE WRONG CURVE, not the wrong length — 2026-09-21.
// MMagTech: *"when transitioning between games that background change seems
// too abrupt."* Ease-out starts at its maximum velocity and settles, which is
// exactly right for focus arriving somewhere and exactly wrong for a whole
// screen changing colour: most of the change happened in the first 150ms and
// the eye read it as a cut with a tail on it.
//
// docs/PROJECT.md's motion table already had the answer — "600 ms, ease-in-out,
// a deliberate state change the user should watch" — and a room re-lighting is
// that and nothing else. Ease-in-out leaves slowly, which is the half that was
// missing.
constexpr float kHomeBackdropFade = 0.600f;

constexpr float kShelfCoverWidth = 240.0f;
constexpr float kShelfCoverHeight = 320.0f;   // 3:4
constexpr float kShelfSpacing = 40.0f;
// HEADROOM IS DERIVED, NOT TYPED — 2026-09-21.
//
// The design system's own rule, at the top of this file: "reserved headroom is
// a layout obligation... every container holding focusable elements has to
// budget for their focused size." It was a hardcoded 20, which was right for a
// 210-point cover — half of its 10% growth is 10.5 — and quietly wrong the
// moment the covers grew. At 347 the growth alone was 17.4 and the clearance
// left under a focused card was about two points. MMagTech, on the panel:
// *"i think the favorites sit too close to the cover above when its selected."*
//
// So it is computed from the cover and the focus scale. Change either and this
// follows, which is the only way it stays true.
// The gap between a shelf's heading and its covers, and it is ZERO on purpose.
// It was 12, which was invisible next to the 34 points of focus headroom that
// follow it and cost 24 of a canvas that had no 24 to spare. The heading is
// separated from its artwork by the headroom, which has to be there anyway.
constexpr float kShelfHeaderGap = 0.0f;
constexpr float kShelfBreathing = 18.0f;
constexpr float kShelfHeadroom =
    kShelfCoverHeight * (kFocusScale - 1.0f) * 0.5f + kShelfBreathing;
constexpr float kCoverRadius = 10.0f;

// --- Library ----------------------------------------------------------------
//
// A TILE GRID, NOT A LIST, and the reason is in docs/history/CABINET.md: a
// full-width row on a 1920pt canvas leaves a name at the far left and a count
// at the far right with a third of the screen empty between them. A grid also gives the
// focus engine a real two-dimensional field to move in.
constexpr float kTileMinWidth = 380.0f;   // adaptive: as many columns as fit
constexpr float kTileHeight = 200.0f;
constexpr float kTileSpacing = 36.0f;     // both axes
constexpr float kTileRadius = 18.0f;
// A GROUP'S HEADING in the Collections tab ("From MMagTech", #352): other
// people's shared collections, under the owner's name. A heading must be
// nearer the thing it names than the thing it does not (kBarGapBelow): 36 + 24
// above it, and below it only the tiles' focus growth (200 x 0.10 / 2) and the
// shelves' breathing room.
constexpr float kTileGroupGapAbove = 24.0f;
constexpr float kTileGroupGapBelow = 10.0f + 18.0f;
constexpr float kTilePadding = 24.0f;     // inside a tile, around its label
// The thumbnail on the right of a tile. Deliberately shorter than the tile:
// a cover spanning the full height left the name too little room to be read,
// and a tile whose name cannot be read is not doing its job.
constexpr float kTileArtHeight = 132.0f;
constexpr float kTileArtGap = 20.0f;
// The switcher's pills: Platforms / Collections.
constexpr float kPillPadX = 14.0f;
constexpr float kPillPadY = 8.0f;
constexpr float kPillGap = 16.0f;
// THE PUSHED SCREENS START WHERE HOME'S CONTENT DOES — 2026-09-21.
//
// It was 40, which put the Library's switcher pills roughly where Home's top
// bar is. That was fine while the bar belonged to Home and vanished on the way
// out; it stopped being fine when the bar became the chrome it is supposed to
// be. MMagTech: *"when you switch from home to library the bar text at the top
// should be the same."*
//
// So the bar stays put across the move and everything under it starts at the
// same y on both screens. Two things now do not move at all between Home and
// the Library — the bar and the lit backdrop — and a transition where the
// frame stays still and only the contents change is the one that reads as a
// move rather than as a new screen being dealt.
constexpr float kSwitcherTop = kContentTop;

// The room left at the FOOT of a pushed screen. It used to be spelled
// kSwitcherTop, which was the same number by accident until the top of the
// screen grew by 128 points and took the bottom padding with it.
constexpr float kScreenBottomPad = 40.0f;

// WHERE A SCROLLING SCREEN'S WINDOW BEGINS. Everything a pushed screen scrolls
// is clipped to start just under the top bar, so content rides up and stops
// rather than passing through the chrome. A few points of gap so a cover's
// focus rim does not touch the bar's descenders on its way past.
constexpr float kScrollClipTop = kBarTop + kBarHeight + 8.0f;

// AND IT FADES INTO THAT EDGE RATHER THAN BEING CUT AT IT — 2026-09-21.
//
// A scissor on its own is a guillotine: a cover scrolling up stops existing
// along a straight horizontal line, mid-artwork, eight points under the bar's
// descenders. It reads as the picture being sliced rather than as the list
// moving, and it leaves the bar's text floating on whatever busy edge happens
// to be passing underneath it.
//
// So a vignette is drawn over the top of the screen, from the canvas edge down
// past the clip line: black at the top, nothing at the bottom. It gives the bar
// a ground that is the same on every screen, and it turns the cut into a fade.
// It is drawn only on the screens that SCROLL — Home does not, its content
// starts below the bar, and a band of shade there would darken the lit backdrop
// for nothing.
constexpr float kScrollFadeHeight = kScrollClipTop + 56.0f;
constexpr float kScrollFadeAlpha = 0.72f;
constexpr float kSwitcherGapBelow = 28.0f;

// --- A grid of games --------------------------------------------------------
//
// `TenFoot` declares a 240 minimum and the grid that uses it hardcodes 260.
// Take 260: it is the value that shipped and was looked at.
// 260 DID NOT FIT TWO ROWS, and that was the real complaint — 2026-09-21.
// MMagTech: *"when viewing games on a platform as you scroll the top row gets
// cut off."* It is arithmetic rather than polish. Content starts at 268 (the
// bar, the title and its gap) and the foot keeps 40, so 772 points of height
// are usable. A row was 481 of them: a 347 cover, a 10 gap, EIGHTY of reserved
// two-line caption and 44 of row spacing. 772 / 481 is 1.6 rows, so no scroll
// position could ever show two whole ones and a sliced row was unavoidable.
//
// 210 gives seven columns of 216, a 288 cover, and — with the captions gone —
// a row pitch of 332. 772 / 332 is 2.3: two whole rows and an honest peek at
// the third, which is what says "there is more" without slicing anything.
constexpr float kGridCoverMin = 210.0f;
constexpr float kGridColumnSpacing = 48.0f;
constexpr float kGridRowSpacing = 44.0f;
constexpr float kGridCaptionGap = 10.0f;
constexpr float kGridCoverRadius = 12.0f;
// NO CAPTIONS UNDER A GRID'S COVERS — changed 2026-09-21.
//
// They were two lines with the space reserved either way, so rows stayed
// aligned whether a title wrapped or not. That was the right fix for the
// problem it was solving and it cost 80 points of every row, which is what
// stopped two rows fitting on the screen at any sensible cover size.
//
// The replacement is the one Home already uses and has used since the hero
// came off it: THE FOCUSED CARD'S TITLE RIDES IN THE SCREEN'S HEADING, where
// it costs no vertical space at all and is the only title anybody is reading.
// Home's shelf header does exactly this. Two screens, one idea.
//
// What is lost is real and worth writing down: a platform grid is where you
// look for something you do NOT recognise by its art, and forty covers with no
// names is a harder screen than forty with them. The focused title answers it
// one at a time. If that turns out not to be enough, captions come back as
// code (git has the two-line caption that was here until #492), and the covers
// shrink to about 183 to pay for it.
// The letter index down the right of a grid: the width of its glass column,
// room for the bar's focus capsule round one Title 3 letter and a margin.
constexpr float kLetterIndexWidth = 96.0f;
constexpr float kScreenChipPadY = 10.0f;

// --- The launch screen ------------------------------------------------------

constexpr float kDetailCoverWidth = 340.0f;
constexpr float kDetailCoverHeight = 460.0f;
constexpr float kDetailRadius = 16.0f;
// A row, which is the settings shape and what the launch screen's actions are.
// Its height is COMPUTED from the type and this padding rather than hardcoded,
// the same way the hero's band is, so it grows with the ramp instead of
// clipping it.
constexpr float kRowRadius = 16.0f;
constexpr float kRowPadX = 32.0f;
constexpr float kRowPadY = 22.0f;
constexpr float kDetailRowGap = 16.0f;
// The launch screen: the space between the left column and the cover on the
// right; Continue from's gap between two cards (three split the column, 16:9)
// and its space under the rows; and the highest the page may start. First
// guesses, to be judged on the television.
constexpr float kDetailCoverGap = 80.0f;
constexpr float kDetailCoverMaxHeight = 760.0f;   // grown to the column, at most
// THE SMALL BOOK ON THE COVER (#364), when the game has a manual: a disc of
// frosted glass, darkened and rimmed, in the cover's foot corner, with a white
// book on it. A plain dark disc vanished into dark art (MMagTech on the TV,
// Ballz, 2026-10-12). Starting values, to be judged on the television.
constexpr float kManualMarkDisc = 56.0f;
constexpr float kManualMarkInset = 16.0f;
constexpr float kManualMarkScale = 0.58f;   // the book's width, of the disc's
constexpr float kManualMarkDiscAlpha = 0.30f;
constexpr float kManualMarkRimAlpha = 0.22f;
constexpr float kManualMarkAlpha = 1.0f;
// The launch screen fades in whole, over this long, once everything behind it
// has loaded, or once this long has passed without it all.
constexpr float kDetailsFade = 0.40f;
constexpr float kDetailsWait = 0.80f;
// Not before this, so the old screen's copy has mostly dissolved first (the
// top-bar switch's kTabContentDelay), and rising this far as it fades in (its
// kTabRise).
constexpr float kDetailMinWait = 0.20f;
constexpr float kDetailRise = 16.0f;
constexpr float kStateCardGap = 28.0f;
constexpr float kStateShelfGap = 40.0f;
constexpr float kStateBlockMinTop = 90.0f;
// The reference implementation's settings column. A row stretched to the full
// 1920 leaves a label at one end and a value at the other with a third of the
// screen empty between them.
constexpr float kRowColumnMaxWidth = 1100.0f;

// SETTINGS IS A SIDE LIST — 2026-09-24, docs/PROJECT.md open question 31.
// Categories down the left, the chosen one's rows to the right. The list is
// wide enough for "Display and Sound" at Title 3 with its padding and no
// wider; the rows keep the settings column's 1100.
//
// Starting values, chosen to be looked at on the television rather than argued
// from a capture.
constexpr float kSettingsListWidth = 420.0f;
constexpr float kSettingsListPadY = 16.0f;
constexpr float kSettingsListGap = 8.0f;
constexpr float kSettingsPaneGap = 60.0f;
// Between a row's title and its detail line.
constexpr float kSettingsDetailGap = 4.0f;
// A Disabled row: built, nothing to do right now. Legible, clearly not live.
constexpr float kSettingsUnbuiltAlpha = 0.38f;
// The backdrop under a full-screen cover: the artwork itself, filled and
// blurred, because the leftovers should be the art's own colours rather than
// letterbox bars.
// The hero's own value, and for the same reason: one level coarser averages a
// cover down to a single muddy colour, which is not "the art's own colours",
// it is a brown rectangle where the artwork used to be.
constexpr float kBackdropBlur = 5.0f;
constexpr float kScrimOverlay = 0.55f;

// BLUR IS A TEXEL COUNT, NOT A MIP BIAS, wherever the source size can vary.
//
// `drawTextured`'s lodBias picks a mip level, and a level is a fraction of the
// SOURCE. That was safe while every cover on the console was RomM's 162x216
// thumbnail; it stopped being safe the moment the backdrop started asking for
// the 810x1080 original, because the same bias of 5 means 5x6 texels on one
// and 25x33 on the other — mud in one case and a legible picture in the other,
// from one unchanged number.
//
// So say what the blur IS: average this art down to about `texels` across,
// whatever arrived. A game the server has no large cover for then blurs to the
// same amount as one that does, which is the only way the screen stays
// consistent across a library that is not consistent.
inline float blurToTexels(float sourceWidth, float texels) {
    return std::log2(std::max(1.0f, sourceWidth / std::max(1.0f, texels)));
}

}  // namespace design
