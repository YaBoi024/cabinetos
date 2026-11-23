// A pretend controller, for testing what reaches a game with nobody holding one.
//
// Creates an Xbox 360-style pad through /dev/uinput, which every SDL on the
// machine sees as a real controller, then taps a button on a timer. Run on the
// A9 while a game is up to prove the whole path: real pad, the console's SDL,
// players.h, the player's virtual controller (vpad.h), the emulator.
//
//   gcc -O2 -o fake-pad tools/fake-pad.c
//   ./fake-pad WAIT TAPS EVERY [east|south|start]
//   ./fake-pad script 40 press:l3+r3 3 press:south 5
//
// The second form is a script: a number waits that many seconds, and
// `press:a+b` presses those buttons together and lets go. `down:a` holds
// buttons until `up:a` lets them go, for presses that overlap, as a person's
// two stick clicks do: `down:l3 0.033 down:r3 0.15 up:l3+r3`. `rx:N` and
// `ry:N` set the right stick (-32768 to 32767, 0 is centred). Names: south,
// east, north, west, l, r, select, start, home, l3, r3, up, down, left, right.
//
// Waits WAIT seconds, taps the button TAPS times, EVERY seconds apart, and
// stays a controller for five more seconds before it goes. `east` (Xbox B) is
// the default because that position is A on a Switch.
//
// It also takes rumble, as a real pad does, and prints each one it is asked to
// play: `rumble strong=... weak=... ms=...`. That is the other half of the
// path, the game's vibration coming back to the pad in somebody's hand.

#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static void emit(int fd, int type, int code, int value) {
    struct input_event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = type;
    ev.code = code;
    ev.value = value;
    if (write(fd, &ev, sizeof ev) < 0) perror("write");
}

static struct ff_effect effects[16];

// Waits `ms`, answering rumble uploads and printing every rumble played.
static void service(int fd, int ms) {
    struct pollfd p = {fd, POLLIN, 0};
    while (ms > 0) {
        const int step = ms < 50 ? ms : 50;
        if (poll(&p, 1, step) > 0) {
            struct input_event ev;
            while (read(fd, &ev, sizeof ev) == (ssize_t)sizeof ev) {
                if (ev.type == EV_UINPUT && ev.code == UI_FF_UPLOAD) {
                    struct uinput_ff_upload up;
                    memset(&up, 0, sizeof up);
                    up.request_id = ev.value;
                    if (ioctl(fd, UI_BEGIN_FF_UPLOAD, &up) == 0) {
                        if (up.effect.id >= 0 && up.effect.id < 16) effects[up.effect.id] = up.effect;
                        up.retval = 0;
                        ioctl(fd, UI_END_FF_UPLOAD, &up);
                    }
                } else if (ev.type == EV_UINPUT && ev.code == UI_FF_ERASE) {
                    struct uinput_ff_erase er;
                    memset(&er, 0, sizeof er);
                    er.request_id = ev.value;
                    if (ioctl(fd, UI_BEGIN_FF_ERASE, &er) == 0) {
                        er.retval = 0;
                        ioctl(fd, UI_END_FF_ERASE, &er);
                    }
                } else if (ev.type == EV_FF && ev.code < 16) {
                    const struct ff_effect* e = &effects[ev.code];
                    printf("rumble %s strong=%u weak=%u ms=%u\n", ev.value ? "play" : "stop",
                           e->u.rumble.strong_magnitude, e->u.rumble.weak_magnitude,
                           e->replay.length);
                    fflush(stdout);
                }
            }
        }
        ms -= step;
    }
}

static int codeOf(const char* n) {
    static const struct { const char* name; int code; } names[] = {
        {"south", BTN_SOUTH}, {"east", BTN_EAST}, {"north", BTN_NORTH}, {"west", BTN_WEST},
        {"l", BTN_TL}, {"r", BTN_TR}, {"select", BTN_SELECT}, {"start", BTN_START},
        {"home", BTN_MODE}, {"l3", BTN_THUMBL}, {"r3", BTN_THUMBR},
        {"up", -1}, {"down", -2}, {"left", -3}, {"right", -4},
    };
    for (size_t i = 0; i < sizeof names / sizeof names[0]; ++i)
        if (strcmp(n, names[i].name) == 0) return names[i].code;
    return 0;
}

static void press(int fd, const char* spec, int down) {
    char buf[128];
    snprintf(buf, sizeof buf, "%s", spec);
    for (char* tok = strtok(buf, "+"); tok; tok = strtok(NULL, "+")) {
        const int c = codeOf(tok);
        if (c > 0) emit(fd, EV_KEY, c, down);
        else if (c == -1 || c == -2) emit(fd, EV_ABS, ABS_HAT0Y, down ? (c == -1 ? -1 : 1) : 0);
        else if (c == -3 || c == -4) emit(fd, EV_ABS, ABS_HAT0X, down ? (c == -3 ? -1 : 1) : 0);
    }
    emit(fd, EV_SYN, SYN_REPORT, 0);
}

int main(int argc, char** argv) {
    const int scripted = argc > 1 && strcmp(argv[1], "script") == 0;
    if (argc < 4 && !scripted) {
        fprintf(stderr, "usage: %s WAIT TAPS EVERY [east|south|start]\n", argv[0]);
        return 2;
    }
    const double wait = scripted ? 0 : atof(argv[1]);
    const int taps = scripted ? 0 : atoi(argv[2]);
    const double every = scripted ? 0 : atof(argv[3]);
    int code = BTN_EAST;
    if (!scripted && argc > 4 && strcmp(argv[4], "south") == 0) code = BTN_SOUTH;
    if (!scripted && argc > 4 && strcmp(argv[4], "start") == 0) code = BTN_START;

    const int fd = open("/dev/uinput", O_RDWR | O_NONBLOCK);
    if (fd < 0) { perror("/dev/uinput"); return 1; }
    const int keys[] = {BTN_SOUTH, BTN_EAST, BTN_NORTH, BTN_WEST, BTN_TL, BTN_TR,
                        BTN_SELECT, BTN_START, BTN_MODE, BTN_THUMBL, BTN_THUMBR};
    const int axes[] = {ABS_X, ABS_Y, ABS_Z, ABS_RX, ABS_RY, ABS_RZ, ABS_HAT0X, ABS_HAT0Y};
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_ABS);
    ioctl(fd, UI_SET_EVBIT, EV_FF);
    ioctl(fd, UI_SET_FFBIT, FF_RUMBLE);
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; ++i) ioctl(fd, UI_SET_KEYBIT, keys[i]);
    for (size_t i = 0; i < sizeof axes / sizeof axes[0]; ++i) ioctl(fd, UI_SET_ABSBIT, axes[i]);

    struct uinput_setup setup;
    memset(&setup, 0, sizeof setup);
    setup.id.bustype = BUS_USB;
    setup.id.vendor = 0x045e;
    setup.id.product = 0x028e;
    setup.id.version = 0x0114;
    snprintf(setup.name, sizeof setup.name, "Microsoft X-Box 360 pad");
    setup.ff_effects_max = 16;
    ioctl(fd, UI_DEV_SETUP, &setup);
    for (size_t i = 0; i < sizeof axes / sizeof axes[0]; ++i) {
        struct uinput_abs_setup a;
        memset(&a, 0, sizeof a);
        a.code = axes[i];
        const int hat = axes[i] == ABS_HAT0X || axes[i] == ABS_HAT0Y;
        const int trigger = axes[i] == ABS_Z || axes[i] == ABS_RZ;
        a.absinfo.minimum = hat ? -1 : trigger ? 0 : -32768;
        a.absinfo.maximum = hat ? 1 : trigger ? 255 : 32767;
        ioctl(fd, UI_ABS_SETUP, &a);
    }
    if (ioctl(fd, UI_DEV_CREATE) < 0) { perror("UI_DEV_CREATE"); return 1; }
    printf("fake pad up\n");
    fflush(stdout);

    if (scripted) {
        for (int i = 2; i < argc; ++i) {
            if (strncmp(argv[i], "press:", 6) == 0) {
                press(fd, argv[i] + 6, 1);
                service(fd, 150);
                press(fd, argv[i] + 6, 0);
                printf("pressed %s\n", argv[i] + 6);
                fflush(stdout);
            } else if (strncmp(argv[i], "down:", 5) == 0) {
                press(fd, argv[i] + 5, 1);
                printf("down %s\n", argv[i] + 5);
                fflush(stdout);
            } else if (strncmp(argv[i], "rx:", 3) == 0 || strncmp(argv[i], "ry:", 3) == 0) {
                emit(fd, EV_ABS, argv[i][1] == 'x' ? ABS_RX : ABS_RY, atoi(argv[i] + 3));
                emit(fd, EV_SYN, SYN_REPORT, 0);
                printf("%s\n", argv[i]);
                fflush(stdout);
            } else if (strncmp(argv[i], "up:", 3) == 0) {
                press(fd, argv[i] + 3, 0);
                printf("up %s\n", argv[i] + 3);
                fflush(stdout);
            } else {
                service(fd, (int)(atof(argv[i]) * 1000));
            }
        }
    }
    service(fd, (int)(wait * 1000));
    for (int i = 0; i < taps; ++i) {
        emit(fd, EV_KEY, code, 1);
        emit(fd, EV_SYN, SYN_REPORT, 0);
        service(fd, 120);
        emit(fd, EV_KEY, code, 0);
        emit(fd, EV_SYN, SYN_REPORT, 0);
        printf("tap %d\n", i + 1);
        fflush(stdout);
        service(fd, (int)(every * 1000));
    }
    service(fd, 5000);
    ioctl(fd, UI_DEV_DESTROY);
    close(fd);
    return 0;
}
