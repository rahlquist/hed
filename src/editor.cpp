// SPDX-License-Identifier: MIT
// nano-style full-screen editor. Raw VT100/xterm escape sequences, no curses dependency.
#include "editor.hpp"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "spell.hpp"
#include "util.hpp"

namespace hed {
namespace {

enum Key {
    K_ESC = 27, K_BS = 127,
    K_LEFT = 1000, K_RIGHT, K_UP, K_DOWN, K_HOME, K_END, K_PGUP, K_PGDN, K_DEL, K_INS,
    K_CLEFT, K_CRIGHT, K_CUP, K_CDOWN, K_CHOME, K_CEND, K_AUP, K_ADOWN, K_STAB,
    K_F1, K_F2, K_F3, K_F4, K_F5, K_F6, K_F7, K_F8, K_F9, K_F10, K_F11, K_F12,
        K_PASTE, K_RESIZE, K_EOF, K_NONE,
        K_MLCLICK = 1100, K_MWHEELUP, K_MWHEELDOWN,  // SGR mouse events
    K_ALT = 0x100000
    };
constexpr int CK(int k) { return k & 0x1f; }

volatile sig_atomic_t g_winch = 0;
struct termios g_orig;
int g_ttyFd = -1;
bool g_raw = false;

void restoreTerm() {
    if (!g_raw) return;
    const char* s = "\x1b[?2004l\x1b[?1000l\x1b[?1006l\x1b[0m\x1b[?25h\x1b[?1049l";
    if (write(g_ttyFd, s, strlen(s)) < 0) {}
    tcsetattr(g_ttyFd, TCSAFLUSH, &g_orig);
    g_raw = false;
}
void onWinch(int) { g_winch = 1; }
void onFatal(int sig) {
    restoreTerm();
    signal(sig, SIG_DFL);
    raise(sig);
}

using Clock = std::chrono::steady_clock;

class Editor {
  public:
    explicit Editor(const EditorOptions& o) : opt(o) {}

  private:
    EditorOptions opt;
    int inFd = 0, outFd = 1;
    size_t rows = 24, cols = 80;
    std::vector<std::string> L{""};
    bool trailingNl = true, crlf = false;
    std::string filename;
    LangId lang = L_TEXT;
    bool readOnly = false;
    size_t cx = 0, cy = 0, rowoff = 0, coloff = 0;
    long wantRx = -1;
    bool showNumbers = true, spellOn = true;
    int tabsize = 4, indentWidth = 4;
    bool useTabs = false;
    std::vector<HlState> hlStart;
    size_t hlValid = 0;
    std::string clip;
    bool cutChain = false;
    bool markOn = false;
    size_t mx = 0, my = 0;
    std::string lastSearch;
    bool matchOn = false;
    size_t matchY = 0, matchX = 0, matchLen = 0;
    struct Snap { std::vector<std::string> L; size_t cx, cy; uint64_t ver; };
    std::vector<Snap> undoS, redoS;
    int lastKind = 0;
    size_t lastLine = (size_t)-1;
    Clock::time_point lastTime;
    uint64_t ver = 0, savedVer = 0, nextVer = 1;
    std::string msg;
    Clock::time_point msgTime;
    bool promptOn = false;
    std::string promptStr;
    bool quitFlag = false, emitOnExit = false;
    int exitCode = 0;
    Speller* sp = nullptr;

    // ---- status message log (ring buffer + disk log)
    std::deque<std::string> msgLog;
    size_t maxLog = 100;   // 0 = unlimited
    // ---- search match counter
    size_t matchTotal = 0, matchIdx = 0;
    // ---- mouse state (SGR coordinates, 1-based, set while parsing)
    int mouseCol = 0, mouseRow = 0;
    // ---- swap file / crash recovery
    std::string swapPath;
    Clock::time_point lastSwapWrite;

    // ---- geometry
    size_t helpRows() const { return rows >= 12 ? 2 : 0; }
    size_t textRows() const {
        long r = (long)rows - 2 - (long)helpRows();
        return r < 1 ? 1 : (size_t)r;
    }
    size_t gutterWidth() const {
        if (!showNumbers) return 0;
        size_t d = 1, n = L.size();
        while (n >= 10) { n /= 10; d++; }
        return std::max<size_t>(d, 3) + 1;
    }
    size_t textCols() const {
        size_t g = gutterWidth();
        return cols > g + 1 ? cols - g : 1;
    }
    bool dirty() const { return ver != savedVer; }

    // Record a status message: show it in the status bar, keep it in the
    // in-memory ring buffer (capped by maxLog / config log_size), and append
    // it to ~/.config/hed/log/YYYY-MM-DD.log so `hed log` can show it later.
    void setMsg(const std::string& m) {
        msg = m;
        msgTime = Clock::now();
        if (maxLog != 0) {
            msgLog.push_back(m);
            while (msgLog.size() > maxLog) msgLog.pop_front();
        }
        appendStatusLog(m);
    }
    void appendStatusLog(const std::string& m) {
        static bool dirReady = false;
        std::string dir = configDir() + "/log";
        if (!dirReady) {
            std::string err;
            if (!mkdirParents(dir, err)) return;  // can't log; stay silent
            dirReady = true;
        }
        char ts[64];
        time_t now = time(nullptr);
        struct tm tmv;
        localtime_r(&now, &tmv);
        strftime(ts, sizeof ts, "%Y-%m-%d", &tmv);
        char hm[32];
        strftime(hm, sizeof hm, "%H:%M:%S", &tmv);
        std::string err;
        appendToFile(dir + "/" + ts + ".log", std::string("[") + hm + "] " + m + "\n", err);
    }

    // ---- terminal
    void updateSize() {
        struct winsize ws;
        if (ioctl(outFd, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
            rows = ws.ws_row;
            cols = ws.ws_col;
        }
        if (rows < 3) rows = 3;
        if (cols < 20) cols = 20;
    }
    bool enableRaw() {
        if (tcgetattr(inFd, &g_orig) != 0) return false;
        g_ttyFd = inFd;
        struct termios raw = g_orig;
        raw.c_iflag &= ~(tcflag_t)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
        raw.c_oflag &= ~(tcflag_t)(OPOST);
        raw.c_cflag |= CS8;
        raw.c_lflag &= ~(tcflag_t)(ECHO | ICANON | IEXTEN | ISIG);
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(inFd, TCSAFLUSH, &raw) != 0) return false;
        g_raw = true;
                // Enable mouse reporting: ?1000 = press/release + scroll wheel,
                // ?1006 = SGR encoding (ESC [ < b ; c ; r M/m) so we get exact coords.
        writeAll(outFd, "\x1b[?1049h\x1b[?2004h\x1b[?1000h\x1b[?1006h\x1b[H\x1b[2J");
                return true;
    }
    int readByteTimeout(int ms) {
        struct pollfd p {inFd, POLLIN, 0};
        int r = poll(&p, 1, ms);
        if (r <= 0) return -1;
        unsigned char c;
        if (read(inFd, &c, 1) != 1) return -1;
        return c;
    }
    int parseCSI(const std::string& seq) {
        char fin = seq.back();
        std::string params = seq.substr(0, seq.size() - 1);
        std::vector<std::string> ps = splitSimple(params, ';');
        int mod = ps.size() >= 2 ? atoi(ps[1].c_str()) : 1;
        bool ctrl = mod >= 2 && ((mod - 1) & 4), alt = mod >= 2 && ((mod - 1) & 2);
        switch (fin) {
            case 'A': return ctrl ? K_CUP : alt ? K_AUP : K_UP;
            case 'B': return ctrl ? K_CDOWN : alt ? K_ADOWN : K_DOWN;
            case 'C': return ctrl ? K_CRIGHT : K_RIGHT;
            case 'D': return ctrl ? K_CLEFT : K_LEFT;
            case 'H': return ctrl ? K_CHOME : K_HOME;
            case 'F': return ctrl ? K_CEND : K_END;
            case 'Z': return K_STAB;
            case 'P': return K_F1;
            case 'Q': return K_F2;
            case 'R': return K_F3;
            case 'S': return K_F4;
            case '~': {
                int n = atoi(ps.empty() ? "0" : ps[0].c_str());
                switch (n) {
                    case 1: case 7: return ctrl ? K_CHOME : K_HOME;
                    case 4: case 8: return ctrl ? K_CEND : K_END;
                    case 2: return K_INS;
                    case 3: return K_DEL;
                    case 5: return K_PGUP;
                    case 6: return K_PGDN;
                    case 11: return K_F1; case 12: return K_F2; case 13: return K_F3; case 14: return K_F4;
                    case 15: return K_F5; case 17: return K_F6; case 18: return K_F7; case 19: return K_F8;
                    case 20: return K_F9; case 21: return K_F10; case 23: return K_F11; case 24: return K_F12;
                    case 200: return K_PASTE;
                    default: return K_NONE;
                }
            }
            default: return K_NONE;
        }
    }
    // Read one key. idleMs >= 0 makes the initial poll time out after that many
    // milliseconds, returning K_NONE (used by the main loop to flush the swap
    // file periodically while the buffer is dirty). idleMs < 0 blocks forever.
    // Parse an SGR mouse sequence: ESC [ < btn ; col ; row M/m.
    // btn: 0 = left press, 64 = wheel up, 65 = wheel down (motion adds 32).
    // Coordinates are 1-based and stored in mouseCol/mouseRow for process().
    int parseMouse(const std::string& seq) {
        std::string params = seq.substr(1, seq.size() - 2);  // strip '<' and final byte
        auto ps = splitSimple(params, ';');
        if (ps.size() < 3) return K_NONE;
        int btn = atoi(ps[0].c_str());
        mouseCol = atoi(ps[1].c_str());
        mouseRow = atoi(ps[2].c_str());
        bool press = seq.back() == 'M';
        if (!press) return K_NONE;  // only act on press events
        if (btn & 64) return (btn & 3) == 0 ? K_MWHEELUP : K_MWHEELDOWN;
        if ((btn & 3) == 0) return K_MLCLICK;  // left button
        return K_NONE;
    }
    int readKey(int idleMs = -1) {
        unsigned char c;
        for (;;) {
            if (g_winch) { g_winch = 0; return K_RESIZE; }
            struct pollfd p {inFd, POLLIN, 0};
            int r = poll(&p, 1, idleMs);
            if (r < 0) {
                if (errno == EINTR) continue;
                return K_EOF;
            }
            if (r == 0) return K_NONE;  // idle timeout
            ssize_t n = read(inFd, &c, 1);
            if (n == 1) break;
            if (n == 0) return K_EOF;
            if (errno != EINTR && errno != EAGAIN) return K_EOF;
        }
        if (c != 27) return c;
        int c1 = readByteTimeout(40);
        if (c1 < 0) return K_ESC;
        if (c1 == '[') {
            std::string seq;
            for (;;) {
                int ch = readByteTimeout(60);
                if (ch < 0) return K_NONE;
                seq += (char)ch;
                if (ch >= 0x40 && ch <= 0x7e) break;
                if (seq.size() > 16) return K_NONE;
            }
            if (seq == "M") return K_NONE;  // legacy X10 mouse: ignore
            if (seq[0] == '<') return parseMouse(seq);  // SGR mouse
            return parseCSI(seq);
        }
        if (c1 == 'O') {
            int ch = readByteTimeout(60);
            switch (ch) {
                case 'A': return K_UP; case 'B': return K_DOWN; case 'C': return K_RIGHT; case 'D': return K_LEFT;
                case 'H': return K_HOME; case 'F': return K_END;
                case 'P': return K_F1; case 'Q': return K_F2; case 'R': return K_F3; case 'S': return K_F4;
                default: return K_NONE;
            }
        }
        if (c1 == 27) return K_ESC;
        return K_ALT | (c1 >= 'A' && c1 <= 'Z' ? c1 + 32 : c1);
    }
    std::string readPaste() {
        std::string buf;
        for (;;) {
            int c = readByteTimeout(3000);
            if (c < 0) break;
            buf.push_back((char)c);
            if (buf.size() >= 6 && buf.compare(buf.size() - 6, 6, "\x1b[201~") == 0) {
                buf.resize(buf.size() - 6);
                break;
            }
        }
        std::string out;
        for (size_t i = 0; i < buf.size(); i++) {
            if (buf[i] == '\r') {
                out += '\n';
                if (i + 1 < buf.size() && buf[i + 1] == '\n') i++;
            } else out += buf[i];
        }
        return out;
    }

    // ---- highlighting cache
    void invalidate(size_t y) { if (hlValid > y) hlValid = y; }
    void lineHl(size_t y, std::vector<uint8_t>& hl) {
        if (hlStart.empty()) { hlStart.emplace_back(); hlValid = 0; }
        while (hlValid < y) {
            HlState st = hlStart[hlValid];
            highlightLine(lang, L[hlValid], st, hl);
            if (hlStart.size() <= hlValid + 1) hlStart.resize(hlValid + 2);
            hlStart[hlValid + 1] = st;
            hlValid++;
        }
        HlState st = hlStart[y];
        highlightLine(lang, L[y], st, hl);
    }
    std::vector<Misspelling> lineSpell(size_t y, const std::vector<uint8_t>& hl) {
        if (!spellOn || !sp || !sp->ok()) return {};
        return findMisspellings(*sp, lang, L[y], hl, false);
    }

    // ---- cursor math
    size_t rxOf(const std::string& s, size_t x) const {
        size_t rx = 0, i = 0, len;
        while (i < x && i < s.size()) {
            if (s[i] == '\t') { rx += (size_t)tabsize - (rx % (size_t)tabsize); i++; continue; }
            rx += (size_t)u8width(s, i, len);
            i += len;
        }
        return rx;
    }
    size_t cxOf(const std::string& s, size_t rx) const {
        size_t cur = 0, i = 0, len;
        while (i < s.size()) {
            size_t w;
            if (s[i] == '\t') { w = (size_t)tabsize - (cur % (size_t)tabsize); len = 1; }
            else w = (size_t)u8width(s, i, len);
            if (cur + w > rx) break;
            cur += w;
            i += len;
        }
        return i;
    }
    void clampCursor() {
        if (L.empty()) L.emplace_back();
        if (cy >= L.size()) cy = L.size() - 1;
        if (cx > L[cy].size()) cx = L[cy].size();
        if (my >= L.size()) my = L.size() - 1;
        if (mx > L[my].size()) mx = L[my].size();
    }
    void scroll() {
        clampCursor();
        size_t tr = textRows();
        if (cy < rowoff) rowoff = cy;
        if (cy >= rowoff + tr) rowoff = cy - tr + 1;
        size_t rx = rxOf(L[cy], cx), tc = textCols();
        if (rx < coloff) coloff = rx;
        if (rx >= coloff + tc) coloff = rx - tc + 1;
    }
    void center(size_t y) {
        size_t tr = textRows();
        if (y < rowoff || y >= rowoff + tr) rowoff = y > tr / 2 ? y - tr / 2 : 0;
    }

    // ---- drawing
    static std::string sgr(uint8_t h, int ov) {
        std::string s = "\x1b[0m";
        if (ov & 4) s += "\x1b[4;38;5;203m";
        else s += hlSgr(h);
        if (ov & 2) s += "\x1b[38;5;16;48;5;220m";
        else if (ov & 1) s += "\x1b[7m";
        return s;
    }
    bool selRange(size_t& sy, size_t& sx, size_t& ey, size_t& ex) const {
        if (!markOn) return false;
        if (my < cy || (my == cy && mx <= cx)) { sy = my; sx = mx; ey = cy; ex = cx; }
        else { sy = cy; sx = cx; ey = my; ex = mx; }
        return !(sy == ey && sx == ex);
    }
    void drawRow(std::string& ab, size_t fy, size_t screenRow) {
        ab += "\x1b[" + std::to_string(screenRow) + ";1H\x1b[0m";
        size_t gw = gutterWidth();
        if (fy >= L.size()) {
            if (gw) ab += "\x1b[38;5;237m" + std::string(gw - 1, ' ') + "~";
            ab += "\x1b[0m\x1b[K";
            return;
        }
        if (gw) {
            char buf[32];
            snprintf(buf, sizeof buf, "%*zu ", (int)gw - 1, fy + 1);
            ab += fy == cy ? "\x1b[38;5;250m" : "\x1b[38;5;239m";
            ab += buf;
        }
        const std::string& s = L[fy];
        std::vector<uint8_t> hl;
        lineHl(fy, hl);
        std::vector<uint8_t> ov(s.size(), 0);
        for (auto& m : lineSpell(fy, hl))
            for (size_t k = m.start; k < m.start + m.len && k < s.size(); k++) ov[k] |= 4;
        if (matchOn && matchY == fy)
            for (size_t k = matchX; k < matchX + matchLen && k < s.size(); k++) ov[k] |= 2;
        size_t sy, sx, ey, ex;
        bool selEol = false;
        if (selRange(sy, sx, ey, ex) && fy >= sy && fy <= ey) {
            size_t a = fy == sy ? sx : 0, b = fy == ey ? ex : s.size();
            for (size_t k = a; k < b && k < s.size(); k++) ov[k] |= 1;
            selEol = fy < ey;
        }
        size_t avail = cols - gw, col = 0, i = 0, len;
        int cur = -1;
        bool truncated = false;
        while (i < s.size()) {
            bool tab = s[i] == '\t';
            size_t w = tab ? (size_t)tabsize - (col % (size_t)tabsize) : (size_t)u8width(s, i, len);
            if (tab) len = 1;
            bool ctrl = !tab && ((unsigned char)s[i] < 0x20 || s[i] == 0x7f);
            if (col + w <= coloff) { col += w; i += len; continue; }
            if (col >= coloff + avail) { truncated = true; break; }
            int attr = hl[i] | (ov[i] << 8);
            if (attr != cur) { ab += sgr(hl[i], ov[i]); cur = attr; }
            if (tab) {
                for (size_t k = std::max(col, coloff); k < col + w && k < coloff + avail; k++) ab += ' ';
            } else if (col < coloff || col + w > coloff + avail) {
                if (col + w > coloff + avail) { truncated = true; break; }
                ab += ' ';
            } else if (ctrl) {
                ab += "\x1b[7m^";
                ab += (char)(s[i] == 0x7f ? '?' : s[i] + '@');
                cur = -1;
            } else ab.append(s, i, len);
            col += w;
            i += len;
        }
        if (selEol && col < coloff + avail) ab += "\x1b[0m\x1b[7m \x1b[0m";
        ab += "\x1b[0m\x1b[K";
        if (truncated)
            ab += "\x1b[" + std::to_string(screenRow) + ";" + std::to_string(cols) + "H\x1b[38;5;244m>\x1b[0m";
    }
    void drawTitle(std::string& ab) {
        std::string name = filename.empty() ? (opt.pipeMode ? "[pipe]" : "[New Buffer]") : filename;
        std::string left = " hed  " + name;
        std::string suffix = std::string(dirty() ? " [+]" : "") + (readOnly ? " [read-only]" : "");
        char right[160];
        snprintf(right, sizeof right, "Ln %zu/%zu Col %zu  %s%s%s ", cy + 1, L.size(), rxOf(L[cy], cx) + 1, langName(lang),
                 spellOn ? "  spell" : "", markOn ? "  MARK" : "");
        std::string r = right;
        size_t lw = displayWidth(left), rw = displayWidth(r), sw = displayWidth(suffix);
        bool cut = false;
        if (lw + sw + rw + 1 > cols) {
            if (rw + sw + 8 < cols) {
                // Shrink the filename, leaving room for a leading ellipsis marker.
                while (displayWidth(left) + sw + rw + 2 > cols && left.size() > 5) { left.erase(u8prev(left, left.size())); cut = true; }
                if (cut) left += "\xe2\x80\xa6";  // …
            } else { r.clear(); rw = 0; }
            lw = displayWidth(left);
        }
        std::string title = left + suffix;
        size_t tw = displayWidth(title);
        // Read-only: draw the whole title row as a solid red bar (white on red).
        ab += "\x1b[1;1H" + std::string(readOnly ? "\x1b[1;37;41m" : "\x1b[0;7m") + title;
        if (tw + rw < cols) ab += std::string(cols - tw - rw, ' ');
        ab += r + "\x1b[0m";
    }
    void drawBottom(std::string& ab) {
        size_t row = textRows() + 2;
        ab += "\x1b[" + std::to_string(row) + ";1H\x1b[0m\x1b[K";
        // Permanent position + undo/redo indicator on the right of the status row.
        std::string right = "Ln " + std::to_string(cy + 1) + ", Col " + std::to_string(rxOf(L[cy], cx) + 1);
        if (!promptOn) right += "  U" + std::to_string(undoS.size()) + " R" + std::to_string(redoS.size());
        size_t rightW = displayWidth(right) + 1;  // + leading space
        std::string m;
        if (promptOn) m = promptStr;
        else if (!msg.empty() && Clock::now() - msgTime < std::chrono::seconds(8)) m = msg;
        if (!m.empty()) {
            size_t maxW = cols > rightW + 1 ? cols - 1 - rightW : 0;
            if (displayWidth(m) > maxW) {
                if (promptOn) {
                    // Prompts: keep the trailing action hints, drop the front.
                    while (displayWidth(m) > maxW) m.erase(0, u8next(m, 0));
                } else {
                    // Messages: keep the beginning, mark truncation with an ellipsis.
                    bool cut = false;
                    while (displayWidth(m) > maxW - 1 && !m.empty()) { m.erase(u8prev(m, m.size())); cut = true; }
                    if (cut) m += "\xe2\x80\xa6";  // …
                }
            }
            ab += promptOn ? "\x1b[1m" + m + "\x1b[0m" : "\x1b[38;5;229m" + m + "\x1b[0m";
        }
        ab += "\x1b[" + std::to_string(row) + ";" + std::to_string(cols - rightW + 1) + "H\x1b[38;5;240m" + right + "\x1b[0m";
        if (!helpRows()) return;
        static const char* h1[][2] = {{"^G", "Help"}, {"^S", "Save"}, {"^W", "Find"}, {"^R", "Replace"},
                                      {"^K", "Cut"}, {"^Z", "Undo"}, {"^_", "Go To"}, {"^T", "Spell"}};
        static const char* h2n[][2] = {{"^X", "Exit"}, {"^O", "Save As"}, {"^N", "Next"}, {"^P", "Prev"},
                                       {"^U", "Paste"}, {"^Y", "Redo"}, {"^^", "Mark"}, {"M-3", "Comment"}};
        static const char* h2p[][2] = {{"^X", "Done"}, {"^Q", "Abort"}, {"^N", "Next"}, {"^P", "Prev"},
                                       {"^U", "Paste"}, {"^Y", "Redo"}, {"^^", "Mark"}, {"M-3", "Comment"}};
        auto line = [&](const char* items[][2], size_t r) {
            ab += "\x1b[" + std::to_string(r) + ";1H\x1b[0m\x1b[K";
            size_t slot = cols / 8, used = 0;
            for (int k = 0; k < 8 && slot >= 4; k++) {
                std::string key = items[k][0], lab = items[k][1];
                size_t keyW = displayWidth(key);
                // A cell needs at least key + space + ellipsis; drop it if too narrow.
                if (slot < keyW + 3) continue;
                size_t maxLab = slot - keyW - 2;  // space + at least the ellipsis
                if (displayWidth(lab) > maxLab) {
                    // Truncate the label and mark it with an ellipsis.
                    size_t cut = 0;
                    while (cut < lab.size() && displayWidth(lab.substr(0, cut)) <= maxLab - 1) cut = u8next(lab, cut);
                    lab = lab.substr(0, cut) + "\xe2\x80\xa6";  // …
                }
                ab += "\x1b[7m" + key + "\x1b[0m " + lab;
                used = keyW + 1 + displayWidth(lab);
                if (used < slot) ab += std::string(slot - used, ' ');
            }
        };
        line(h1, row + 1);
        line(opt.pipeMode ? h2p : h2n, row + 2);
    }
    void refresh() {
        scroll();
        std::string ab;
        ab.reserve(rows * cols * 3);
        ab += "\x1b[?25l";
        drawTitle(ab);
        size_t tr = textRows();
        for (size_t r = 0; r < tr; r++) drawRow(ab, rowoff + r, r + 2);
        drawBottom(ab);
        char buf[48];
        if (promptOn) {
            size_t w = std::min(displayWidth(promptStr), cols - 1);
            snprintf(buf, sizeof buf, "\x1b[%zu;%zuH", tr + 2, w + 1);
        } else {
            snprintf(buf, sizeof buf, "\x1b[%zu;%zuH", cy - rowoff + 2, rxOf(L[cy], cx) - coloff + gutterWidth() + 1);
        }
        ab += buf;
        ab += "\x1b[?25h";
        writeAll(outFd, ab);
    }

    // ---- prompts
    bool prompt(const std::string& label, std::string& buf, const std::function<void(int)>& cb = nullptr) {
        promptOn = true;
        for (;;) {
            promptStr = label + buf;
            refresh();
            int k = readKey();
            if (k == K_RESIZE) { updateSize(); continue; }
            if (k == K_EOF) { promptOn = false; return false; }
            if (k == '\r' || k == '\n') { promptOn = false; return true; }
            if (k == K_ESC || k == CK('c') || k == CK('q') || k == CK('g')) { promptOn = false; return false; }
            if (k == K_BS || k == CK('h')) {
                if (!buf.empty()) buf.erase(u8prev(buf, buf.size()));
                if (cb) cb(0);
                continue;
            }
            if (k == CK('u')) { buf.clear(); if (cb) cb(0); continue; }
            if (k == K_PASTE) {
                std::string p = readPaste();
                buf += p.substr(0, p.find('\n'));
                if (cb) cb(0);
                continue;
            }
            if (k == K_UP || k == K_DOWN || k == CK('n') || k == CK('p') || k == CK('w') || k == K_F3) {
                if (cb) cb(k);
                continue;
            }
            if (k >= 32 && k < 256 && k != 127) {
                buf.push_back((char)k);
                if (cb) cb(0);
            }
        }
    }
    int askKey(const std::string& q) {
        promptOn = true;
        promptStr = q;
        int k;
        do {
            refresh();
            k = readKey();
            if (k == K_RESIZE) updateSize();
        } while (k == K_RESIZE || k == K_NONE);
        promptOn = false;
        return k;
    }

    // ---- undo
    void snapshot(int kind) {
        auto now = Clock::now();
        if (kind != 3 && kind == lastKind && cy == lastLine && now - lastTime < std::chrono::milliseconds(1500)) {
            lastTime = now;
            return;
        }
        undoS.push_back({L, cx, cy, ver});
        lastKind = kind;
        lastLine = cy;
        lastTime = now;
        size_t bytes = 0;
        for (auto& l : L) bytes += l.size() + 1;
        size_t maxDepth = bytes > (4u << 20) ? 10 : bytes > (1u << 20) ? 50 : 500;
        while (undoS.size() > maxDepth) undoS.erase(undoS.begin());
        redoS.clear();
    }
    void modified(size_t fromLine) {
        ver = nextVer++;
        invalidate(fromLine);
    }
    void doUndo() {
        if (undoS.empty()) { setMsg("Nothing to undo"); return; }
        redoS.push_back({L, cx, cy, ver});
        Snap s = std::move(undoS.back());
        undoS.pop_back();
        L = std::move(s.L); cx = s.cx; cy = s.cy; ver = s.ver;
        lastKind = 0; markOn = false; matchOn = false;
        clampCursor(); invalidate(0); center(cy);
        setMsg("Undo (" + std::to_string(undoS.size()) + ")");  // remaining undo depth
    }
    void doRedo() {
        if (redoS.empty()) { setMsg("Nothing to redo"); return; }
        undoS.push_back({L, cx, cy, ver});
        Snap s = std::move(redoS.back());
        redoS.pop_back();
        L = std::move(s.L); cx = s.cx; cy = s.cy; ver = s.ver;
        lastKind = 0; markOn = false; matchOn = false;
        clampCursor(); invalidate(0); center(cy);
        setMsg("Redo (" + std::to_string(redoS.size()) + ")");  // remaining redo depth
    }
    bool editable() {
        if (readOnly) { setMsg("Read-only buffer (opened with --readonly)"); return false; }
        return true;
    }

    // ---- text primitives
    std::string indentUnit() const { return useTabs ? std::string("\t") : std::string((size_t)indentWidth, ' '); }
    void insertText(const std::string& t) {
        auto parts = splitSimple(t, '\n');
        size_t y0 = cy;
        std::string tail = L[cy].substr(cx);
        L[cy] = L[cy].substr(0, cx) + parts[0];
        for (size_t k = 1; k < parts.size(); k++) L.insert(L.begin() + (long)(cy + k), parts[k]);
        size_t lastY = cy + parts.size() - 1;
        cx = L[lastY].size();
        L[lastY] += tail;
        cy = lastY;
        wantRx = -1;
        modified(y0);
    }
    std::string getText(size_t sy, size_t sx, size_t ey, size_t ex) const {
        if (sy == ey) return L[sy].substr(sx, ex - sx);
        std::string t = L[sy].substr(sx) + "\n";
        for (size_t y = sy + 1; y < ey; y++) t += L[y] + "\n";
        return t + L[ey].substr(0, ex);
    }
    void deleteText(size_t sy, size_t sx, size_t ey, size_t ex) {
        if (sy == ey) L[sy].erase(sx, ex - sx);
        else {
            L[sy] = L[sy].substr(0, sx) + L[ey].substr(ex);
            L.erase(L.begin() + (long)sy + 1, L.begin() + (long)ey + 1);
        }
        cy = sy; cx = sx; wantRx = -1;
        modified(sy);
    }
    void selLines(size_t& a, size_t& b) {
        size_t sy, sx, ey, ex;
        if (selRange(sy, sx, ey, ex)) {
            a = sy; b = ey;
            if (ex == 0 && ey > sy) b--;
        } else a = b = cy;
    }

    // ---- editing ops
    void insertByte(char c) {
        if (!editable()) return;
        snapshot(1);
        std::string& s = L[cy];
        if (c == '}' && isBraceLang(lang) && !useTabs && trimRight(s.substr(0, cx)).empty() && cx >= (size_t)indentWidth &&
            trimRight(s).empty()) {
            size_t rm = (size_t)indentWidth;
            s.erase(cx - rm, rm);
            cx -= rm;
        }
        s.insert(cx, 1, c);
        cx++;
        wantRx = -1;
        modified(cy);
    }
    void newline() {
        if (!editable()) return;
        snapshot(3);
        std::string cur = L[cy];
        std::string before = cur.substr(0, cx), after = cur.substr(cx);
        std::string ind = leadingWs(before);
        std::string tb = trimRight(before);
        bool open = false;
        if (lang == L_PYTHON && !tb.empty() && tb.back() == ':') open = true;
        if ((isBraceLang(lang) || lang == L_PYTHON || lang == L_SQL) && !tb.empty() && strchr("{[(", tb.back())) open = true;
        if ((lang == L_BASH) && (endsWith(tb, " then") || endsWith(tb, " do") || tb == "then" || tb == "do")) open = true;
        if (lang == L_FISH) {
            std::string t2 = tb.substr(leadingWs(tb).size());
            for (const char* kw : {"function ", "if ", "for ", "while ", "switch ", "case ", "else"})
                if (startsWith(t2, kw)) open = true;
            if (t2 == "begin") open = true;
        }
        size_t aws = 0;
        while (aws < after.size() && (after[aws] == ' ' || after[aws] == '\t')) aws++;
        after.erase(0, aws);
        if (trimRight(before).empty()) before.clear();
        std::string newInd = ind + (open ? indentUnit() : "");
        L[cy] = before;
        bool closer = open && !after.empty() && strchr("}])", after[0]) && !tb.empty() && strchr("{[(", tb.back());
        if (closer) {
            L.insert(L.begin() + (long)cy + 1, newInd);
            L.insert(L.begin() + (long)cy + 2, ind + after);
        } else L.insert(L.begin() + (long)cy + 1, newInd + after);
        cy++;
        cx = newInd.size();
        wantRx = -1;
        modified(cy - 1);
    }
    void backspace() {
        if (!editable()) return;
        if (cx == 0 && cy == 0) return;
        snapshot(2);
        if (cx > 0) {
            std::string& s = L[cy];
            size_t fnw = s.find_first_not_of(' ');
            if (!useTabs && (fnw == std::string::npos || fnw >= cx) && cx > 1) {
                size_t target = ((cx - 1) / (size_t)indentWidth) * (size_t)indentWidth;
                s.erase(target, cx - target);
                cx = target;
            } else {
                size_t p = u8prev(s, cx);
                s.erase(p, cx - p);
                cx = p;
            }
            modified(cy);
        } else {
            cx = L[cy - 1].size();
            L[cy - 1] += L[cy];
            L.erase(L.begin() + (long)cy);
            cy--;
            modified(cy);
        }
        wantRx = -1;
    }
    void delForward() {
        if (!editable()) return;
        if (cx < L[cy].size()) {
            snapshot(2);
            size_t n = u8next(L[cy], cx);
            L[cy].erase(cx, n - cx);
            modified(cy);
        } else if (cy + 1 < L.size()) {
            snapshot(2);
            L[cy] += L[cy + 1];
            L.erase(L.begin() + (long)cy + 1);
            modified(cy);
        }
    }
    void tab() {
        if (!editable()) return;
        size_t a, b;
        if (markOn && (selLines(a, b), a != b)) {
            snapshot(3);
            for (size_t y = a; y <= b; y++)
                if (!trimRight(L[y]).empty()) L[y] = indentUnit() + L[y];
            modified(a);
            return;
        }
        snapshot(1);
        std::string ins = useTabs ? "\t" : std::string((size_t)indentWidth - rxOf(L[cy], cx) % (size_t)indentWidth, ' ');
        L[cy].insert(cx, ins);
        cx += ins.size();
        wantRx = -1;
        modified(cy);
    }
    void dedentLines() {
        if (!editable()) return;
        size_t a, b;
        selLines(a, b);
        snapshot(3);
        for (size_t y = a; y <= b; y++) {
            std::string& s = L[y];
            size_t rm = 0;
            if (!s.empty() && s[0] == '\t') rm = 1;
            else while (rm < s.size() && rm < (size_t)indentWidth && s[rm] == ' ') rm++;
            s.erase(0, rm);
            if (y == cy) cx = cx >= rm ? cx - rm : 0;
        }
        modified(a);
    }
    void cut() {
        if (!editable()) return;
        size_t sy, sx, ey, ex;
        if (selRange(sy, sx, ey, ex)) {
            snapshot(3);
            clip = getText(sy, sx, ey, ex);
            deleteText(sy, sx, ey, ex);
            markOn = false;
            setMsg("Cut selection");
            return;
        }
        snapshot(3);
        std::string line = L[cy] + "\n";
        clip = cutChain ? clip + line : line;
        if (L.size() == 1) L[0].clear();
        else L.erase(L.begin() + (long)cy);
        clampCursor();
        cx = 0;
        modified(cy > 0 ? cy - 1 : 0);
        markOn = false;
    }
    void copy() {
        size_t sy, sx, ey, ex;
        if (selRange(sy, sx, ey, ex)) { clip = getText(sy, sx, ey, ex); markOn = false; }
        else clip = L[cy] + "\n";
        setMsg("Copied " + std::to_string(std::count(clip.begin(), clip.end(), '\n') + (clip.back() == '\n' ? 0 : 1)) + " line(s)");
    }
    void paste() {
        if (!editable()) return;
        if (clip.empty()) { setMsg("Clipboard is empty"); return; }
        snapshot(3);
        if (!clip.empty() && clip.back() == '\n' && cx != 0 && L[cy].size() == cx) { /* paste lines below */ }
        insertText(clip);
    }
    void pasteBracketed() {
        std::string p = readPaste();
        if (!editable() || p.empty()) return;
        snapshot(3);
        size_t sy, sx, ey, ex;
        if (selRange(sy, sx, ey, ex)) { deleteText(sy, sx, ey, ex); markOn = false; }
        insertText(p);
    }
    void toggleComment() {
        if (!editable()) return;
        size_t a, b;
        selLines(a, b);
        CommentStyle cs = commentStyle(lang);
        bool all = true;
        size_t minInd = (size_t)-1;
        for (size_t y = a; y <= b; y++) {
            const std::string& s = L[y];
            if (trimRight(s).empty()) continue;
            size_t f = leadingWs(s).size();
            minInd = std::min(minInd, f);
            std::string rest = s.substr(f);
            if (!cs.line.empty() ? !startsWith(rest, cs.line) : !(startsWith(rest, cs.open) && endsWith(trimRight(rest), cs.close)))
                all = false;
        }
        if (minInd == (size_t)-1) return;
        snapshot(3);
        for (size_t y = a; y <= b; y++) {
            std::string& s = L[y];
            if (trimRight(s).empty()) continue;
            size_t f = leadingWs(s).size();
            if (all) {
                if (!cs.line.empty()) {
                    size_t n = cs.line.size() + (f + cs.line.size() < s.size() && s[f + cs.line.size()] == ' ' ? 1 : 0);
                    s.erase(f, n);
                } else {
                    s = trimRight(s);
                    size_t n = cs.open.size() + (f + cs.open.size() < s.size() && s[f + cs.open.size()] == ' ' ? 1 : 0);
                    s.erase(f, n);
                    size_t cl = cs.close.size();
                    s.erase(s.size() - cl);
                    s = trimRight(s);
                }
            } else {
                if (!cs.line.empty()) s.insert(minInd, cs.line + " ");
                else { s.insert(minInd, cs.open + " "); s += " " + cs.close; }
            }
        }
        clampCursor();
        modified(a);
        size_t counted = 0;
        for (size_t y = a; y <= b; y++) if (!trimRight(L[y]).empty()) counted++;
        setMsg(std::string(all ? "Uncommented" : "Commented") + " lines " + std::to_string(a + 1) + "-" +
               std::to_string(b + 1) + " (" + std::to_string(counted) + " lines)");
    }
    void moveLine(int dir) {
        if (!editable()) return;
        if ((dir < 0 && cy == 0) || (dir > 0 && cy + 1 >= L.size())) return;
        snapshot(3);
        size_t other = dir < 0 ? cy - 1 : cy + 1;
        std::swap(L[cy], L[other]);
        cy = other;
        modified(std::min(cy, other) > 0 ? std::min(cy, other) - 1 : 0);
        invalidate(0);
    }
    void dupLine() {
        if (!editable()) return;
        snapshot(3);
        L.insert(L.begin() + (long)cy + 1, L[cy]);
        cy++;
        modified(cy - 1);
    }

    // ---- movement
    void left() {
        if (cx > 0) cx = u8prev(L[cy], cx);
        else if (cy > 0) { cy--; cx = L[cy].size(); }
        wantRx = -1;
    }
    void right() {
        if (cx < L[cy].size()) cx = u8next(L[cy], cx);
        else if (cy + 1 < L.size()) { cy++; cx = 0; }
        wantRx = -1;
    }
    void vert(long delta) {
        if (wantRx < 0) wantRx = (long)rxOf(L[cy], cx);
        long ny = (long)cy + delta;
        if (ny < 0) ny = 0;
        if (ny >= (long)L.size()) ny = (long)L.size() - 1;
        cy = (size_t)ny;
        cx = cxOf(L[cy], (size_t)wantRx);
    }
    static bool wordCh(char c) { return isalnum((unsigned char)c) || c == '_' || (unsigned char)c >= 0x80; }
    void wordRight() {
        const std::string& s = L[cy];
        if (cx >= s.size()) { right(); return; }
        while (cx < s.size() && !wordCh(s[cx])) cx++;
        while (cx < s.size() && wordCh(s[cx])) cx++;
        wantRx = -1;
    }
    void wordLeft() {
        if (cx == 0) { left(); return; }
        const std::string& s = L[cy];
        while (cx > 0 && !wordCh(s[cx - 1])) cx--;
        while (cx > 0 && wordCh(s[cx - 1])) cx--;
        wantRx = -1;
    }
    void home() {
        size_t f = leadingWs(L[cy]).size();
        cx = (cx == f) ? 0 : f;
        wantRx = -1;
    }

    // ---- search
    bool findIn(const std::string& q, bool icase, size_t y, size_t x, size_t& fx) {
        std::string s = icase ? toLower(L[y]) : L[y];
        if (x > s.size()) return false;
        size_t p = s.find(q, x);
        if (p == std::string::npos) return false;
        fx = p;
        return true;
    }
    bool searchFrom(const std::string& qIn, size_t y, size_t x, bool fwd, size_t& fy, size_t& fx, bool& wrapped) {
        bool icase = !hasUpper(qIn);
        std::string q = icase ? toLower(qIn) : qIn;
        size_t n = L.size();
        wrapped = false;
        if (fwd) {
            for (size_t k = 0; k <= n; k++) {
                size_t i = (y + k) % n;
                if (y + k >= n) wrapped = true;
                size_t start = k == 0 ? x : 0;
                size_t p;
                if (findIn(q, icase, i, start, p)) {
                    if (k == n && p >= x) return false;
                    fy = i; fx = p;
                    return true;
                }
            }
        } else {
            for (size_t k = 0; k <= n; k++) {
                size_t i = (y + n - (k % n)) % n;
                if (k > y) wrapped = true;
                std::string s = icase ? toLower(L[i]) : L[i];
                size_t p;
                if (k == 0) {
                    if (x == 0) continue;
                    p = s.rfind(q, x - 1);
                } else p = s.rfind(q);
                if (p != std::string::npos) {
                    if (k == n && p <= x) return false;
                    fy = i; fx = p;
                    return true;
                }
            }
        }
        return false;
    }
    void jumpMatch(size_t y, size_t x, size_t len) {
        cy = y; cx = x; wantRx = -1;
        matchOn = true; matchY = y; matchX = x; matchLen = len;
        center(y);
    }
    // Count every occurrence of q across the whole buffer (overlapping matches
    // count, matching how the live search walks the text).
    size_t countMatches(const std::string& q, bool icase) const {
        std::string nq = icase ? toLower(q) : q;
        if (nq.empty()) return 0;
        size_t total = 0;
        for (auto& s : L) {
            std::string h = icase ? toLower(s) : s;
            size_t p = 0;
            while ((p = h.find(nq, p)) != std::string::npos) { total++; p++; }
        }
        return total;
    }
    // 1-based index of the match at (y, x) in line-major order.
    size_t matchIndex(size_t y, size_t x, const std::string& q, bool icase) const {
        std::string nq = icase ? toLower(q) : q;
        size_t idx = 0;
        for (size_t i = 0; i <= y && i < L.size(); i++) {
            std::string h = icase ? toLower(L[i]) : L[i];
            size_t p = 0;
            while ((p = h.find(nq, p)) != std::string::npos) {
                if (i < y || p <= x) idx++;
                else break;
                p++;
            }
            if (i == y) break;
        }
        return idx;
    }
    // Status message "match X of Y" for the match at (y, x); stores the total.
    void setMatchMsg(size_t y, size_t x, const std::string& q, bool wrapped) {
        bool icase = !hasUpper(q);
        matchTotal = countMatches(q, icase);
        matchIdx = matchIndex(y, x, q, icase);
        setMsg("match " + std::to_string(matchIdx) + " of " + std::to_string(matchTotal) +
               (wrapped ? " (wrapped)" : ""));
    }
    void find() {
        size_t oy = cy, ox = cx, orow = rowoff, ocol = coloff;
        std::string buf;
        std::string label = lastSearch.empty() ? "Search: " : "Search [" + lastSearch + "]: ";
        auto cb = [&](int k) {
            const std::string& q = buf.empty() ? lastSearch : buf;
            if (q.empty()) { matchOn = false; return; }
            size_t fy, fx;
            bool w, ok;
            if (k == 0) ok = searchFrom(q, oy, ox, true, fy, fx, w);
            else {
                bool fwd = !(k == K_UP || k == CK('p'));
                size_t sy = matchOn ? matchY : cy, sx = matchOn ? matchX : cx;
                ok = fwd ? searchFrom(q, sy, sx + 1, true, fy, fx, w) : searchFrom(q, sy, sx, false, fy, fx, w);
            }
            if (ok) { jumpMatch(fy, fx, q.size()); setMatchMsg(fy, fx, q, w); }
            else { matchOn = false; cy = oy; cx = ox; setMsg("Not found: " + q); }
        };
        if (!prompt(label, buf, cb)) {
            cy = oy; cx = ox; rowoff = orow; coloff = ocol;
            matchOn = false;
            setMsg("Search cancelled");
            return;
        }
        if (!buf.empty()) lastSearch = buf;
        if (lastSearch.empty()) return;
        if (!matchOn) {
            size_t fy, fx;
            bool w;
            if (searchFrom(lastSearch, oy, buf.empty() ? ox + 1 : ox, true, fy, fx, w)) jumpMatch(fy, fx, lastSearch.size());
            else setMsg("\"" + lastSearch + "\" not found");
        }
        if (matchOn) setMatchMsg(matchY, matchX, lastSearch, false);  // keep "match X of Y" after Enter
    }
    void findNext(bool fwd) {
        if (lastSearch.empty()) { find(); return; }
        size_t fy, fx;
        bool w;
        bool ok = fwd ? searchFrom(lastSearch, cy, cx + 1, true, fy, fx, w) : searchFrom(lastSearch, cy, cx, false, fy, fx, w);
        if (ok) { jumpMatch(fy, fx, lastSearch.size()); setMatchMsg(fy, fx, lastSearch, w); }
        else setMsg("\"" + lastSearch + "\" not found");
    }
    // Build a short context snippet around the match at (y, x) for the replace
    // confirmation prompt, e.g.  "ln 5:  ...pre [MATCH] post..."
    std::string matchContext(size_t y, size_t x, size_t len) const {
        const std::string& s = L[y];
        size_t xs = std::min(x, s.size());
        std::string pre = s.substr(0, xs);
        std::string hit = s.substr(xs, std::min(len, s.size() - xs));
        std::string post = s.substr(std::min(xs + len, s.size()));
        const size_t maxPre = 14, maxPost = 12, maxHit = 24;
        std::string out;
        if (displayWidth(pre) > maxPre) {
            while (displayWidth(pre) > maxPre) pre.erase(0, u8next(pre, 0));
            out = "\xe2\x80\xa6" + pre;  // …
        } else out = pre;
        if (displayWidth(hit) > maxHit) {
            size_t c = 0;
            while (c < hit.size() && displayWidth(hit.substr(0, c)) <= maxHit - 1) c = u8next(hit, c);
            hit = hit.substr(0, c) + "\xe2\x80\xa6";
        }
        out += "[" + hit + "]";
        if (displayWidth(post) > maxPost) {
            size_t c = 0;
            while (c < post.size() && displayWidth(post.substr(0, c)) <= maxPost - 1) c = u8next(post, c);
            out += post.substr(0, c) + "\xe2\x80\xa6";
        } else out += post;
        return "ln " + std::to_string(y + 1) + ": \"" + out + "\"";
    }
    void replace() {
        if (!editable()) return;
        std::string q;
        if (!prompt(lastSearch.empty() ? "Replace: " : "Replace [" + lastSearch + "]: ", q)) { setMsg("Cancelled"); return; }
        if (q.empty()) q = lastSearch;
        if (q.empty()) return;
        lastSearch = q;
        std::string with;
        if (!prompt("Replace \"" + q + "\" with: ", with)) { setMsg("Cancelled"); return; }
        bool icase = !hasUpper(q);
        std::string nq = icase ? toLower(q) : q;
        size_t total = countMatches(q, icase);
        snapshot(3);
        size_t sy = cy, sx = cx, y = cy, x = cx;
        bool all = false, phase2 = false;
        int count = 0;
        for (;;) {
            size_t fy = 0, fx = 0;
            bool found = false;
            for (size_t i = y; i < L.size(); i++) {
                if (findIn(nq, icase, i, i == y ? x : 0, fx)) { fy = i; found = true; break; }
            }
            if (!found) {
                if (!phase2) { phase2 = true; y = 0; x = 0; continue; }
                break;
            }
            if (phase2 && (fy > sy || (fy == sy && fx >= sx))) break;
            jumpMatch(fy, fx, q.size());
            int k = 'y';
            if (!all) {
                size_t idx = matchIndex(fy, fx, q, icase);
                std::string prompt = "Replace this instance?  (" + std::to_string(idx) + " of " +
                                     std::to_string(total) + ")  " + matchContext(fy, fx, q.size()) +
                                     "   Y Yes  N No  A All from here  Esc Cancel";
                k = askKey(prompt);
            }
            if (k == 'a' || k == 'A') { all = true; k = 'y'; }
            if (k == 'y' || k == 'Y') {
                L[fy].replace(fx, q.size(), with);
                count++;
                modified(fy);
                if (phase2 && fy == sy) sx = sx + with.size() - q.size();
                y = fy;
                x = fx + with.size();
            } else if (k == 'n' || k == 'N') {
                y = fy;
                x = fx + 1;
            } else break;
        }
        matchOn = false;
        if (count == 0 && !undoS.empty()) undoS.pop_back();
        setMsg("Replaced " + std::to_string(count) + " occurrence" + (count == 1 ? "" : "s"));
    }
    void gotoLine() {
        std::string buf;
        if (!prompt("Go to line[:column] (negative counts from end): ", buf) || buf.empty()) return;
        for (auto& c : buf)
            if (c == ',') c = ':';
        auto parts = splitSimple(buf, ':');
        long ln = atol(parts[0].c_str()), co = parts.size() > 1 ? atol(parts[1].c_str()) : 1;
        if (ln < 0) ln = (long)L.size() + ln + 1;
        if (ln < 1) ln = 1;
        if (ln > (long)L.size()) ln = (long)L.size();
        cy = (size_t)ln - 1;
        cx = cxOf(L[cy], co > 0 ? (size_t)co - 1 : 0);
        wantRx = -1;
        center(cy);
    }

    // ---- spell
    bool nextMisspelling(size_t y, size_t x, size_t& fy, Misspelling& out) {
        size_t n = L.size();
        std::vector<uint8_t> hl;
        for (size_t k = 0; k <= n; k++) {
            size_t i = (y + k) % n;
            lineHl(i, hl);
            for (auto& m : findMisspellings(*sp, lang, L[i], hl, false)) {
                if (k == 0 && m.start < x) continue;
                if (k == n && m.start >= x) return false;
                fy = i;
                out = m;
                return true;
            }
        }
        return false;
    }
    void spellWalk() {
        if (!sp || !sp->ensureLoaded()) { setMsg("Spell checker unavailable: " + (sp ? sp->error() : std::string())); return; }
        spellOn = true;
        size_t y = cy, x = cx;
        int fixed = 0;
        for (;;) {
            size_t fy = 0;
            Misspelling m;
            if (!nextMisspelling(y, x, fy, m)) {
                matchOn = false;
                setMsg(fixed ? "Spell check done (" + std::to_string(fixed) + " changed)" : "No misspellings found");
                return;
            }
            jumpMatch(fy, m.start, m.len);
            auto sug = sp->suggest(m.word, 6);  // show at most 6 suggestions
            std::string q = "\"" + m.word + "\": ";
            for (size_t k = 0; k < sug.size(); k++) q += std::to_string(k + 1) + ":" + sug[k] + "  ";
            if (sug.empty()) q += "(no suggestions)  ";
            q += "| a add  i ignore  e edit  n next  Esc stop";
            int k = askKey(q);
            if (k >= '1' && k <= '9' && (size_t)(k - '1') < sug.size() && editable()) {
                snapshot(3);
                L[fy].replace(m.start, m.len, sug[(size_t)(k - '1')]);
                modified(fy);
                fixed++;
                y = fy;
                x = m.start + sug[(size_t)(k - '1')].size();
            } else if (k == 'a') {
                std::string err;
                if (!sp->addUserWord(m.word, err)) setMsg("Could not save word: " + err);
                y = fy; x = m.start + m.len;
            } else if (k == 'i') {
                sp->ignore(m.word);
                y = fy; x = m.start + m.len;
            } else if (k == 'e' && editable()) {
                std::string w = m.word;
                if (prompt("Replace \"" + m.word + "\" with: ", w) && w != m.word) {
                    snapshot(3);
                    L[fy].replace(m.start, m.len, w);
                    modified(fy);
                    fixed++;
                }
                y = fy; x = m.start + w.size();
            } else if (k == 'n' || k == ' ' || k == CK('t')) {
                y = fy; x = m.start + m.len;
            } else {
                matchOn = false;
                setMsg("Spell check stopped");
                return;
            }
            invalidate(0);
        }
    }

    // ---- help
    void help() {
        static const char* text[] = {
            "hed - nano-style editor. ^ = Ctrl, M- = Alt (or Esc then key).",
            "",
            "FILE      ^S save      ^O save as      ^X exit (asks to save)     ^Q quit / abort",
            "SEARCH    ^W or ^F find (live; Up/Down or ^N/^P in the prompt cycle matches)",
            "          ^N next match     ^P previous match     ^R or ^\\ replace (y/n/a)",
            "          Search is smart-case: lowercase = case-insensitive.",
            "MOVE      arrows, Home/End (Home toggles indent/col 0), PgUp/PgDn, ^A/^E line start/end",
            "          Ctrl+Left/Right word, Ctrl+Home/End or M-\\ / M-/ top/bottom, ^_ or ^L or M-G go to line",
            "EDIT      ^K cut line (repeat to collect lines) or cut selection, ^U paste, M-6 copy",
            "          ^^ (Ctrl+6) or M-A set/clear mark to select, Tab/Shift+Tab indent/outdent",
            "          ^Z undo  ^Y redo (also M-U / M-E), ^D delete, M-D duplicate line",
            "          M-Up/M-Down move line, M-3 toggle comment on line/selection",
            "SPELL     ^T or F7 walk misspellings: 1-6 pick, a add to dictionary, i ignore, e edit",
            "          M-S toggle misspelling underline (comments/strings in code, all text in prose)",
            "VIEW      M-N toggle line numbers, mouse: click moves cursor, wheel scrolls, Esc clear mark/highlight",
            "",
            "Pipe mode (cmd | hed | cmd2):  ^X finishes and writes the buffer to stdout, ^Q aborts (exit 1).",
            "Config: ~/.config/hed/config (see `hed config`); status log: `hed log`.",
            "Personal dictionary: ~/.config/hed/words.txt",
            "",
            "Press any key to return.",
        };
        std::string ab = "\x1b[?25l\x1b[H\x1b[2J\x1b[0m";
        size_t r = 1;
        for (auto t : text) {
            if (r > rows) break;
            std::string s = t;
            if (s.size() > cols) s.resize(cols);
            ab += "\x1b[" + std::to_string(r) + ";1H" + (r == 1 ? "\x1b[1m" + s + "\x1b[0m" : s);
            r++;
        }
        writeAll(outFd, ab);
        int k;
        do { k = readKey(); if (k == K_RESIZE) updateSize(); } while (k == K_RESIZE || k == K_NONE);
    }

    // ---- files
    bool save(bool askName) {
        std::string name = filename;
        if (name.empty() || askName) {
            if (!prompt("File name to write: ", name) || name.empty()) { setMsg("Save cancelled"); return false; }
            if (name != filename && pathExists(name)) {
                int k = askKey("\"" + name + "\" exists. Overwrite?  Y Yes  N No");
                if (k != 'y' && k != 'Y') { setMsg("Save cancelled"); return false; }
            }
        }
        Text t;
        t.lines = L;
        t.trailingNewline = trailingNl;
        t.crlf = crlf;
        std::string err;
        if (!atomicWrite(name, joinText(t), err)) { setMsg("Error: " + err); return false; }
        if (filename != name) {
            filename = name;
            if (!opt.langSet && lang == L_TEXT) {
                lang = detectLang(filename, L.empty() ? "" : L[0]);
                hlStart.clear();
                hlValid = 0;
            }
        }
        readOnly = false;
        savedVer = ver;
        setMsg("Wrote " + std::to_string(L.size()) + " line" + (L.size() == 1 ? "" : "s") + " to " + filename);
        return true;
    }
    void exitEditor() {
        if (opt.pipeMode && filename.empty()) { emitOnExit = true; quitFlag = true; return; }
        if (dirty()) {
            int k = askKey("Save modified buffer?   Y Yes   N No   Esc Cancel");
            if (k == 'y' || k == 'Y') { if (save(false)) quitFlag = true; }
            else if (k == 'n' || k == 'N') quitFlag = true;
            else setMsg("Cancelled");
            return;
        }
        quitFlag = true;
    }
    void abortEditor() {
        if (opt.pipeMode && filename.empty()) { exitCode = 1; quitFlag = true; return; }
        if (dirty()) {
            int k = askKey("Discard unsaved changes and quit?   Y Yes   N No");
            if (k != 'y' && k != 'Y') { setMsg("Cancelled"); return; }
        }
        quitFlag = true;
    }
    void detectIndent() {
        indentWidth = opt.indentWidth > 0 ? opt.indentWidth : defaultIndent(lang);
        if (opt.indentMode == 1) { useTabs = true; return; }
        if (opt.indentMode == 2) { useTabs = false; return; }
        size_t tabs = 0, spaces = 0, two = 0, four = 0;
        for (size_t i = 0; i < L.size() && i < 2000; i++) {
            const std::string& s = L[i];
            if (s.empty()) continue;
            if (s[0] == '\t') tabs++;
            else if (s[0] == ' ') {
                spaces++;
                size_t n = leadingWs(s).size();
                if (n % 4 == 0) four++;
                else if (n % 2 == 0) two++;
            }
        }
        useTabs = tabs > spaces;
        if (!useTabs && opt.indentWidth == 0 && spaces > 0) indentWidth = two > 0 && two * 4 >= spaces ? 2 : (four ? 4 : indentWidth);
        if (lang == L_BASH || lang == L_FISH) { if (!spaces && tabs) useTabs = true; }
    }

    void process(int k) {
        bool keepChain = false;
        switch (k) {
            case K_NONE: break;
            case K_RESIZE: updateSize(); break;
            case K_EOF: quitFlag = true; exitCode = 1; break;
            case CK('x'): exitEditor(); break;
            case CK('q'): abortEditor(); break;
            case CK('s'): save(false); break;
            case CK('o'): save(true); break;
            case CK('g'): case K_F1: help(); break;
            case CK('w'): case CK('f'): find(); break;
            case CK('n'): case K_F3: findNext(true); break;
            case CK('p'): findNext(false); break;
            case CK('r'): case 0x1c: replace(); break;
            case CK('k'): cut(); keepChain = true; break;
            case CK('u'): paste(); break;
            case K_ALT | '6': case K_ALT | '^': copy(); break;
            case 0x1e: case K_ALT | 'a': markOn = !markOn; mx = cx; my = cy; setMsg(markOn ? "Mark set" : "Mark unset"); break;
            case CK('z'): case K_ALT | 'u': doUndo(); break;
            case CK('y'): case K_ALT | 'e': doRedo(); break;
            case 0x1f: case CK('l'): case K_ALT | 'g': gotoLine(); break;
            case CK('t'): case K_F7: spellWalk(); break;
            case K_ALT | 's':
                if (!spellOn && sp && !sp->ensureLoaded()) { setMsg("Spell checker unavailable"); break; }
                spellOn = !spellOn;
                setMsg(spellOn ? "Spell highlighting on" : "Spell highlighting off");
                break;
            case K_ALT | 'n': showNumbers = !showNumbers; break;
            case K_ALT | '3': case K_ALT | '#': toggleComment(); break;
            case K_ALT | 'd': dupLine(); break;
            case K_AUP: moveLine(-1); break;
            case K_ADOWN: moveLine(1); break;
            case K_MLCLICK: {
                // SGR mouse click: move the cursor to the clicked cell. Row 1 is
                // the title bar, so text rows start at screen row 2; the gutter
                // occupies the leftmost columns.
                if (mouseRow >= 2 && mouseRow < (int)(2 + textRows())) {
                    size_t g = gutterWidth();
                    int c = mouseCol - 1;  // 0-based screen column
                    size_t tc = textCols();
                    if (c >= (int)g && (size_t)c < g + tc) {
                        size_t y = rowoff + (size_t)(mouseRow - 2);
                        if (y >= L.size()) y = L.size() - 1;
                        size_t rx = (size_t)c - g + coloff;
                        cx = cxOf(L[y], rx);
                        cy = y;
                        wantRx = -1;
                    }
                }
                break;
            }
            case K_MWHEELUP: {
                rowoff = rowoff > 3 ? rowoff - 3 : 0;
                if (cy < rowoff) rowoff = cy;
                break;
            }
            case K_MWHEELDOWN: {
                size_t tr = textRows();
                size_t max = L.size() > tr ? L.size() - tr : 0;
                rowoff = rowoff + 3 < max ? rowoff + 3 : max;
                if (cy >= rowoff + tr) rowoff = cy - tr + 1;
                break;
            }
            case CK('a'): case K_HOME: home(); break;
            case CK('e'): case K_END: cx = L[cy].size(); wantRx = -1; break;
            case K_CHOME: case K_ALT | '\\': cy = 0; cx = 0; wantRx = -1; break;
            case K_CEND: case K_ALT | '/': cy = L.size() - 1; cx = L[cy].size(); wantRx = -1; break;
            case K_LEFT: left(); break;
            case K_RIGHT: right(); break;
            case K_UP: case K_CUP: vert(-1); break;
            case K_DOWN: case K_CDOWN: vert(1); break;
            case K_CLEFT: wordLeft(); break;
            case K_CRIGHT: wordRight(); break;
            case K_PGUP: {
                size_t tr = textRows();
                rowoff = rowoff > tr ? rowoff - tr : 0;
                vert(-(long)tr);
                break;
            }
            case K_PGDN: {
                size_t tr = textRows();
                if (rowoff + tr < L.size()) rowoff += tr;
                vert((long)tr);
                break;
            }
            case CK('d'): case K_DEL: delForward(); break;
            case K_BS: case CK('h'): backspace(); break;
            case '\r': case '\n': newline(); break;
            case '\t': tab(); break;
            case K_STAB: dedentLines(); break;
            case K_PASTE: pasteBracketed(); break;
            case K_ESC: markOn = false; matchOn = false; msg.clear(); break;
            default:
                if (k >= 32 && k < 256 && k != 127) insertByte((char)k);
        }
        if (!keepChain) cutChain = false;
        else cutChain = true;
    }

    // ---- swap file / crash recovery
    // Write the current buffer to the swap file. Called on start and then every
    // ~30s while the buffer is dirty, so a crash loses at most 30s of edits.
    void writeSwapFile() {
        if (swapPath.empty()) return;
        Text t;
        t.lines = L;
        t.trailingNewline = trailingNl;
        t.crlf = crlf;
        std::string err;
        atomicWrite(swapPath, joinText(t), err);  // best effort; ignore failures
        lastSwapWrite = Clock::now();
    }
    // Called from the main loop on idle timeouts while the buffer is dirty.
    void maybeSwapSave() {
        if (swapPath.empty() || !dirty()) return;
        if (Clock::now() - lastSwapWrite < std::chrono::seconds(30)) return;
        writeSwapFile();
    }
    // Set up the swap file for a named (non-pipe) buffer and decide whether to
    // offer crash recovery. Returns true if a recoverable swap file exists.
    bool setupSwap() {
        if (filename.empty() || opt.pipeMode) return false;
        std::string sdir = configDir() + "/swap";
        std::string err;
        if (!mkdirParents(sdir, err)) return false;
        swapPath = sdir + "/" + baseName(filename) + ".swp";
        long origMt = pathExists(filename) ? fileMtime(filename) : -1;
        long swMt = pathExists(swapPath) ? fileMtime(swapPath) : -1;
        if (swMt >= 0 && origMt >= 0 && swMt <= origMt) {
            unlink(swapPath.c_str());  // stale swap (older than the file): discard
            swMt = -1;
        }
        if (swMt >= 0) return true;  // newer swap file -> offer recovery
        writeSwapFile();             // fresh session: create the swap file now
        return false;
    }
    // Load the swap file's content into the buffer (crash recovery).
    void recoverFromSwap() {
        std::string data, err;
        if (!readFile(swapPath, data, err)) { setMsg("Could not read swap file: " + err); return; }
        Text t = splitText(data);
        L = t.lines;
        trailingNl = t.trailingNewline;
        crlf = t.crlf;
        clampCursor();
        invalidate(0);
        setMsg("Recovered from swap file (press ^S to save)");
    }

  public:
    int runImpl() {
        setTheme(opt.theme);   // syntax palette from config / --theme
        maxLog = opt.logSize;  // status-log ring size (0 = unlimited)
        std::string content;
        bool fromFile = false;
        if (!opt.file.empty() && opt.file != "-") {
            filename = opt.file;
            if (pathExists(filename)) {
                if (isDir(filename)) { fprintf(stderr, "hed: %s is a directory\n", filename.c_str()); return 2; }
                std::string err;
                if (!readFile(filename, content, err)) { fprintf(stderr, "hed: %s\n", err.c_str()); return 2; }
                fromFile = true;
                if (access(filename.c_str(), W_OK) != 0) setMsg("Warning: file is not writable; use ^O to save elsewhere");
            } else setMsg("New file");
        } else if (opt.pipeMode) content = opt.initial;
        if (!content.empty() || fromFile) {
            Text t = splitText(content);
            L = t.lines;
            trailingNl = t.trailingNewline || content.empty();
            crlf = t.crlf;
        }
        if (content.find('\0') != std::string::npos) setMsg("Warning: file contains NUL bytes (binary?)");
        readOnly = opt.readOnly;
        lang = opt.langSet ? opt.lang : detectLang(filename, content);
        tabsize = opt.tabsize > 0 ? opt.tabsize : 4;
        showNumbers = opt.numbers;
        detectIndent();
        sp = &Speller::instance();
        spellOn = opt.spell && sp->ensureLoaded();
        if (opt.line != 0) {
            long ln = opt.line < 0 ? (long)L.size() + opt.line + 1 : opt.line;
            if (ln < 1) ln = 1;
            if (ln > (long)L.size()) ln = (long)L.size();
            cy = (size_t)ln - 1;
            if (opt.col > 0) cx = cxOf(L[cy], (size_t)opt.col - 1);
        }
        if (!isatty(0) || !isatty(1) || opt.pipeMode) {
            int fd = open("/dev/tty", O_RDWR | O_CLOEXEC);
            if (fd < 0) {
                fprintf(stderr, "hed: interactive editor needs a terminal (/dev/tty unavailable).\n"
                                "Use a one-shot command instead, e.g.:  hed write FILE ...  |  hed show FILE  |  hed replace FILE OLD NEW\n");
                return 2;
            }
            inFd = outFd = fd;
        }
        if (!isatty(inFd)) {
            fprintf(stderr, "hed: interactive editor needs a terminal. For scripts/agents use: hed write|replace|insert|show ...\n");
            return 2;
        }
        if (!enableRaw()) { fprintf(stderr, "hed: cannot set terminal raw mode\n"); return 2; }
        atexit(restoreTerm);
        struct sigaction sa;
        memset(&sa, 0, sizeof sa);
        sa.sa_handler = onWinch;
        sigaction(SIGWINCH, &sa, nullptr);
        signal(SIGTERM, onFatal);
        signal(SIGHUP, onFatal);
        signal(SIGSEGV, onFatal);
        signal(SIGABRT, onFatal);
        updateSize();
        if (opt.line != 0) center(cy);
        if (msg.empty()) setMsg("^G help  ^X " + std::string(opt.pipeMode && filename.empty() ? "done (writes to stdout)" : "exit") +
                                "  ^S save  ^W find  ^T spell");
        // Crash recovery: if a swap file newer than the original exists, offer
        // to restore it before the user starts editing.
        if (setupSwap()) {
            setMsg("Swap file found, press R to recover, any other key to discard");
            int k;
            do { refresh(); k = readKey(); if (k == K_RESIZE) updateSize(); } while (k == K_RESIZE || k == K_NONE);
            if (k == 'r' || k == 'R') recoverFromSwap();
            else { unlink(swapPath.c_str()); writeSwapFile(); setMsg("Swap file discarded"); }
        }
        refresh();
        while (!quitFlag) {
            // While dirty, poll with a 1s timeout so we can flush the swap file
            // every ~30s even when the user is idle. Clean buffers block forever.
            int k = readKey(dirty() ? 1000 : -1);
            if (k == K_NONE) { maybeSwapSave(); continue; }
            process(k);
            refresh();  // redraw immediately so status messages appear right away
        }
        restoreTerm();
        if (!swapPath.empty()) unlink(swapPath.c_str());  // clean exit: remove swap
        if (inFd != 0) close(inFd);
        if (emitOnExit) {
            Text t;
            t.lines = L;
            t.trailingNewline = trailingNl;
            t.crlf = crlf;
            writeAll(1, joinText(t));
        }
        return exitCode;
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// Configuration file handling.
//
// The config file is a small INI-style file with a single [editor] section.
// We parse it leniently: blank lines and lines starting with '#' or ';' are
// comments; a `key = value` line is applied to the Config struct. Keys are
// only honoured inside the [editor] section (or at the top of the file, which
// we treat as [editor] for convenience). Unknown keys and malformed values are
// ignored so a hand-edited file can never crash the editor.
//
// Precedence (highest first): command-line flags > HED_* env vars > config
// file > built-in defaults. loadConfig() applies defaults, then the file, then
// the environment; the caller (cmdEdit) lets explicit CLI flags win last.
// ---------------------------------------------------------------------------
namespace {

// Parse a boolean value leniently: true/false, yes/no, on/off, 1/0.
bool cfgBool(const std::string& v, bool& out) {
    std::string s = toLower(v);
    if (s == "true" || s == "yes" || s == "on" || s == "1") { out = true; return true; }
    if (s == "false" || s == "no" || s == "off" || s == "0") { out = false; return true; }
    return false;
}

// Parse an integer within [lo, hi].
bool cfgInt(const std::string& v, long lo, long hi, long& out) {
    if (v.empty()) return false;
    char* e = nullptr;
    errno = 0;
    long n = strtol(v.c_str(), &e, 10);
    if (errno || !e || *e || n < lo || n > hi) return false;
    out = n;
    return true;
}

// Apply one `key = value` pair to a Config. Returns false if the key is
// unknown or the value is malformed (caller decides whether to ignore).
bool cfgApply(Config& c, const std::string& key, const std::string& val) {
    long n;
    if (key == "tabsize") return cfgInt(val, 1, 32, n) ? (c.tabsize = (int)n, true) : false;
    if (key == "indent") {
        std::string s = toLower(val);
        if (s == "auto" || s == "0") { c.indent = 0; return true; }
        if (s == "tabs" || s == "1") { c.indent = 1; return true; }
        if (s == "spaces" || s == "2") { c.indent = 2; return true; }
        return false;
    }
    if (key == "spaces") return cfgInt(val, 1, 16, n) ? (c.spaces = (int)n, true) : false;
    if (key == "spell") return cfgBool(val, c.spell);
    if (key == "numbers") return cfgBool(val, c.numbers);
    if (key == "theme") {
        std::string s = toLower(val);
        if (!s.empty()) { c.theme = s; return true; }  // setTheme() validates + falls back with a warning
        return false;
    }
    if (key == "log_size") return cfgInt(val, 0, 1 << 30, n) ? (c.logSize = (size_t)n, true) : false;
    return false;  // unknown key
}

// Overlay HED_* environment variables onto a Config (they beat the file but
// lose to command-line flags). Invalid values are silently ignored.
void cfgApplyEnv(Config& c) {
    const char* v;
    if ((v = getenv("HED_TABSIZE"))) { long n; if (cfgInt(v, 1, 32, n)) c.tabsize = (int)n; }
    if ((v = getenv("HED_INDENT"))) { std::string s = toLower(v); if (s == "auto" || s == "0") c.indent = 0; else if (s == "tabs" || s == "1") c.indent = 1; else if (s == "spaces" || s == "2") c.indent = 2; }
    if ((v = getenv("HED_SPACES"))) { long n; if (cfgInt(v, 1, 16, n)) c.spaces = (int)n; }
    if ((v = getenv("HED_SPELL"))) { bool b; if (cfgBool(v, b)) c.spell = b; }
    if ((v = getenv("HED_NUMBERS"))) { bool b; if (cfgBool(v, b)) c.numbers = b; }
    if ((v = getenv("HED_THEME"))) { std::string s = toLower(v); if (!s.empty()) c.theme = s; }
    if ((v = getenv("HED_LOG_SIZE"))) { long n; if (cfgInt(v, 0, 1 << 30, n)) c.logSize = (size_t)n; }
}

}  // namespace

std::string configFilePath() { return configDir() + "/config"; }

Config loadConfig() {
    Config c;  // built-in defaults
    std::string data, err;
    if (readFile(configFilePath(), data, err)) {
        // Parse the INI file. We only honour keys in the [editor] section
        // (top-of-file keys are treated as [editor] for convenience).
        bool inEditor = true;
        for (auto& raw : splitSimple(data, '\n')) {
            std::string line = trimRight(raw);
            if (line.empty() || line[0] == '#' || line[0] == ';') continue;
            if (line[0] == '[') {
                inEditor = toLower(trimRight(line)) == "[editor]";
                continue;
            }
            if (!inEditor) continue;
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = toLower(trimRight(line.substr(0, eq)));
            std::string val = trimRight(line.substr(eq + 1));
            size_t vs = val.find_first_not_of(" \t");  // strip leading whitespace
            if (vs != std::string::npos) val.erase(0, vs);
            cfgApply(c, key, val);  // ignore malformed/unknown keys
        }
    }
    cfgApplyEnv(c);  // environment beats the file
    return c;
}

bool saveConfig(const Config& c, std::string& err) {
    // Write a fresh, well-commented config file with the current values.
    std::string out =
        "# hed configuration\n"
        "#\n"
        "# This file is written by `hed config set KEY VALUE` and read on every\n"
        "# editor start. You can also edit it by hand. Precedence (highest first):\n"
        "#   command-line flags > HED_* environment variables > this file > defaults.\n"
        "#\n"
        "# Recognised keys (all optional):\n"
        "#   tabsize   tab display width (1-32)\n"
        "#   indent    auto | tabs | spaces   (indent style)\n"
        "#   spaces    indent width in columns when indent = spaces (1-16)\n"
        "#   spell     true | false   start with misspelling underlining on\n"
        "#   numbers   true | false   show the line-number gutter\n"
        "#   theme     catppuccin | dark | light | <custom theme name>   syntax palette (see 'hed theme')\n"
        "#   log_size  status messages kept in memory / shown by `hed log` (0 = unlimited)\n"
        "\n"
        "[editor]\n"
        "tabsize = " + std::to_string(c.tabsize) + "\n"
        "indent = " + std::string(c.indent == 1 ? "tabs" : c.indent == 2 ? "spaces" : "auto") + "\n"
        "spaces = " + std::to_string(c.spaces) + "\n"
        "spell = " + std::string(c.spell ? "true" : "false") + "\n"
        "numbers = " + std::string(c.numbers ? "true" : "false") + "\n"
        "theme = " + c.theme + "\n"
        "log_size = " + std::to_string(c.logSize) + "\n";
    if (!mkdirParents(configDir(), err)) return false;
    return atomicWrite(configFilePath(), out, err);
}

int runEditor(const EditorOptions& o) {
    Editor e(o);
    return e.runImpl();
}

}  // namespace hed
