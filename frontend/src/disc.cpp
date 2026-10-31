#include "disc.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>

#include <json-c/json.h>

#include "fileio.h"

namespace disc {

System systemFor(const std::string& slug) {
    // From each core's source at our pins (#162, research 2026-10-11).
    // pcsx_rearmed and flycast take set_initial_image with the exact path;
    // beetle_saturn registers its interface during the load, too late for it;
    // genesis_plus_gx has no start-on-disc at all; Opera (3DO) has no disk
    // control and reads no list. Dolphin changes the disc itself when the
    // game asks (AutoDiscChange), given the whole list.
    if (slug == "psx") return {Change::Insert, Boot::InitialImage};
    if (slug == "dc") return {Change::Insert, Boot::InitialImage};
    if (slug == "saturn") return {Change::Insert, Boot::SwapFirst};
    if (slug == "segacd") return {Change::Insert, Boot::ListFirst};
    if (slug == "3do") return {Change::StartOver, Boot::File};
    if (slug == "ngc") return {Change::Automatic, Boot::ListFirst};
    return {};
}

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string extOf(const std::string& name) {
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= name.size()) return {};
    return lower(name.substr(dot + 1));
}

std::string stemOf(const std::string& name) {
    const size_t dot = name.find_last_of('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

bool isSheet(const std::string& ext) {
    return ext == "cue" || ext == "gdi" || ext == "ccd" || ext == "toc" || ext == "mds";
}

bool isImage(const std::string& ext) {
    static const char* const kImages[] = {"chd", "iso", "bin", "img", "cdi", "pbp", "cso",
                                          "zso", "rvz", "gcz", "ciso", "wbfs", "wia",
                                          "gcm", "nrg", "mdf"};
    for (const char* k : kImages)
        if (ext == k) return true;
    return false;
}

bool isAlnum(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; }

// NATURAL ORDER: "Disc 2" before "Disc 10", case ignored.
bool naturalLess(const std::string& a, const std::string& b) {
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        const unsigned char ca = static_cast<unsigned char>(a[i]);
        const unsigned char cb = static_cast<unsigned char>(b[j]);
        if (std::isdigit(ca) && std::isdigit(cb)) {
            size_t ie = i, je = j;
            while (ie < a.size() && std::isdigit(static_cast<unsigned char>(a[ie]))) ++ie;
            while (je < b.size() && std::isdigit(static_cast<unsigned char>(b[je]))) ++je;
            const unsigned long long na = std::strtoull(a.substr(i, ie - i).c_str(), nullptr, 10);
            const unsigned long long nb = std::strtoull(b.substr(j, je - j).c_str(), nullptr, 10);
            if (na != nb) return na < nb;
            i = ie;
            j = je;
            continue;
        }
        const int la = std::tolower(ca), lb = std::tolower(cb);
        if (la != lb) return la < lb;
        ++i;
        ++j;
    }
    return (a.size() - i) < (b.size() - j);
}

// A DISC MARKER: Disc, Disk, CD, DVD, Side, Part or D, then a number or one
// letter, brackets optional: "(Disc 2)", "CD1", "Side B", "(D3)". Where it
// is in the name and what it says. The LAST in the name is the one used:
// a title can hold a word like "Part" of its own.
struct Marker {
    bool found = false;
    bool letter = false;
    int value = 0;       // the number, or a letter's place (A = 1)
    size_t at = 0, end = 0;
};

Marker lastMarker(const std::string& stem) {
    static const char* const kWords[] = {"disc", "disk", "dvd", "side", "part", "cd", "d"};
    const std::string s = lower(stem);
    Marker best;
    for (size_t p = 0; p < s.size(); ++p) {
        if (p > 0 && isAlnum(s[p - 1])) continue;
        for (const char* w : kWords) {
            const size_t wl = std::strlen(w);
            if (s.compare(p, wl, w) != 0) continue;
            size_t q = p + wl;
            // "D" alone only directly before its number ("D1"): a lone "d"
            // with a space after is a word, not a marker.
            const bool bare = wl == 1;
            if (!bare)
                while (q < s.size() && (s[q] == ' ' || s[q] == '-' || s[q] == '_' || s[q] == '.'))
                    ++q;
            if (q >= s.size()) continue;
            Marker m;
            if (std::isdigit(static_cast<unsigned char>(s[q]))) {
                size_t e = q;
                while (e < s.size() && std::isdigit(static_cast<unsigned char>(s[e]))) ++e;
                if (e < s.size() && std::isalpha(static_cast<unsigned char>(s[e]))) continue;
                m.value = std::atoi(s.substr(q, e - q).c_str());
                m.end = e;
            } else if (!bare && std::isalpha(static_cast<unsigned char>(s[q])) &&
                       (q + 1 >= s.size() || !isAlnum(s[q + 1]))) {
                m.value = s[q] - 'a' + 1;
                m.letter = true;
                m.end = q + 1;
            } else {
                continue;
            }
            m.found = true;
            m.at = p;
            best = m;   // later ones replace earlier ones
            break;
        }
    }
    return best;
}

// The bracketed parts of a name: "(USA)", "(Disc 1)", "[!]", without their
// brackets.
std::vector<std::pair<size_t, std::string>> groups(const std::string& stem) {
    std::vector<std::pair<size_t, std::string>> out;
    for (size_t p = 0; p < stem.size(); ++p) {
        const char open = stem[p];
        if (open != '(' && open != '[') continue;
        const char close = open == '(' ? ')' : ']';
        const size_t e = stem.find(close, p + 1);
        if (e == std::string::npos) break;
        out.emplace_back(p, stem.substr(p + 1, e - p - 1));
        p = e;
    }
    return out;
}

}  // namespace

std::vector<Disc> fromNames(const std::vector<std::string>& names,
                            const std::vector<std::string>& listed) {
    std::vector<size_t> order(names.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::vector<Marker> markers(names.size());
    for (size_t i = 0; i < names.size(); ++i) markers[i] = lastMarker(stemOf(names[i]));

    // 1. THE LIBRARY'S OWN LIST, when it names every disc.
    bool byList = !listed.empty();
    std::vector<int> listAt(names.size(), -1);
    if (byList) {
        for (size_t i = 0; i < names.size() && byList; ++i) {
            for (size_t k = 0; k < listed.size(); ++k) {
                std::string l = listed[k];
                if (const size_t slash = l.find_last_of("/\\"); slash != std::string::npos)
                    l.erase(0, slash + 1);
                if (l == names[i]) { listAt[i] = static_cast<int>(k); break; }
            }
            if (listAt[i] < 0) byList = false;
        }
    }
    // 2. A DISC MARKER IN EVERY NAME, each saying something different.
    bool byMarker = !byList && !names.empty();
    for (size_t i = 0; i < names.size() && byMarker; ++i) {
        if (!markers[i].found) byMarker = false;
        for (size_t j = 0; j < i && byMarker; ++j)
            if (markers[j].value == markers[i].value) byMarker = false;
    }
    if (byList) {
        std::sort(order.begin(), order.end(),
                  [&](size_t a, size_t b) { return listAt[a] < listAt[b]; });
    } else if (byMarker) {
        std::sort(order.begin(), order.end(),
                  [&](size_t a, size_t b) { return markers[a].value < markers[b].value; });
    } else {
        // 3. THE PART WHERE THE NAMES DIFFER, naturally; that is name order
        // when nothing else separates them.
        std::vector<std::string> stems;
        for (const std::string& n : names) stems.push_back(stemOf(n));
        size_t pre = stems.empty() ? 0 : stems[0].size(), suf = pre;
        for (const std::string& s : stems) {
            size_t k = 0;
            while (k < pre && k < s.size() && s[k] == stems[0][k]) ++k;
            pre = k;
        }
        for (const std::string& s : stems) {
            size_t k = 0;
            while (k < suf && k < s.size() && k < stems[0].size() &&
                   s[s.size() - 1 - k] == stems[0][stems[0].size() - 1 - k])
                ++k;
            suf = k;
        }
        auto middle = [&](size_t i) {
            const std::string& s = stems[i];
            if (pre + suf >= s.size()) return std::string();
            return s.substr(pre, s.size() - pre - suf);
        };
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            const std::string ma = middle(a), mb = middle(b);
            if (naturalLess(ma, mb)) return true;
            if (naturalLess(mb, ma)) return false;
            return naturalLess(names[a], names[b]);
        });
    }

    // THE NUMBERS: the names' own, when every name numbers its disc from 1
    // and no two say the same (a disc missing on RomM leaves its gap); else
    // the place in the order.
    bool ownNumbers = !names.empty();
    for (size_t i = 0; i < names.size() && ownNumbers; ++i) {
        if (!markers[i].found || markers[i].letter || markers[i].value < 1) ownNumbers = false;
        for (size_t j = 0; j < i && ownNumbers; ++j)
            if (markers[j].value == markers[i].value) ownNumbers = false;
    }

    // THE NAMES: "Disc N", and the bracketed parts only this disc's name has.
    std::vector<std::vector<std::pair<size_t, std::string>>> gs;
    for (const std::string& n : names) gs.push_back(groups(stemOf(n)));
    std::vector<Disc> out;
    for (size_t k = 0; k < order.size(); ++k) {
        const size_t i = order[k];
        Disc d;
        d.number = ownNumbers ? markers[i].value : static_cast<int>(k) + 1;
        d.file = names[i];
        d.label = "Disc " + std::to_string(d.number);
        for (const auto& [at, g] : gs[i]) {
            const Marker& m = markers[i];
            // The marker's own group: said only when it says another number
            // than the console's count ("Disc 1 (Disc 0)", Enemy Zero).
            if (m.found && m.at > at && m.at <= at + g.size()) {
                if (m.value != d.number) d.label += " (" + g + ")";
                continue;
            }
            bool everywhere = names.size() > 1;
            for (size_t j = 0; j < names.size() && everywhere; ++j) {
                if (j == i) continue;
                bool has = false;
                for (const auto& [at2, g2] : gs[j]) has = has || g2 == g;
                everywhere = has;
            }
            if (!everywhere) d.label += " (" + g + ")";
        }
        out.push_back(std::move(d));
    }
    return out;
}

std::vector<int> missing(const std::vector<std::string>& names) {
    std::vector<int> seen;
    for (const std::string& n : names) {
        const Marker m = lastMarker(stemOf(n));
        if (!m.found || m.letter) return {};
        for (int v : seen)
            if (v == m.value) return {};
        seen.push_back(m.value);
    }
    if (seen.empty()) return {};
    const int lo = *std::min_element(seen.begin(), seen.end());
    const int hi = *std::max_element(seen.begin(), seen.end());
    std::vector<int> out;
    for (int v = std::min(lo, 1); v <= hi; ++v)
        if (std::find(seen.begin(), seen.end(), v) == seen.end()) out.push_back(v);
    return out;
}

Plan plan(const std::vector<romm::RomFile>& files) {
    Plan p;
    std::vector<const romm::RomFile*> mine;
    for (const romm::RomFile& f : files) {
        if (!f.topLevel) continue;
        if (!f.category.empty() && f.category != "game") continue;
        mine.push_back(&f);
    }
    bool sheets = false;
    for (const romm::RomFile* f : mine) sheets = sheets || isSheet(extOf(f->fileName));
    for (const romm::RomFile* f : mine) {
        const std::string ext = extOf(f->fileName);
        if (ext == "m3u") {
            if (!p.listFileId) {
                p.listFileId = f->id;
                p.listFileName = f->fileName;
            }
            continue;
        }
        p.fetch.push_back(*f);
        if (sheets ? isSheet(ext) : isImage(ext)) p.discNames.push_back(f->fileName);
    }
    return p;
}

std::vector<std::string> readList(const std::string& text) {
    std::vector<std::string> out;
    size_t p = 0;
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) p = 3;
    while (p < text.size()) {
        size_t e = text.find('\n', p);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(p, e - p);
        p = e + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        // "file|label" is RetroArch's label syntax; the name is before it.
        if (const size_t bar = line.find('|'); bar != std::string::npos) line.erase(bar);
        out.push_back(line);
    }
    return out;
}

std::string listFileName(const std::string& fsName) {
    return storage::safeSegment(fsName) + ".m3u";
}

std::vector<int> listOrder(const std::vector<Disc>& discs, int first) {
    std::vector<int> o;
    if (byNumber(discs, first)) o.push_back(first);
    for (const Disc& d : discs)
        if (d.number != first) o.push_back(d.number);
    return o;
}

std::string listText(const std::vector<Disc>& discs, int first) {
    std::string t;
    for (int n : listOrder(discs, first))
        if (const Disc* d = byNumber(discs, n)) t += d->file + "\n";
    return t;
}

const Disc* byNumber(const std::vector<Disc>& discs, int n) {
    for (const Disc& d : discs)
        if (d.number == n) return &d;
    return nullptr;
}

int highest(const std::vector<Disc>& discs) {
    int h = 0;
    for (const Disc& d : discs) h = std::max(h, d.number);
    return h;
}

bool writeRecord(const std::string& entryDir, const std::vector<Disc>& discs) {
    std::string t;
    for (const Disc& d : discs) t += d.file + "\n";
    return cab::writeBytes(entryDir + "/.discs", std::vector<uint8_t>(t.begin(), t.end()));
}

std::vector<Disc> readRecord(const std::string& entryDir) {
    const std::vector<uint8_t> b = cab::readBytes(entryDir + "/.discs");
    if (b.empty()) return {};
    const std::vector<std::string> names = readList(std::string(b.begin(), b.end()));
    // The order is the record's; the names come back by the same rule, so
    // a disc is called the same thing on and off line.
    return fromNames(names, names);
}

namespace {

std::mutex gLock;

std::string recordPath(const storage::User& u) { return storage::userDir(u) + "/discs.json"; }

struct Known { int disc = 0, count = 0; };

std::map<int, Known> load(const storage::User& u) {
    std::map<int, Known> m;
    const std::vector<uint8_t> b = cab::readBytes(recordPath(u));
    if (b.empty()) return m;
    const std::string text(b.begin(), b.end());
    json_object* root = json_tokener_parse(text.c_str());
    if (!root) return m;
    if (json_object_get_type(root) == json_type_object) {
        json_object_object_foreach(root, key, val) {
            Known k;
            json_object* v = nullptr;
            if (json_object_object_get_ex(val, "disc", &v)) k.disc = json_object_get_int(v);
            if (json_object_object_get_ex(val, "of", &v)) k.count = json_object_get_int(v);
            m[std::atoi(key)] = k;
        }
    }
    json_object_put(root);
    return m;
}

}  // namespace

int lastDisc(const storage::User& u, int romId) {
    if (!u.valid()) return 0;
    std::lock_guard<std::mutex> lk(gLock);
    const auto m = load(u);
    const auto it = m.find(romId);
    return it == m.end() ? 0 : it->second.disc;
}

int knownCount(const storage::User& u, int romId) {
    if (!u.valid()) return 0;
    std::lock_guard<std::mutex> lk(gLock);
    const auto m = load(u);
    const auto it = m.find(romId);
    return it == m.end() ? 0 : it->second.count;
}

void remember(const storage::User& u, int romId, int disc, int count) {
    if (!u.valid() || romId <= 0 || disc <= 0) return;
    std::lock_guard<std::mutex> lk(gLock);
    auto m = load(u);
    if (auto it = m.find(romId); it != m.end() && it->second.disc == disc &&
                                 it->second.count == count)
        return;
    m[romId] = {disc, count};
    json_object* root = json_object_new_object();
    for (const auto& [rom, k] : m) {
        json_object* o = json_object_new_object();
        json_object_object_add(o, "disc", json_object_new_int(k.disc));
        json_object_object_add(o, "of", json_object_new_int(k.count));
        json_object_object_add(root, std::to_string(rom).c_str(), o);
    }
    const std::string text = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN);
    json_object_put(root);
    if (!cab::writeBytes(recordPath(u), std::vector<uint8_t>(text.begin(), text.end())))
        std::fprintf(stderr, "[disc] could not write %s\n", recordPath(u).c_str());
}

std::string stateNamePart(int disc) { return " (Disc " + std::to_string(disc) + ")"; }

int discInStateName(const std::string& name) {
    // The last "(Disc N)" before the time stamp's " [".
    const size_t stamp = name.rfind(" [");
    const std::string head = stamp == std::string::npos ? name : name.substr(0, stamp);
    const size_t at = head.rfind(" (Disc ");
    if (at == std::string::npos) return 0;
    const size_t num = at + 7;
    size_t e = num;
    while (e < head.size() && std::isdigit(static_cast<unsigned char>(head[e]))) ++e;
    if (e == num || e >= head.size() || head[e] != ')') return 0;
    return std::atoi(head.substr(num, e - num).c_str());
}

std::string ofText(int disc, int count) {
    return "Disc " + std::to_string(disc) + " of " + std::to_string(count);
}

int test() {
    int failures = 0;
    auto check = [&](bool ok, const std::string& what) {
        std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
        if (!ok) ++failures;
    };
    auto files = [](const std::vector<Disc>& d) {
        std::string s;
        for (const Disc& x : d) s += (s.empty() ? "" : " | ") + x.file;
        return s;
    };
    auto labels = [](const std::vector<Disc>& d) {
        std::string s;
        for (const Disc& x : d) s += (s.empty() ? "" : " | ") + x.label;
        return s;
    };

    {   // RomM's own order is not disc order (Panzer Dragoon Saga's ids).
        const auto d = fromNames({"Panzer Dragoon Saga (USA) (Disc 1).chd",
                                  "Panzer Dragoon Saga (USA) (Disc 4).chd",
                                  "Panzer Dragoon Saga (USA) (Disc 3).chd",
                                  "Panzer Dragoon Saga (USA) (Disc 2).chd"});
        check(labels(d) == "Disc 1 | Disc 2 | Disc 3 | Disc 4", "ordered by marker: " + labels(d));
        check(d[1].file == "Panzer Dragoon Saga (USA) (Disc 2).chd", "disc 2 is disc 2");
    }
    {   // What only one disc's name says is said.
        const auto d = fromNames({"Command & Conquer - Red Alert (USA) (Disc 2) (Soviet).chd",
                                  "Command & Conquer - Red Alert (USA) (Disc 1) (Allies).chd"});
        check(labels(d) == "Disc 1 (Allies) | Disc 2 (Soviet)", "Red Alert: " + labels(d));
    }
    {   // A part every disc shares is not.
        const auto d = fromNames({"Metal Gear Solid (USA) (Disc 2) (Rev 1).chd",
                                  "Metal Gear Solid (USA) (Disc 1) (Rev 1).chd"});
        check(labels(d) == "Disc 1 | Disc 2", "MGS: " + labels(d));
    }
    {   // Disc 10 after Disc 9.
        std::vector<std::string> n;
        for (int i = 10; i >= 1; --i) n.push_back("Riven (Disc " + std::to_string(i) + ").cue");
        const auto d = fromNames(n);
        check(d[9].file == "Riven (Disc 10).cue" && d[1].file == "Riven (Disc 2).cue",
              "numbers, not letters: " + files(d));
    }
    {   // Other markers, no brackets, letters.
        const auto a = fromNames({"Game CD2.iso", "Game CD1.iso"});
        check(a[0].file == "Game CD1.iso", "CD1: " + files(a));
        const auto b = fromNames({"Game - Side B.chd", "Game - Side A.chd"});
        check(b[0].file == "Game - Side A.chd", "Side A: " + files(b));
        const auto c = fromNames({"Game (D2).chd", "Game (D1).chd"});
        check(c[0].file == "Game (D1).chd", "D1: " + files(c));
        const auto e = fromNames({"Final Fantasy VII (Disk 3).chd", "Final Fantasy VII (Disk 1).chd",
                                  "Final Fantasy VII (Disk 2).chd"});
        check(files(e) == "Final Fantasy VII (Disk 1).chd | Final Fantasy VII (Disk 2).chd | "
                          "Final Fantasy VII (Disk 3).chd", "Disk: " + files(e));
    }
    {   // A title's own word is not the marker: the last one is.
        const auto d = fromNames({"Hero Part 2 (Disc 2).chd", "Hero Part 2 (Disc 1).chd"});
        check(d[0].file == "Hero Part 2 (Disc 1).chd", "Part in a title: " + files(d));
    }
    {   // Odd names: no marker, the part that differs, naturally.
        const auto d = fromNames({"mygame_b.chd", "mygame_a.chd", "mygame_c.chd"});
        check(files(d) == "mygame_a.chd | mygame_b.chd | mygame_c.chd", "no marker: " + files(d));
        check(labels(d) == "Disc 1 | Disc 2 | Disc 3", "no marker labels: " + labels(d));
        const auto e = fromNames({"x 10.iso", "x 9.iso"});
        check(e[0].file == "x 9.iso", "differing part, naturally: " + files(e));
    }
    {   // A disc 0 keeps its own number in its name.
        const auto d = fromNames({"Enemy Zero (USA) (Disc 1) (Game Disc).cue",
                                  "Enemy Zero (USA) (Disc 0) (Opening Disc).cue"});
        check(d[0].file.find("Disc 0") != std::string::npos, "disc 0 first: " + files(d));
        check(d[0].label == "Disc 1 (Disc 0) (Opening Disc)", "disc 0's name: " + d[0].label);
    }
    {   // The library's list decides the order when it names every disc.
        const auto d = fromNames({"b.chd", "a.chd"}, readList("\xEF\xBB\xBF#EXTM3U\r\nb.chd\r\n\r\na.chd|Second\r\n"));
        check(files(d) == "b.chd | a.chd", "the library's list: " + files(d));
        const auto e = fromNames({"b.chd", "a.chd"}, readList("b.chd\n"));
        check(files(e) == "a.chd | b.chd", "a list missing a disc is ignored: " + files(e));
    }
    {   // Which files: the game's own, a sheet's tracks with it.
        std::vector<romm::RomFile> f = {
            {1, "G (Disc 1).cue", 100, "game", true, {}},
            {2, "G (Disc 1) (Track 1).bin", 100, "game", true, {}},
            {3, "G (Disc 2).cue", 100, "game", true, {}},
            {4, "G (Disc 2) (Track 1).bin", 100, "game", true, {}},
            {5, "G.m3u", 10, "game", true, {}},
            {6, "Manual.pdf", 10, "manual", false, {}},
            {7, "Soundtrack.mp3", 10, "soundtrack", true, {}},
        };
        const Plan p = plan(f);
        check(p.fetch.size() == 4, "fetch the sheets and their tracks: " + std::to_string(p.fetch.size()));
        check(p.discNames.size() == 2, "two discs, the sheets");
        check(p.listFileId == 5, "the library's list is read, not fetched");
        std::vector<romm::RomFile> g = {{1, "RE4 (Disc 1).rvz", 1, "game", true, {}},
                                        {2, "RE4 (Disc 2).rvz", 1, "game", true, {}}};
        check(plan(g).discNames.size() == 2, "images are discs when there is no sheet");
        std::vector<romm::RomFile> h = {{1, "One.cue", 1, "game", true, {}},
                                        {2, "One (Track 1).bin", 1, "game", true, {}},
                                        {3, "One (Track 2).bin", 1, "game", true, {}}};
        check(plan(h).discNames.size() == 1, "one sheet and its tracks is one disc");
    }
    {   // The list, and Start over's order.
        const auto d = fromNames({"a (Disc 1).chd", "a (Disc 2).chd", "a (Disc 3).chd"});
        check(listText(d) == "a (Disc 1).chd\na (Disc 2).chd\na (Disc 3).chd\n", "plain list, LF, no blank line");
        check(listText(d, 2) == "a (Disc 2).chd\na (Disc 1).chd\na (Disc 3).chd\n", "disc 2 first");
        const std::vector<int> o = listOrder(d, 3);
        check(o == std::vector<int>({3, 1, 2}), "core index to disc number");
        check(listFileName("Panzer Dragoon") == "Panzer Dragoon.m3u", "named after the game");
    }
    {   // A disc missing on RomM keeps the others' numbers.
        const auto d = fromNames({"G (Disc 4).chd", "G (Disc 1).chd", "G (Disc 3).chd"});
        check(labels(d) == "Disc 1 | Disc 3 | Disc 4", "the names' own numbers: " + labels(d));
        check(highest(d) == 4 && byNumber(d, 2) == nullptr, "of 4, no disc 2");
        check(listText(d, 3) == "G (Disc 3).chd\nG (Disc 1).chd\nG (Disc 4).chd\n",
              "disc 3 first, by number");
    }
    {   // A disc missing on RomM.
        check(missing({"G (Disc 1).chd", "G (Disc 3).chd"}) == std::vector<int>({2}),
              "disc 2 missing");
        check(missing({"G (Disc 2).chd", "G (Disc 3).chd"}) == std::vector<int>({1}),
              "disc 1 missing");
        check(missing({"G (Disc 0).cue", "G (Disc 1).cue"}).empty(), "a disc 0 is not a gap");
        check(missing({"G (Disc 1).chd", "G (Disc 2).chd"}).empty(), "none missing");
        check(missing({"G - Side A.chd", "G - Side C.chd"}).empty(), "letters say nothing");
        check(missing({"a.chd", "b.chd"}).empty(), "no markers say nothing");
    }
    {   // States.
        check(discInStateName("Panzer Dragoon (Disc 2) [2026-10-11 03-12-45-120].state") == 2,
              "a state says its disc");
        check(discInStateName("Panzer Dragoon [2026-10-11 03-12-45-120].state") == 0,
              "a state with no disc");
        check(discInStateName("Game (Disc 1) (Disc 3) [x].state") == 3, "the last one");
        check(ofText(2, 4) == "Disc 2 of 4", "Disc 2 of 4");
    }
    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}

}  // namespace disc
