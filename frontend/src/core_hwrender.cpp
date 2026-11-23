// Hardware-rendered cores: the render target in the UI's GLES context, the
// desktop OpenGL context a core that asks for one gets beside it (#219), the
// hand-over to vkhost for a Vulkan core, the SET_HW_RENDER family of
// environment calls (environmentHWRender) and where the picture sits in the
// texture. Moved out of core.cpp in the restructure (#398). When the target is
// built and torn down, around retro_load_game and retro_unload_game, stays in
// Core::loadGame and Core::unloadGame (core.cpp), where the order is visible.

#include "core.h"
#include "core_state.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <optional>
#include <string>

#include "libretro.h"
#include "libretro_vulkan.h"
#include "vkhost.h"

// The vendored libretro.h stops at environment call 72 and this one is 73.
//
// Carried across by value rather than by updating the header: libretro.h is
// 3,937 lines that every part of this host reads, and replacing it wholesale
// to gain one constant is a change with a blast radius out of all proportion
// to the gain. The guard means a later wholesale update simply wins, rather
// than colliding with this.
//
// A core asks this to find out which version of the negotiation interface the
// frontend understands, BEFORE it declares one. Answering matters: unanswered,
// a core is entitled to assume the newest it knows.
#ifndef RETRO_ENVIRONMENT_GET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_SUPPORT
#define RETRO_ENVIRONMENT_GET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_SUPPORT \
    (73 | RETRO_ENVIRONMENT_EXPERIMENTAL)
#endif

namespace cab {
namespace corestate {

// The other control, and it is the same idea as gAnswerOptions: turn one thing
// off so the difference can be measured rather than argued about.
//
// With this set, SET_HW_RENDER is refused no matter what the core asked for.
// A core that draws with GL then falls back to whatever it does with no
// picture — Dolphin selects its Null video backend and keeps emulating — which
// separates "this core cannot get a picture out of this host" from "this core
// cannot run in this host at all". Those two look identical from the outside
// and they are a different amount of work.
bool gRefuseHWRender = false;

// --- Hardware-rendered cores -------------------------------------------------
//
// See core.h for what this is and why the core gets a framebuffer rather than a
// context. What is here is the mechanism.
//
// ONE CONTEXT, SHARED WITH THE UI. The core draws into a texture this file
// owns, in the frontend's own GLES 3.0 context, and the player draws that
// texture. For a GLES core there is no second context, no second thread, and
// no readback in the steady path; a desktop GL core gets a second context
// (below), still with no thread and no readback.
//
// WHICH CONTEXTS ARE ACCEPTED, AND WHY REFUSING IS STILL THE RIGHT ANSWER
// SOMETIMES. GLES, because that is what this context is; Vulkan through
// vkhost; desktop GL in a second context (below) where this machine can make
// one. Anything else is refused with the version named, rather than accepted
// and then failed inside the core, which reads as a broken game rather than
// as a frontend that cannot do something.
//
// DESKTOP GL IS A SECOND CONTEXT, SHARED WITH THE UI'S (#219). melonDS's
// renderer, the only way the DS draws above its own 256x192, is desktop
// OpenGL 3.1 and nothing else; it has no GLES or Vulkan path. A GLES context
// cannot run it, so a core that asks for desktop GL gets a context of its own
// in the same share group as the UI's: textures are shared, so the core draws
// into the same colour texture the player samples and nothing is copied. A
// framebuffer object is never shared between contexts, so the core's is made
// in its context (gHWDesktopFBO) and gHWFBO, in the UI's, wraps the same
// texture for the readers (core_picture.cpp). The core's context is current
// only inside a call into the core (CoreContext); everywhere else the UI's
// is, exactly as before, and the core's state can no longer disturb the UI's.
// Mesa allows a desktop context to share with a GLES one through both GLX
// and EGL (it checks only the reset strategy and no-error flags). Which
// cores ask for desktop GL at all: melonDS alone. The others ask what the
// console prefers, and the answer (GET_PREFERRED_HW_RENDER below) stays
// Vulkan or GLES.
//
// THE VERSION IS READ, NOT ASSUMED. SDL is asked for GLES 3.0 and the driver
// is free to hand back more — on the test VM it hands back 3.2. Refusing a
// core that wants 3.1 because of what was ASKED for would be turning down
// something this machine can actually do, so the version comes from
// glGetIntegerv on the context that exists.
retro_hw_render_callback gHW{};
bool gHWWanted = false;        // the core asked, and it was accepted
// WHICH API, because from here on almost nothing is shared. A GLES core draws
// into an FBO this file owns, on this thread; a Vulkan core draws wherever it
// likes on whatever thread it likes and hands over a finished image. See
// vkhost.h for why that difference is the whole point.
bool gHWVulkan = false;
bool gHWContextLive = false;   // context_reset has run and context_destroy has not
GLuint gHWFBO = 0, gHWColor = 0, gHWDepth = 0;
unsigned gHWTargetW = 0, gHWTargetH = 0;   // what the target is sized to
bool gHWFrame = false;         // the last frame came from the core's GL
std::string gHWContextName;
// The last answer to GET_HW_RENDER_INTERFACE that was logged, 0 for none.
// Azahar asks on every frame, which was 44 lines a second in the journal;
// logged once per answer instead, and cleared by each SET_HW_RENDER so every
// game's first ask is still in the log.
int gHWInterfaceLogged = 0;

// The desktop GL core's own context and framebuffer, and the window and UI
// context to go back to. Set for as long as such a core has a game loaded.
bool gHWDesktop = false;
SDL_GLContext gHWDesktopContext = nullptr;
SDL_GLContext gUIContext = nullptr;
SDL_Window* gHWWindow = nullptr;
GLuint gHWDesktopFBO = 0;

uintptr_t currentFramebuffer(void) { return gHWDesktop ? gHWDesktopFBO : gHWFBO; }

retro_proc_address_t procAddress(const char* sym) {
    // SDL's, not EGL's: SDL created this context, and on a driver where the
    // two disagree the one that made the context is the one to ask.
    return reinterpret_cast<retro_proc_address_t>(SDL_GL_GetProcAddress(sym));
}

std::string contextName(const retro_hw_render_callback& cb) {
    char buf[64];
    switch (cb.context_type) {
        case RETRO_HW_CONTEXT_OPENGLES2: return "OpenGL ES 2.0";
        case RETRO_HW_CONTEXT_OPENGLES3: return "OpenGL ES 3.0";
        case RETRO_HW_CONTEXT_OPENGLES_VERSION:
            std::snprintf(buf, sizeof(buf), "OpenGL ES %u.%u", cb.version_major,
                          cb.version_minor);
            return buf;
        case RETRO_HW_CONTEXT_OPENGL:
            std::snprintf(buf, sizeof(buf), "OpenGL %u.%u compatibility",
                          cb.version_major, cb.version_minor);
            return buf;
        case RETRO_HW_CONTEXT_OPENGL_CORE:
            std::snprintf(buf, sizeof(buf), "OpenGL %u.%u core", cb.version_major,
                          cb.version_minor);
            return buf;
        case RETRO_HW_CONTEXT_VULKAN: return "Vulkan";
        case RETRO_HW_CONTEXT_DIRECT3D: return "Direct3D";
        case RETRO_HW_CONTEXT_NONE: return "none";
        default: return "an API this frontend has never heard of";
    }
}

// What the context actually is, asked once. Falls back to the 3.0 that was
// requested when there is no context to ask — which is the core-options audit,
// where cores are loaded with no window and nothing will be rendered anyway.
void contextVersion(unsigned& major, unsigned& minor) {
    static unsigned cachedMajor = 0, cachedMinor = 0;
    if (cachedMajor == 0) {
        if (SDL_GL_GetCurrentContext()) {
            GLint a = 0, b = 0;
            glGetIntegerv(GL_MAJOR_VERSION, &a);
            glGetIntegerv(GL_MINOR_VERSION, &b);
            cachedMajor = a > 0 ? static_cast<unsigned>(a) : 3;
            cachedMinor = a > 0 ? static_cast<unsigned>(b) : 0;
        } else {
            cachedMajor = 3;
            cachedMinor = 0;
        }
    }
    major = cachedMajor;
    minor = cachedMinor;
}

bool isDesktopGL(const retro_hw_render_callback& cb) {
    return cb.context_type == RETRO_HW_CONTEXT_OPENGL ||
           cb.context_type == RETRO_HW_CONTEXT_OPENGL_CORE;
}

namespace {

// The current context's version, from its version string ("4.6 (Core
// Profile) Mesa 25.1.4"): a 2.x context has no GL_MAJOR_VERSION to ask.
bool versionOf(unsigned& major, unsigned& minor) {
    const char* v = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    return v && std::sscanf(v, "%u.%u", &major, &minor) == 2;
}

// A desktop GL context in the UI context's share group, at least the version
// asked for, or null with the reason in `what`; on success `what` is the
// context's version string. The UI's context is current again when this
// returns, whatever happened: SDL makes a context current as it creates it.
SDL_GLContext makeDesktopContext(unsigned major, unsigned minor, bool coreProfile,
                                 std::string* what) {
    SDL_GLContext ui = SDL_GL_GetCurrentContext();
    SDL_Window* window = SDL_GL_GetCurrentWindow();
    if (!ui || !window) {
        *what = "there is no window";
        return nullptr;
    }
    // SDL's attributes are global, and every context made after this one is
    // made from them: put back exactly as found.
    const SDL_GLAttr attrs[] = {SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_MAJOR_VERSION,
                                SDL_GL_CONTEXT_MINOR_VERSION,
                                SDL_GL_SHARE_WITH_CURRENT_CONTEXT};
    int was[4] = {};
    for (int i = 0; i < 4; ++i) SDL_GL_GetAttribute(attrs[i], &was[i]);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        static_cast<int>(coreProfile ? SDL_GL_CONTEXT_PROFILE_CORE
                                                     : SDL_GL_CONTEXT_PROFILE_COMPATIBILITY));
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, static_cast<int>(major));
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, static_cast<int>(minor));
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
    SDL_GLContext ctx = SDL_GL_CreateContext(window);
    for (int i = 0; i < 4; ++i) SDL_GL_SetAttribute(attrs[i], was[i]);
    if (!ctx) {
        *what = SDL_GetError();
    } else {
        unsigned a = 0, b = 0;
        const char* v = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        *what = v ? v : "no version";
        if (!versionOf(a, b) || a < major || (a == major && b < minor)) {
            *what = "it came back as OpenGL " + *what;
            SDL_GL_MakeCurrent(window, ui);
            SDL_GL_DestroyContext(ctx);
            ctx = nullptr;
        }
    }
    SDL_GL_MakeCurrent(window, ui);
    return ctx;
}

}  // namespace

bool desktopGLServable() {
    // ASKED ONCE, BY MAKING ONE, because nothing short of that answers it: a
    // driver can offer desktop GL and still refuse to share with a GLES
    // context. 3.1 core is what melonDS asks for; a core that asks for more
    // is checked when it asks. Not answered, and asked again later, while
    // there is no window (the option check, the core-options report).
    static int answer = -1;
    if (answer >= 0) return answer == 1;
    if (!SDL_GL_GetCurrentContext()) return false;
    std::string what;
    SDL_GLContext ctx = makeDesktopContext(3, 1, /*coreProfile=*/true, &what);
    if (ctx) SDL_GL_DestroyContext(ctx);
    answer = ctx ? 1 : 0;
    std::fprintf(stderr, "[core] desktop OpenGL for cores: %s (%s)\n", ctx ? "yes" : "no",
                 what.c_str());
    return answer == 1;
}

bool canServe(const retro_hw_render_callback& cb) {
    unsigned haveMajor = 3, haveMinor = 0;
    contextVersion(haveMajor, haveMinor);
    auto atLeast = [&](unsigned major, unsigned minor) {
        return haveMajor > major || (haveMajor == major && haveMinor >= minor);
    };
    switch (cb.context_type) {
        case RETRO_HW_CONTEXT_OPENGLES2:
            return true;  // every GLES 3 context runs GLES 2 content
        case RETRO_HW_CONTEXT_OPENGLES3:
            return atLeast(3, 0);
        case RETRO_HW_CONTEXT_OPENGLES_VERSION:
            return atLeast(cb.version_major, cb.version_minor);
        case RETRO_HW_CONTEXT_VULKAN:
            // Served where the machine has a usable Vulkan device AND can
            // export what it draws, which is one question with one answer —
            // see gpu.h. On the test VM this is false and the core is refused
            // by name, exactly as before.
            return vk::available();
        case RETRO_HW_CONTEXT_OPENGL:
        case RETRO_HW_CONTEXT_OPENGL_CORE:
            // In a second context, where this machine can make one (above).
            return desktopGLServable();
        default:
            // Direct3D. Not something an environment callback should quietly
            // paper over.
            return false;
    }
}

// The desktop GL core's context for this game, made the first time it asks
// (SET_HW_RENDER, inside retro_load_game). A 0.0 version is libretro's "any":
// 2.1, SDL's own default.
bool openDesktopContext(const retro_hw_render_callback& cb, std::string* what) {
    unsigned major = cb.version_major, minor = cb.version_minor;
    if (major == 0) {
        major = 2;
        minor = 1;
    }
    gUIContext = SDL_GL_GetCurrentContext();
    gHWWindow = SDL_GL_GetCurrentWindow();
    gHWDesktopContext = makeDesktopContext(
        major, minor, cb.context_type == RETRO_HW_CONTEXT_OPENGL_CORE, what);
    gHWDesktop = gHWDesktopContext != nullptr;
    return gHWDesktop;
}

// After the core has unloaded and destroyHWTarget has run: the core's
// framebuffer goes with its context.
void closeDesktopContext() {
    if (gHWDesktopContext) {
        SDL_GL_MakeCurrent(gHWWindow, gUIContext);
        SDL_GL_DestroyContext(gHWDesktopContext);
    }
    gHWDesktopContext = nullptr;
    gHWDesktopFBO = 0;
    gHWDesktop = false;
}

CoreContext::CoreContext() : switched_(gHWDesktop && gHWDesktopContext) {
    if (switched_) SDL_GL_MakeCurrent(gHWWindow, gHWDesktopContext);
}

CoreContext::~CoreContext() {
    if (!switched_) return;
    // WHAT THE CORE DREW IS FINISHED BEFORE THE UI SAMPLES IT, which across
    // two contexts the driver does not promise by itself. A fence in the
    // core's context, waited on by the graphics chip in the UI's: nothing on
    // the processor waits. Flushed first, so the wait is never on a fence
    // that was never sent.
    GLsync drawn = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    glFlush();
    SDL_GL_MakeCurrent(gHWWindow, gUIContext);
    if (drawn) {
        glWaitSync(drawn, 0, GL_TIMEOUT_IGNORED);
        glDeleteSync(drawn);
    }
}

void destroyHWTarget() {
    // The core's framebuffer in its own context: deleted from the UI's, the
    // same number would delete one of the UI's.
    if (gHWDesktopFBO && gHWDesktopContext) {
        CoreContext in;
        glDeleteFramebuffers(1, &gHWDesktopFBO);
    }
    gHWDesktopFBO = 0;
    if (gHWFBO) glDeleteFramebuffers(1, &gHWFBO);
    if (gHWColor) glDeleteTextures(1, &gHWColor);
    if (gHWDepth) glDeleteRenderbuffers(1, &gHWDepth);
    gHWFBO = gHWColor = gHWDepth = 0;
    gHWTargetW = gHWTargetH = 0;
}

namespace {

// A framebuffer on gHWColor in the context that is current, with the depth
// and stencil the core asked for when `withDepth`. 0, with the reason on
// stderr, when it is incomplete.
GLuint framebufferOnTarget(unsigned w, unsigned h, bool withDepth) {
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gHWColor, 0);

    const bool depth = withDepth && gHW.depth;
    if (depth) {
        // Packed 24/8 when stencil is wanted as well, which is what the ABI
        // says to do. Stencil alone is defined as invalid and ignored.
        glGenRenderbuffers(1, &gHWDepth);
        glBindRenderbuffer(GL_RENDERBUFFER, gHWDepth);
        glRenderbufferStorage(GL_RENDERBUFFER,
                              gHW.stencil ? GL_DEPTH24_STENCIL8 : GL_DEPTH_COMPONENT24,
                              static_cast<GLsizei>(w), static_cast<GLsizei>(h));
        glFramebufferRenderbuffer(
            GL_FRAMEBUFFER,
            gHW.stencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT,
            GL_RENDERBUFFER, gHWDepth);
    }

    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::fprintf(stderr,
                     "[core] the %ux%u render target this core asked for is "
                     "incomplete (0x%x)\n", w, h, status);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &fbo);
        return 0;
    }

    // Cleared once, so the first presented frame is black rather than whatever
    // the driver left in the allocation.
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | (depth ? GL_DEPTH_BUFFER_BIT : 0u) |
            (depth && gHW.stencil ? GL_STENCIL_BUFFER_BIT : 0u));
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return fbo;
}

}  // namespace

// Allocates the target the core renders into, at the size it declared as its
// maximum. Returns false with the reason on stderr, which is a hard failure:
// a hardware core with no target draws nothing at all.
bool ensureHWTarget(unsigned w, unsigned h) {
    if (w == 0 || h == 0) return false;
    if (gHWFBO && (!gHWDesktop || gHWDesktopFBO) && w == gHWTargetW && h == gHWTargetH)
        return true;
    destroyHWTarget();

    glGenTextures(1, &gHWColor);
    glBindTexture(GL_TEXTURE_2D, gHWColor);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(w),
                 static_cast<GLsizei>(h), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    // Linear, where the software path uses nearest — and the difference is not
    // an inconsistency. A Game Boy's 160x144 pixels were each chosen by
    // somebody; a Dreamcast's output is already a rendering of a 3D scene at
    // whatever internal resolution the core was asked for, so smoothing it
    // between that size and the panel is what it wants.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // The core draws into gHWFBO, in this context, unless it is on desktop
    // GL: then it draws into one made in its own context with the depth, and
    // gHWFBO holds only the colour, for the readers.
    gHWFBO = framebufferOnTarget(w, h, /*withDepth=*/!gHWDesktop);
    if (gHWFBO && gHWDesktop) {
        CoreContext in;
        gHWDesktopFBO = framebufferOnTarget(w, h, /*withDepth=*/true);
    }
    if (!gHWFBO || (gHWDesktop && !gHWDesktopFBO)) {
        destroyHWTarget();
        return false;
    }

    gHWTargetW = w;
    gHWTargetH = h;
    std::fprintf(stderr, "[core] render target %ux%u, depth %s, stencil %s\n", w, h,
                 gHW.depth ? "yes" : "no", gHW.stencil ? "yes" : "no");
    return true;
}

// Puts the context back the way the UI expects to find it.
//
// A core rendering a 3D machine sets depth testing, culling, scissoring, a
// stencil mask and a colour mask, binds its own program, buffers and textures,
// and leaves all of it set. The UI's beginFrame establishes only what it uses,
// which was correct while nothing else touched the context and is not correct
// now. Fixing it here rather than in the renderer keeps the invariant next to
// the thing that breaks it: the rule is that retro_run leaves the context as
// it found it, and this is that rule.
void restoreGLState() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glUseProgram(0);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glStencilMask(0xFF);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    // The software upload path sets these and a core may too. Left set, a
    // stride from one core's frame corrupts the next texture anything uploads,
    // which shows up as skewed cover art rather than as anything to do with
    // emulation.
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

// The hardware-render calls of the environment callback. core.cpp's
// environment asks here before its own switch; nothing for any other call.
std::optional<bool> environmentHWRender(unsigned cmd, void* data) {
    switch (cmd) {
        case RETRO_ENVIRONMENT_SET_HW_RENDER: {
            // Flycast, Mupen64Plus, PPSSPP and melonDS. See the block above
            // gHW for what is handed over and why it is a framebuffer rather
            // than a context (and, for desktop GL, a second context too).
            auto* cb = static_cast<retro_hw_render_callback*>(data);
            if (!cb) return false;
            gHWContextName = contextName(*cb);
            if (gRefuseHWRender) {
                std::fprintf(stderr,
                             "[core] refusing %s because --core-no-hw-render was "
                             "asked for; this is the control, not a fault\n",
                             gHWContextName.c_str());
                return false;
            }
            if (!canServe(*cb)) {
                // Named, not just refused. "Hardware rendering unavailable" is
                // the kind of message that sends somebody looking at the core;
                // the version is what tells them the frontend's context is the
                // thing to change.
                unsigned haveMajor = 3, haveMinor = 0;
                contextVersion(haveMajor, haveMinor);
                if (isDesktopGL(*cb))
                    std::fprintf(stderr,
                                 "[core] asked for %s; this machine cannot make a desktop "
                                 "OpenGL context beside the console's (\"desktop OpenGL "
                                 "for cores\" above says why)\n",
                                 gHWContextName.c_str());
                else
                    std::fprintf(stderr,
                                 "[core] asked for %s; this frontend holds an OpenGL "
                                 "ES %u.%u context and will not pretend otherwise\n",
                                 gHWContextName.c_str(), haveMajor, haveMinor);
                return false;
            }
            // ASKED AGAIN, THE CONTEXT IS KEPT. melonDS asks a second time
            // from its first retro_run, with its own context current;
            // making a new one there threw away every object it had and left
            // the UI's context current under it, and the console crashed
            // about 25 s later (A9, 2026-10-18). RetroArch only stores a
            // repeated ask; so does this.
            if (isDesktopGL(*cb) && !gHWDesktopContext) {
                std::string what;
                if (!openDesktopContext(*cb, &what)) {
                    std::fprintf(stderr, "[core] asked for %s and it could not be made: %s\n",
                                 gHWContextName.c_str(), what.c_str());
                    return false;
                }
                std::fprintf(stderr, "[core] the core's own context: OpenGL %s\n",
                             what.c_str());
            }
            gHWVulkan = cb->context_type == RETRO_HW_CONTEXT_VULKAN;
            gHWInterfaceLogged = 0;
            if (!gHWVulkan) {
                // The core keeps this struct and calls through it, so the two
                // frontend-owned fields are written into the CORE's copy. Ours
                // is for context_reset, context_destroy and the depth/stencil
                // flags, which are read after the core has stopped looking at
                // it.
                //
                // NEITHER FIELD MEANS ANYTHING IN VULKAN. There is no
                // framebuffer object to name and no context to load symbols
                // out of; the core gets the device and the queue through
                // GET_HW_RENDER_INTERFACE instead. Filling them in anyway
                // would hand a Vulkan core two GL function pointers.
                cb->get_current_framebuffer = currentFramebuffer;
                cb->get_proc_address = procAddress;
            }
            gHW = *cb;
            gHWWanted = true;
            std::fprintf(stderr, "[core] hardware rendering: %s, %s origin\n",
                         gHWContextName.c_str(),
                         cb->bottom_left_origin ? "bottom-left" : "top-left");
            return true;
        }

        case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
            // Worth answering rather than refusing: the cores that can render
            // through more than one API ask this before choosing, and an
            // unanswered ask means the core picks whatever its build defaults
            // to — which on Linux is usually desktop GL, which this context
            // cannot serve. So this is the difference between Flycast asking
            // for something serveable and Flycast asking for something that
            // has to be turned down.
            //
            // THIS ONE LINE IS WHAT DECIDES WHICH API A CORE USES, and it is
            // the "advertise whichever it got" half of docs/PROJECT.md open
            // question 20. It was hard-wired to GLES, and that is why Dolphin
            // spent fifty seconds of emulated Mario Kart drawing into a
            // context its own render thread could not reach: the core asked
            // what we preferred, was told OpenGL ES, and dutifully took it —
            // and never asked for the Vulkan it also has compiled in.
            //
            // A core that cannot do Vulkan is unaffected: it ignores an answer
            // it has no path for and asks for GLES anyway, which canServe
            // still accepts. Nothing about those cores changes.
            *static_cast<unsigned*>(data) =
                vk::available() ? RETRO_HW_CONTEXT_VULKAN : RETRO_HW_CONTEXT_OPENGLES3;
            return true;

        case RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE: {
            // How a Vulkan core says which physical device it needs and which
            // extensions must be on it. It can arrive either side of
            // SET_HW_RENDER, so it is stored and acted on when the device is
            // built. See vkhost.h.
            auto* n = static_cast<const retro_hw_render_context_negotiation_interface*>(data);
            if (!n || n->interface_type !=
                          RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN)
                return false;
            vk::setNegotiation(
                reinterpret_cast<const retro_hw_render_context_negotiation_interface_vulkan*>(n));
            return true;
        }

        case RETRO_ENVIRONMENT_GET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_SUPPORT: {
            auto* s = static_cast<retro_hw_render_context_negotiation_interface*>(data);
            if (!s || s->interface_type !=
                          RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN)
                return false;
            // Version 1. The v2 entry points hand the frontend a wrapper the
            // core calls to create the instance and device, so that the
            // frontend can add its own extensions to what the core asked for.
            // This host does not need that yet — it passes its three required
            // extensions straight to create_device — and claiming a version
            // whose contract is not implemented is how a core ends up calling
            // a null wrapper.
            s->interface_version = 1;
            return true;
        }

        case RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE: {
            // Logged because a core that cannot get this has no way to present
            // and no way to say so — it simply runs and draws nothing, which
            // is the single hardest failure in this area to tell apart from a
            // broken game. Once per answer (gHWInterfaceLogged).
            static const char* const kAnswer[] = {"", "refused, this core is not on Vulkan",
                                                  "refused, there is no device yet",
                                                  "handed over"};
            const int answer = !gHWVulkan ? 1 : !vk::renderInterface() ? 2 : 3;
            if (answer != gHWInterfaceLogged) {
                std::fprintf(stderr, "[vulkan] the core asked for the render interface: %s\n",
                             kAnswer[answer]);
                gHWInterfaceLogged = answer;
            }
            if (!gHWVulkan) return false;
            const retro_hw_render_interface_vulkan* iface = vk::renderInterface();
            if (!iface) return false;
            *static_cast<const retro_hw_render_interface**>(data) =
                reinterpret_cast<const retro_hw_render_interface*>(iface);
            return true;
        }

        case RETRO_ENVIRONMENT_SET_HW_SHARED_CONTEXT:
            // No. A shared context is for a core that renders from its own
            // thread, and this frontend drives retro_run from the thread that
            // owns the context. Flycast's threaded renderer asks; refused, it
            // submits from the run thread, which is the arrangement the frame
            // pacing in runFor is built on.
            return false;

        default:
            return std::nullopt;
    }
}

}  // namespace corestate

using namespace corestate;

GLuint Core::texture() const {
    if (gHWVulkan) return vk::texture();
    return gHWFrame ? gHWColor : texture_;
}

bool Core::hardwareRendered() const {
    // PLAYSTATION 2 COUNTS, AND SAYING SO FIXES A REAL FAULT ON THE TELEVISION.
    //
    // This answer is read in exactly one place: whether to INTEGER scale the
    // picture. That is right for a Game Boy, whose pixels are a deliberate
    // grid, and wrong for a machine rendering a 3D scene — the comment at that
    // call site says so already, for Dreamcast.
    //
    // PCSX2 renders with Vulkan on the GPU, so it is hardware-rendered in every
    // sense that matters here; it simply does not arrive through libretro's
    // hardware-render path, which is what gHWWanted tracks. Left false, a
    // 640x448 PlayStation 2 picture floored to 2x and drew 896 rows of 1080 —
    // MMagTech, 2026-09-21: "the game didnt touch the top or bottum of the
    // screen almost like it was windowed".
    return gIsPs2 || gHWWanted;
}

unsigned Core::rotation() const { return gRotation; }

void Core::frameUV(float& u0, float& v0, float& u1, float& v1) const {
    u0 = 0;
    v0 = 0;
    u1 = 1;
    v1 = 1;
    if (gHWVulkan) {
        vk::frameUV(u0, v0, u1, v1);
        return;
    }
    if (!gHWFrame || gHWTargetW == 0 || gHWTargetH == 0) return;
    // The picture is a corner of a target sized to the core's declared
    // maximum, so sampling the whole texture would draw a small picture inside
    // a large black field.
    u1 = static_cast<float>(gFrameW) / static_cast<float>(gHWTargetW);
    const float top = static_cast<float>(gFrameH) / static_cast<float>(gHWTargetH);
    if (gHW.bottom_left_origin) {
        // Row zero is the BOTTOM of the picture. Swapping the two V
        // coordinates is the flip; it costs nothing because it is the same
        // quad drawn with its texture coordinates the other way up.
        v0 = top;
        v1 = 0;
    } else {
        v1 = top;
    }
}

}  // namespace cab
