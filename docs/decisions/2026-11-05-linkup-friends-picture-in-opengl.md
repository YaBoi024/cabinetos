# A LinkUp friend's picture is drawn with OpenGL, not Vulkan

- **Decided:** The friend's picture is drawn with OpenGL, for the friend's picture only. Nothing here touches the rule that emulators use Vulkan. The choice is measured again when GStreamer's Vulkan library can draw in X11, which a line in `ci/base-watch.txt` reports, or when a base update moves gamescope, Mesa or SDL by a major version.
- **Date:** 2026-11-05
- **Why:** Fedora builds GStreamer's Vulkan library for Wayland only, and the session's programs are X11 clients, so `vulkansink` has no window to open, and through gamescope's Wayland socket it showed only black. The route that works with no patch is SDL's Vulkan renderer, and to be as smooth as OpenGL it costs 20 to 40 ms more delay, to save a fraction of a core on a friend's console that is not running a game.
- **Numbers:** #654, "LinkUp: play a game with a friend on another CabinetOS console over the internet", in its comment "The friend's picture: OpenGL, measured against Vulkan"; PR #666, "LinkUp: the console's side, step 3 of #654".
- **Replaces:** none.
