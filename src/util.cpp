// SPDX-License-Identifier: MIT
#include "util.hpp"

#include <fcntl.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwchar>

namespace hed {

Text splitText(const std::string& s) {
    Text t;
    size_t nl = 0, crnl = 0;
    for (size_t i = 0; i < s.size(); ++i)
        if (s[i] == '\n') {
            nl++;
            if (i > 0 && s[i - 1] == '\r') crnl++;
        }
    t.crlf = nl > 0 && crnl == nl;
    size_t start = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\n') {
            size_t end = i;
            if (t.crlf) end--;
            t.lines.emplace_back(s.substr(start, end - start));
            start = i + 1;
        }
    }
    if (start < s.size()) t.lines.emplace_back(s.substr(start));
    t.trailingNewline = !s.empty() && s.back() == '\n';
    if (t.lines.empty()) t.lines.emplace_back();
    return t;
}

std::string joinText(const Text& t) {
    const char* nl = t.crlf ? "\r\n" : "\n";
    std::string out;
    size_t total = 0;
    for (auto& l : t.lines) total += l.size() + 2;
    out.reserve(total);
    for (size_t i = 0; i < t.lines.size(); ++i) {
        out += t.lines[i];
        if (i + 1 < t.lines.size()) out += nl;
    }
    if (t.trailingNewline) out += nl;
    return out;
}

std::vector<std::string> splitSimple(const std::string& s, char d) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == d) {
            out.emplace_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return out;
}

bool readFd(int fd, std::string& out) {
    out.clear();
    char buf[65536];
    for (;;) {
        ssize_t n = read(fd, buf, sizeof buf);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) break;
        out.append(buf, (size_t)n);
    }
    return true;
}

bool readFile(const std::string& path, std::string& out, std::string& err) {
    if (path == "-") return readFd(0, out);
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        err = path + ": " + strerror(errno);
        return false;
    }
    struct stat st;
    if (fstat(fd, &st) == 0 && S_ISDIR(st.st_mode)) {
        close(fd);
        err = path + ": is a directory";
        return false;
    }
    bool ok = readFd(fd, out);
    if (!ok) err = path + ": " + strerror(errno);
    close(fd);
    return ok;
}

bool writeAll(int fd, const std::string& data) {
    size_t off = 0;
    while (off < data.size()) {
        ssize_t n = write(fd, data.data() + off, data.size() - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        off += (size_t)n;
    }
    return true;
}

bool pathExists(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0;
}

bool isDir(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string dirName(const std::string& p) {
    size_t k = p.find_last_of('/');
    if (k == std::string::npos) return ".";
    if (k == 0) return "/";
    return p.substr(0, k);
}

std::string baseName(const std::string& p) {
    size_t k = p.find_last_of('/');
    return k == std::string::npos ? p : p.substr(k + 1);
}

static mode_t defaultMode() {
    mode_t um = umask(0);
    umask(um);
    return 0666 & ~um;
}

bool atomicWrite(const std::string& path, const std::string& data, std::string& err, int forceMode) {
    std::string target = path;
    struct stat lst;
    if (lstat(path.c_str(), &lst) == 0 && S_ISLNK(lst.st_mode)) {
        char rp[PATH_MAX];
        if (realpath(path.c_str(), rp)) target = rp;
    }
    struct stat st;
    bool exists = stat(target.c_str(), &st) == 0;
    if (exists && !S_ISREG(st.st_mode)) {
        err = target + ": not a regular file";
        return false;
    }
    mode_t mode = forceMode >= 0 ? (mode_t)forceMode : exists ? (st.st_mode & 07777) : defaultMode();
    std::string tmpl = dirName(target) + "/.hed-tmp-XXXXXX";
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    int fd = mkstemp(buf.data());
    if (fd < 0) {
        // Directory not writable (e.g. file in a root-owned dir): fall back to in-place write.
        int fd2 = open(target.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
        if (fd2 < 0) {
            err = target + ": " + strerror(errno);
            return false;
        }
        bool ok = writeAll(fd2, data);
        if (ok && forceMode >= 0) fchmod(fd2, mode);
        if (close(fd2) != 0) ok = false;
        if (!ok) err = target + ": " + strerror(errno);
        return ok;
    }
    bool ok = writeAll(fd, data) && fsync(fd) == 0;
    fchmod(fd, mode);
    if (exists && (st.st_uid != geteuid() || st.st_gid != getegid())) {
        if (fchown(fd, st.st_uid, st.st_gid) != 0) { /* best effort */
        }
    }
    if (close(fd) != 0) ok = false;
    if (!ok) {
        err = target + ": " + strerror(errno);
        unlink(buf.data());
        return false;
    }
    if (rename(buf.data(), target.c_str()) != 0) {
        err = target + ": " + strerror(errno);
        unlink(buf.data());
        return false;
    }
    return true;
}

bool appendToFile(const std::string& path, const std::string& data, std::string& err, int forceMode) {
    mode_t mode = forceMode >= 0 ? (mode_t)forceMode : defaultMode();
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, mode);
    if (fd < 0) {
        err = path + ": " + strerror(errno);
        return false;
    }
    bool ok = writeAll(fd, data);
    if (ok && forceMode >= 0) fchmod(fd, mode);
    if (close(fd) != 0) ok = false;
    if (!ok) err = path + ": " + strerror(errno);
    return ok;
}

bool makeBackup(const std::string& path, std::string& backupPath, std::string& err) {
    std::string data;
    if (!readFile(path, data, err)) return false;
    char ts[32];
    time_t now = time(nullptr);
    struct tm tmv;
    localtime_r(&now, &tmv);
    strftime(ts, sizeof ts, "%Y%m%d%H%M%S", &tmv);
    std::string dir = dirName(path);
    backupPath = (dir == "." && path.find('/') == std::string::npos ? std::string() : dir + "/") + ts + "-" + baseName(path);
    struct stat st;
    int mode = stat(path.c_str(), &st) == 0 ? (int)(st.st_mode & 07777) : -1;
    return atomicWrite(backupPath, data, err, mode);
}

bool mkdirParents(const std::string& dir, std::string& err) {
    if (dir.empty() || isDir(dir)) return true;
    std::string cur;
    for (size_t i = 0; i <= dir.size(); ++i) {
        if (i == dir.size() || dir[i] == '/') {
            cur = dir.substr(0, i);
            if (!cur.empty() && !isDir(cur)) {
                if (mkdir(cur.c_str(), 0777) != 0 && errno != EEXIST) {
                    err = cur + ": " + strerror(errno);
                    return false;
                }
            }
        }
    }
    return true;
}

std::string toLower(std::string s) {
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return s;
}

bool hasUpper(const std::string& s) {
    for (char c : s)
        if (c >= 'A' && c <= 'Z') return true;
    return false;
}

std::string trimRight(const std::string& s) {
    size_t e = s.size();
    while (e > 0 && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) e--;
    return s.substr(0, e);
}

std::string leadingWs(const std::string& s) {
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
    return s.substr(0, i);
}

bool startsWith(const std::string& s, const std::string& p) { return s.size() >= p.size() && s.compare(0, p.size(), p) == 0; }
bool endsWith(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
}

static void putUtf8(std::string& o, uint32_t cp) {
    if (cp < 0x80) o += (char)cp;
    else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
}

std::string interpretEscapes(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 >= s.size()) { o += s[i]; continue; }
        char c = s[++i];
        switch (c) {
            case 'n': o += '\n'; break;
            case 't': o += '\t'; break;
            case 'r': o += '\r'; break;
            case '0': o += '\0'; break;
            case 'a': o += '\a'; break;
            case 'b': o += '\b'; break;
            case 'e': o += '\x1b'; break;
            case 'f': o += '\f'; break;
            case 'v': o += '\v'; break;
            case '\\': o += '\\'; break;
            case '\'': o += '\''; break;
            case '"': o += '"'; break;
            case 'x': case 'u': case 'U': {
                size_t maxd = c == 'x' ? 2 : c == 'u' ? 4 : 8, k = 0;
                uint32_t v = 0;
                while (k < maxd && i + 1 < s.size() && isxdigit((unsigned char)s[i + 1])) {
                    char h = s[++i];
                    v = v * 16 + (uint32_t)(isdigit((unsigned char)h) ? h - '0' : (tolower(h) - 'a' + 10));
                    k++;
                }
                if (k == 0) { o += '\\'; o += c; }
                else if (c == 'x') o += (char)v;
                else putUtf8(o, v);
                break;
            }
            default: o += '\\'; o += c;
        }
    }
    return o;
}

std::string stripLeadingTabs(const std::string& s) {
    Text t = splitText(s);
    for (auto& l : t.lines) {
        size_t i = 0;
        while (i < l.size() && l[i] == '\t') i++;
        l.erase(0, i);
    }
    return joinText(t);
}

std::string dedent(const std::string& s) {
    Text t = splitText(s);
    if (t.lines.size() > 1 && trimRight(t.lines.front()).empty()) t.lines.erase(t.lines.begin());
    if (t.lines.size() > 1 && trimRight(t.lines.back()).empty()) {
        t.lines.pop_back();
        t.trailingNewline = true;
    }
    std::string common;
    bool first = true;
    for (auto& l : t.lines) {
        if (trimRight(l).empty()) continue;
        std::string ws = leadingWs(l);
        if (first) { common = ws; first = false; continue; }
        size_t k = 0;
        while (k < common.size() && k < ws.size() && common[k] == ws[k]) k++;
        common.resize(k);
    }
    for (auto& l : t.lines) {
        if (startsWith(l, common)) l.erase(0, common.size());
        else if (trimRight(l).empty()) l.clear();
    }
    return joinText(t);
}

static bool parseNum(const std::string& s, long& v) {
    if (s.empty()) return false;
    char* end = nullptr;
    v = strtol(s.c_str(), &end, 10);
    return end && *end == '\0';
}

bool parseRange(const std::string& specIn, size_t total, size_t& a, size_t& b, std::string& err) {
    std::string spec = specIn;
    for (auto& c : spec)
        if (c == ',') c = ':';
    long A = 1, B = (long)total;
    size_t colon = spec.find(':');
    auto bad = [&]() { err = "bad range '" + specIn + "' (use N, A:B, A:, :B, A:+N or -N:)"; return false; };
    if (colon == std::string::npos) {
        if (!parseNum(spec, A)) return bad();
        if (A < 0) { A = (long)total + A + 1; B = (long)total; }
        else B = A;
    } else {
        std::string l = spec.substr(0, colon), r = spec.substr(colon + 1);
        if (!l.empty() && !parseNum(l, A)) return bad();
        if (A < 0) A = (long)total + A + 1;
        if (r.empty() || r == "$") B = (long)total;
        else if (r[0] == '+') {
            long n;
            if (!parseNum(r.substr(1), n)) return bad();
            B = A + n;
        } else if (!parseNum(r, B)) return bad();
        else if (B < 0) B = (long)total + B + 1;
    }
    if (A < 1) A = 1;
    if (B > (long)total) B = (long)total;
    if (total == 0) { a = 1; b = 0; return true; }
    if (A > (long)total) { err = "range starts at line " + std::to_string(A) + " but there are only " + std::to_string(total) + " lines"; return false; }
    if (B < A) { err = "empty range '" + specIn + "'"; return false; }
    a = (size_t)A;
    b = (size_t)B;
    return true;
}

size_t u8next(const std::string& s, size_t i) {
    if (i >= s.size()) return s.size();
    i++;
    while (i < s.size() && ((unsigned char)s[i] & 0xC0) == 0x80) i++;
    return i;
}

size_t u8prev(const std::string& s, size_t i) {
    if (i == 0) return 0;
    i--;
    while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80) i--;
    return i;
}

int u8width(const std::string& s, size_t i, size_t& len) {
    unsigned char c = (unsigned char)s[i];
    if (c < 0x80) {
        len = 1;
        return (c < 0x20 || c == 0x7f) ? 2 : 1;
    }
    int n = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    if (n == 1 || i + (size_t)n > s.size()) { len = 1; return 1; }
    uint32_t cp = c & (0xFF >> (n + 1));
    for (int k = 1; k < n; k++) {
        unsigned char cc = (unsigned char)s[i + (size_t)k];
        if ((cc & 0xC0) != 0x80) { len = 1; return 1; }
        cp = (cp << 6) | (cc & 0x3F);
    }
    len = (size_t)n;
    int w = wcwidth((wchar_t)cp);
    return w < 0 ? 1 : w;
}

size_t displayWidth(const std::string& s) {
    size_t w = 0, i = 0, len;
    while (i < s.size()) { w += (size_t)u8width(s, i, len); i += len; }
    return w;
}

std::string jsonEscape(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
                else o += (char)c;
        }
    }
    return o + "\"";
}

std::string homeDir() {
    const char* h = getenv("HOME");
    if (h && *h) return h;
    struct passwd* pw = getpwuid(getuid());
    return pw ? pw->pw_dir : "/tmp";
}

std::string configDir() {
    const char* x = getenv("XDG_CONFIG_HOME");
    std::string base = (x && *x) ? std::string(x) : homeDir() + "/.config";
    return base + "/hed";
}

// ---------------------------------------------------------------- diff (Myers)
namespace {
struct DOp { char op; size_t a, b; };  // op: '=', '-', '+'; a/b: positions in old/new

void myers(const std::vector<std::string>& A, size_t ao, int N, const std::vector<std::string>& B, size_t bo, int M,
           std::vector<DOp>& out) {
    if (N == 0 && M == 0) return;
    const int MAX = N + M, DLIMIT = 3000;
    const int off = MAX + 1;
    std::vector<int> V((size_t)(2 * MAX + 3), 0);
    std::vector<std::vector<int>> trace;
    int found = -1;
    for (int d = 0; d <= MAX && d <= DLIMIT; d++) {
        std::vector<int> slice((size_t)(2 * d + 3));
        for (int k = -(d + 1); k <= d + 1; k++) slice[(size_t)(k + d + 1)] = V[(size_t)(k + off)];
        trace.push_back(std::move(slice));
        for (int k = -d; k <= d; k += 2) {
            int x = (k == -d || (k != d && V[(size_t)(k - 1 + off)] < V[(size_t)(k + 1 + off)])) ? V[(size_t)(k + 1 + off)]
                                                                                                : V[(size_t)(k - 1 + off)] + 1;
            int y = x - k;
            while (x < N && y < M && A[ao + (size_t)x] == B[bo + (size_t)y]) { x++; y++; }
            V[(size_t)(k + off)] = x;
            if (x >= N && y >= M) { found = d; goto done; }
        }
    }
done:
    if (found < 0) {  // too different: delete all then insert all
        for (int i = 0; i < N; i++) out.push_back({'-', ao + (size_t)i, bo});
        for (int j = 0; j < M; j++) out.push_back({'+', ao + (size_t)N, bo + (size_t)j});
        return;
    }
    std::vector<DOp> rev;
    int x = N, y = M;
    for (int d = found; d > 0; d--) {
        const auto& T = trace[(size_t)d];
        auto get = [&](int k) { return T[(size_t)(k + d + 1)]; };
        int k = x - y;
        int pk = (k == -d || (k != d && get(k - 1) < get(k + 1))) ? k + 1 : k - 1;
        int px = get(pk), py = px - pk;
        while (x > px && y > py) { x--; y--; rev.push_back({'=', ao + (size_t)x, bo + (size_t)y}); }
        if (x == px) { y--; rev.push_back({'+', ao + (size_t)x, bo + (size_t)y}); }
        else { x--; rev.push_back({'-', ao + (size_t)x, bo + (size_t)y}); }
    }
    while (x > 0 && y > 0) { x--; y--; rev.push_back({'=', ao + (size_t)x, bo + (size_t)y}); }
    out.insert(out.end(), rev.rbegin(), rev.rend());
}
}  // namespace

std::string unifiedDiff(const std::vector<std::string>& A, const std::vector<std::string>& B, const std::string& nameA,
                        const std::string& nameB, int context, bool color) {
    size_t pre = 0;
    while (pre < A.size() && pre < B.size() && A[pre] == B[pre]) pre++;
    size_t suf = 0;
    while (suf < A.size() - pre && suf < B.size() - pre && A[A.size() - 1 - suf] == B[B.size() - 1 - suf]) suf++;
    if (pre == A.size() && pre == B.size()) return "";
    std::vector<DOp> ops;
    for (size_t i = 0; i < pre; i++) ops.push_back({'=', i, i});
    myers(A, pre, (int)(A.size() - pre - suf), B, pre, (int)(B.size() - pre - suf), ops);
    for (size_t i = 0; i < suf; i++) ops.push_back({'=', A.size() - suf + i, B.size() - suf + i});

    const char *R = color ? "\x1b[0m" : "", *RED = color ? "\x1b[31m" : "", *GRN = color ? "\x1b[32m" : "",
               *CYA = color ? "\x1b[36m" : "", *BLD = color ? "\x1b[1m" : "";
    std::string out = std::string(BLD) + "--- " + nameA + R + "\n" + BLD + "+++ " + nameB + R + "\n";
    size_t n = ops.size(), i = 0;
    size_t ctx = (size_t)std::max(0, context);
    while (i < n) {
        while (i < n && ops[i].op == '=') i++;
        if (i >= n) break;
        size_t hs = i >= ctx ? i - ctx : 0, he = i;
        // extend hunk while changes are within 2*ctx of each other
        for (;;) {
            while (he < n && ops[he].op != '=') he++;
            size_t j = he;
            while (j < n && ops[j].op == '=') j++;
            if (j < n && j - he <= 2 * ctx) { he = j; continue; }
            he = std::min(n, he + ctx);
            break;
        }
        size_t oc = 0, nc = 0;
        for (size_t k = hs; k < he; k++) {
            if (ops[k].op != '+') oc++;
            if (ops[k].op != '-') nc++;
        }
        size_t os = ops[hs].a + (oc ? 1 : 0), ns = ops[hs].b + (nc ? 1 : 0);
        out += std::string(CYA) + "@@ -" + std::to_string(os) + "," + std::to_string(oc) + " +" + std::to_string(ns) + "," +
               std::to_string(nc) + " @@" + R + "\n";
        for (size_t k = hs; k < he; k++) {
            if (ops[k].op == '=') out += " " + A[ops[k].a] + "\n";
            else if (ops[k].op == '-') out += std::string(RED) + "-" + A[ops[k].a] + R + "\n";
            else out += std::string(GRN) + "+" + B[ops[k].b] + R + "\n";
        }
        i = he;
    }
    return out;
}

}  // namespace hed
