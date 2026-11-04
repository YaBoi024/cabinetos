// The ground under every screen: black under a game, otherwise the
// backdrop, lit on the browsing screens by what focus is on and
// cross-faded (#398, restructure batch d).

#include "app.h"


using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

void App::drawGround(bool gameUp, float dt) {
    if (gameUp) {
        // BLACK behind a running game, not the menu's backdrop. The
        // reference implementation's player clears to black, and it is
        // right: a gradient around a game picture is decoration competing
        // with the thing you are looking at, and the letterbox glow is
        // bias lighting, which means light against black. On a purple
        // backdrop it is neither.
        //
        // EXCEPT WHEN THE GAME IS NOT OURS TO DRAW. On the composited path
        // the emulator owns its own window and gamescope puts our frame on
        // top, so this fill would black the game out. Drawing nothing is
        // what lets it through — the scrim below still dims it, because a
        // translucent black rectangle composites exactly as it should.
        if (!overlayTest && !standalonePaused && !ps2Window)
            renderer.draw(ui::Rect{0, 0, ui::kCanvasWidth, ui::kCanvasHeight, 0,
                                   ui::Color::black(1.0f)});
    } else {
        renderer.drawBackdrop(look::backdrop());

    // ---- THE BACKDROP FOLLOWS FOCUS, ON EVERY BROWSING SCREEN ----------
    //
    // The design system's kHomeBackdrop* block has the reasoning and the
    // three numbers to argue about. What happens here is the mechanism:
    //
    //  1. Ask what focus is pointing at. On Home that is a card; on the
    //     Library it is the focused tile; in a platform's grid it is the
    //     focused game. The TOP BAR is not a game and neither is a switcher
    //     pill, so walking onto one changes nothing — the room stays lit by
    //     whatever you were on rather than blanking on the way past.
    //  2. Do nothing until that answer has held still for kHomeBackdropDelay.
    //     A controller crosses a shelf far faster than the 180ms focus
    //     tempo and a backdrop chasing it frame for frame is a strobe.
    //  3. Cross-fade, old under new. One texture replacing another with a
    //     cut is the failure this is built to avoid.
    //
    // IT RUNS OUTSIDE ANY ONE SCREEN, and that is the second reason it is
    // here rather than in Home. MMagTech, on the panel: *"after this
    // transition to the library it isnt smooth."* It was a cut in two ways
    // at once — the Library's content appeared between one frame and the
    // next, AND the lit room vanished with Home, so the whole screen
    // changed colour instantly. The content now fades (Renderer::
    // setContentAlpha) and the room does not go anywhere: it simply
    // re-lights from the tile you land on. The ground staying put while the
    // content changes is most of what makes a move read as a move.
    //
    // THE LARGE COVER, NOT THE SHELF'S THUMBNAIL. Filling 1920x1080 from a
    // 162x216 PNG is a 12x upscale of an image that was already a
    // thumbnail; the 810x1080 original costs about 1.5x the bytes and is
    // the only reason this looks like anything. romm_types.h has the
    // measurement.
    if (here() != Screen::Detail) {
        // The launch screen is excluded because it already IS this idea,
        // at full strength: a game's cover filled, blurred and scrimmed
        // across the whole screen. Two of them would fight.
        std::string want = backdropWant;
        if (here() == Screen::Home) {
            const Card* lit = cardAt(focusRow, focusSlot);
            if (lit) want = lit->coverLarge.empty() ? lit->cover : lit->coverLarge;
        } else if (here() == Screen::Library) {
            const std::string tile = libraryScreen.focusedCover();
            if (!tile.empty()) want = tile;
        } else if (here() == Screen::Grid) {
            const int ci = gridScreen.focusedCard();
            if (ci >= 0 && ci < static_cast<int>(cards.size()))
                want = cards[ci].coverLarge.empty() ? cards[ci].cover
                                                    : cards[ci].coverLarge;
        } else if (here() == Screen::Search) {
            // The room follows the results here too. With nothing found,
            // or nothing typed yet, it is lit by the last game art you
            // were browsing, NOT by whatever the previous screen left: that
            // was the plain purple when you came from Settings, so Search
            // was sometimes plain and sometimes blurred. It is always
            // blurred now.
            const int ci = searchScreen.focusedCard();
            if (ci >= 0 && ci < static_cast<int>(cards.size()))
                want = cards[ci].coverLarge.empty() ? cards[ci].cover
                                                    : cards[ci].coverLarge;
            else if (!lastLitArt.empty())
                want = lastLitArt;
        } else if (here() == Screen::AddAccount || here() == Screen::TailscaleSignIn ||
                   here() == Screen::Report || here() == Screen::WhatsNew ||
                   here() == Screen::HowTo) {
            // **A TEXT SCREEN GETS THE PLAIN GRADIENT.** MMagTech,
            // 2026-09-22: *"i preferred the purple background that went
            // with the first run setup better. The current background
            // isn't ideal for a text heavy screen."*
            //
            // Every other screen here is a wall of covers, and a colour
            // field lifted from the focused one sits under them. This one
            // is prose, an address and a code — things that have to be
            // READ, and read off a television — and a blurred game cover
            // behind them is contrast nobody chose, different on every
            // visit depending on what happened to be lit on Home.
            //
            // Clearing it rather than excluding this screen from the block
            // above: leaving `want` alone would keep whatever Home was lit
            // by, which is exactly the bleed-through being complained
            // about. It fades out on the same cross-fade everything else
            // uses, so it goes as deliberately as it arrives.
            //
            // It also puts this screen where first run already is, which
            // is the point — the two do the same job minutes apart.
            want.clear();
        } else if (here() == Screen::Settings) {
            // SETTINGS IS A TEXT SCREEN TOO, and gets the plain gradient
            // for the reason above: rows to read, not covers to browse.
            want.clear();
        }
        // Which screen asked, so a change of screen can be told apart
        // from focus moving within one.
        if (want == backdropWant) backdropScreen = here();
        if (!want.empty() && (here() == Screen::Home || here() == Screen::Library ||
                              here() == Screen::Grid || here() == Screen::Search))
            lastLitArt = want;
        if (want != backdropWant) {
            backdropWant = want;
            backdropSettle = 0.0f;
            // A NEW SCREEN CHANGES THE ROOM AT ONCE, AND AT THE SCREEN'S
            // OWN SPEED. The wait exists for focus running along a shelf;
            // applied to a screen change it left the old art showing for a
            // moment after the new screen had arrived. MMagTech,
            // 2026-09-24, Search to Settings: "you can see the colors
            // before it goes all purple so it looks like a visual bug".
            backdropForScreen = (here() != backdropScreen);
            if (backdropForScreen) backdropSettle = backdropDelay;
        } else if (backdropWant != backdropKey &&
                   backdropMix.elapsed >= backdropMix.duration) {
            // AND NEVER INTERRUPT A FADE THAT IS STILL RUNNING. Committing
            // mid-flight drops the outgoing layer at whatever alpha it had
            // reached — at a third of the way through, a picture showing at
            // 70% vanishes in one frame, which is a pop in the middle of
            // the very transition that exists to avoid one. Two layers can
            // cross-fade; three cannot, so the third waits its turn.
            backdropSettle += dt;
            // A CAPTURE HAS NO WALL CLOCK TO WAIT ON. `--frames N` on the
            // A9's Radeon goes by in well under the 220ms this delay
            // wants — the same trap `--launch-after` fell into, recorded in
            // docs/lessons/testing.md — so a screenshot would show the screen
            // with no backdrop at all and look like the feature was not
            // built. Every other animation here already settles for a
            // capture; this one joins them.
            if (shotMode) backdropSettle = backdropDelay;
            if (backdropSettle >= backdropDelay) {
                backdropPrevKey = backdropKey;
                backdropKey = backdropWant;
                backdropMix.from = 0.0f;
                backdropMix.to = 0.0f;
                backdropMix.elapsed = 0.0f;
                // A new screen's background arrives with the screen, at its
                // speed, so the two read as one change.
                backdropMix.retarget(1.0f, backdropForScreen ? kTabArrive
                                                             : backdropFade);
                // And arrive, rather than being caught half way in.
                if (shotMode) backdropMix.elapsed = backdropMix.duration;
            }
        }

        // One cover, cropped to the screen rather than stretched across it.
        // A 3:4 cover mapped 0..1 over 16:9 is a smear that is no longer
        // the art's colours, which is the only thing it is here to be.
        auto drawLit = [&](const std::string& key, float alpha) {
            if (key.empty() || alpha <= 0.001f) return;
            const ui::Image& art = images.get(key);
            if (!art.ready) return;
            const float boxAspect = ui::kCanvasWidth / ui::kCanvasHeight;
            const float imgAspect = art.aspect();
            float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
            if (imgAspect < boxAspect) {
                const float span = imgAspect / boxAspect;
                v0 = (1.0f - span) * 0.5f;
                v1 = v0 + span;
            } else {
                const float span = boxAspect / imgAspect;
                u0 = (1.0f - span) * 0.5f;
                u1 = u0 + span;
            }
            renderer.drawTextured(
                0, 0, ui::kCanvasWidth, ui::kCanvasHeight, art.texture, u0, v0, u1, v1,
                ui::Color{1, 1, 1, alpha * art.fade * look::artFill(backdropFill)}, false,
                blurToTexels(static_cast<float>(art.width) * (u1 - u0),
                             backdropTexels));
        };
        const float mix = backdropMix.value();
        drawLit(backdropPrevKey, 1.0f - mix);
        drawLit(backdropKey, mix);
        // The scrim goes on whether or not there is art, so the content
        // sits on the same ground either way and a game with no cover is
        // not a brighter screen than one with.
        if (!backdropKey.empty())
            renderer.draw(ui::Rect{0, 0, ui::kCanvasWidth, ui::kCanvasHeight, 0,
                                   ui::Color::black(look::artScrim(backdropScrim))});
    }
    }
}
