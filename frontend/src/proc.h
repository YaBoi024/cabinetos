// Running another program, without a shell anywhere in it.
//
// WHY THIS IS ITS OWN FILE. Two things in this console shell out — the
// NetworkManager plumbing and the Bluetooth plumbing — and both of them feed
// strings CHOSEN BY STRANGERS into the command they run. A Wi-Fi scan returns
// SSIDs that anybody within radio range picked; a Bluetooth scan returns device
// names that anybody within radio range picked. Somebody who names their
// network `"; rm -rf ~"` should be a row in a list and nothing else.
//
// So there is exactly ONE place in this program that starts a process, it takes
// an argv array, and it never builds a command line. Duplicating that into two
// files would be duplicating the part that must not be got wrong twice.
//
// IT BLOCKS, AND IT SAYS SO IN ITS NAME'S PLACE — here, because there is
// nowhere else to say it. A Wi-Fi scan takes seconds and a Bluetooth scan takes
// tens of them. Callers run this on a worker, exactly as romm::Client is run,
// and a frame loop that calls it has stopped drawing.

#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace proc {

struct Result {
    // The process's exit status, or -1 when it never ran at all. Those are
    // different failures: a missing binary and a binary that ran and refused
    // need different sentences.
    int status = -1;
    bool timedOut = false;
    // Captured separately because they answer different questions: a tool puts
    // its data on one and its reason for failing on the other, and the reason
    // is the half a person can act on.
    std::string out;
    std::string err;

    bool ok() const { return status == 0; }
};

// argv[0] is looked up on PATH. Nothing is expanded, globbed or quoted, because
// nothing is parsed: this is fork, dup, execvp.
//
// The child's stdin is /dev/null. That is not tidiness — `nmcli` and
// `bluetoothctl` both prompt when they want something, and a child that
// inherits a terminal can sit on that prompt until the deadline.
Result run(const std::vector<std::string>& args, int timeoutSeconds);
// The same, stopped early (as if timed out) once `*cancel` is set. For a long
// tool somebody can walk away from: a ten-second Bluetooth scan behind a
// screen that has been closed must not hold the console for the rest of it.
Result run(const std::vector<std::string>& args, int timeoutSeconds,
           const std::atomic<bool>* cancel);
// And with what the tool prints handed over as it arrives, whole lines at a
// time, for a tool whose output is news while it runs (a Bluetooth scan).
Result run(const std::vector<std::string>& args, int timeoutSeconds,
           const std::atomic<bool>* cancel,
           const std::function<void(const std::string& line)>& onLine);

// A program that runs ALONGSIDE the console rather than being waited for: an
// emulator that is a whole application (docs/PROJECT.md, open question 32).
// Returns its pid, or -1 when it could not be started. It gets a session of
// its own, so a signal meant for the console never reaches it, and what it
// prints is appended to `logPath` rather than mixed into the console's journal.
// The caller reaps it with waitpid; nothing here waits.
int spawn(const std::vector<std::string>& args, const std::string& logPath);

// The first process below `root` whose name is `name`, or -1. A Flatpak is
// three processes deep (the sandbox, its init, then the program), and a signal
// to either sandbox process is ignored: measured 2026-09-28, SIGTERM to
// `flatpak ps`'s own child-pid left Eden running, and the same signal to Eden
// closed it in 0.8 s. An empty `name` is `root`'s first child, whatever it
// calls itself: bwrap's one child is the program it was given.
int findDescendant(int root, const std::string& name);

// Whitespace off both ends. Every one of these tools newline-terminates
// everything, and a trailing newline in an error message reads badly on a
// television.
std::string trimmed(const std::string& s);

// Split on newlines, dropping a trailing empty line.
std::vector<std::string> lines(const std::string& s);

}  // namespace proc
