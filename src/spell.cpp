// SPDX-License-Identifier: MIT
// Spell checker. Dictionary: SymSpell frequency_dictionary_en_82_765.txt (MIT, (c) Wolf Garbe),
// embedded at build time. Suggestion algorithm: frequency-ranked Damerau-Levenshtein candidates
// (edit distance 1, then 2), plus split-word suggestions. Written from scratch for hed.
#include "spell.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>

#include "util.hpp"

extern "C" {
extern const char hed_dict_data[];
extern const char hed_dict_end[];
}

namespace hed {

static const char* TECH_WORDS = R"(
api apis async await backend frontend boolean booleans cli config configs configured cpu cpus cron crontab daemon daemons
dev devops dns docker dockerfile env envs github gitlab gpu gpus hostname hostnames http https json jsonl kubernetes
localhost login logins logout middleware namespace namespaces nginx npm param params postgres postgresql pytest regex
regexes repo repos runtime runtimes schema schemas sql stderr stdin stdout subdomain subdomains sudo symlink symlinks
systemd timestamp timestamps todo todos tooltip tooltips typescript javascript unicode url urls utf username usernames
webhook webhooks websocket websockets yaml init args kwargs bool str int func funcs enum enums const eof lookup lookups
auth oauth jwt ssl tls ssh sshd scp rsync grep sed awk bash zsh fish shell shells stdlib struct structs util utils
tmp mkdir chmod chown filesystem filesystems inode inodes subprocess subprocesses multithreaded multithreading mutex
mutexes goroutine callback callbacks async iterable iterables iterator iterators serializer deserialize serialize
serialized deserialized serialization unmarshal marshal refactor refactored refactoring linter linters lint linting
formatter prettier eslint webpack vite nodejs npm npx pnpm yarn deno pip venv virtualenv conda pypi toml ini csv tsv
xml html css scss dom jsx tsx vue svelte localhost backend backends frontend frontends dataset datasets dataframe
dataframes numpy pandas matplotlib sklearn pytorch tensorflow llm llms gpt claude anthropic openai ollama llama
inference embeddings embedding tokenizer tokenizers tokenize tokenized quantized quantization finetune finetuning
homelab proxmox truenas zfs btrfs ext raid nas vlan vlans subnet subnets cidr ipv ipv4 ipv6 dhcp nat vpn wireguard
openvpn firewall iptables nftables ufw fail2ban selinux apparmor syslog journald journalctl systemctl ansible terraform
kubectl helm podman containerd compose redis mongodb mysql sqlite mariadb aurora iam aws gcp azure entra saml ldap
kerberos rbac acl acls cve cves pentest infosec siem soc mfa totp yubikey gpg pgp sha md5 hmac aes rsa ecdsa ed25519
checksum checksums hash hashes hashing plaintext ciphertext keychain keyring passwordless whitelist blacklist allowlist
denylist dotfile dotfiles readme changelog markdown heredoc heredocs stdin nano vim emacs neovim tmux ssh ok okay
filename filenames pathname dirname basename unix linux macos ubuntu debian fedora centos rhel arch alpine nixos
upstream downstream codebase codebases workflow workflows repo monorepo microservice microservices serverless
api's app apps webapp webapps cron uptime downtime failover rollback rollbacks hotfix hotfixes changeset diff diffs
upsert upserts idempotent idempotency nullable enqueue dequeue async lifecycle metadata namespace plugin plugins
config's hed ncurses outdent vipe hunspell nuspell symspell scowl ngram frontmatter changelog readme vendored whitespace garbe dropdown checkbox checkboxes navbar sidebar popup popups modal modals viewport responsive hover
)";

Speller& Speller::instance() {
    static Speller sp;
    return sp;
}

std::string userDictPath() { return configDir() + "/words.txt"; }

void Speller::loadList(const char* data, size_t n, uint64_t defFreq) {
    size_t i = 0;
    if (n >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF) i = 3;
    while (i < n) {
        size_t j = i;
        while (j < n && data[j] != '\n') j++;
        size_t ws = i;
        while (ws < j && !isspace((unsigned char)data[ws])) ws++;
        if (ws > i) {
            std::string w = toLower(std::string(data + i, ws - i));
            uint64_t f = defFreq;
            size_t k = ws;
            while (k < j && isspace((unsigned char)data[k])) k++;
            if (k < j && isdigit((unsigned char)data[k])) f = strtoull(data + k, nullptr, 10);
            auto& slot = dict_[w];
            if (f > slot) slot = f;
        }
        i = j + 1;
    }
}

bool Speller::ensureLoaded() {
    if (tried_) return ok();
    tried_ = true;
    dict_.reserve(120000);
    std::string err;
    const char* env = getenv("HED_DICT");
    if (env && *env) {
        std::string data;
        if (readFile(env, data, err)) loadList(data.data(), data.size(), 1000);
        else error_ = "HED_DICT: " + err;
    } else {
        loadList(hed_dict_data, (size_t)(hed_dict_end - hed_dict_data), 1000);
    }
    {
        std::string w;
        for (const char* q = TECH_WORDS;; q++) {
            if (*q && !isspace((unsigned char)*q)) { w += *q; continue; }
            if (!w.empty()) { auto& slot = dict_[toLower(w)]; if (slot < 50000) slot = 50000; w.clear(); }
            if (!*q) break;
        }
    }
    std::string data;
    if (readFile(userDictPath(), data, err)) loadList(data.data(), data.size(), 100000);
    if (const char* extra = getenv("HED_EXTRA_DICTS")) {
        for (auto& p : splitSimple(extra, ':'))
            if (!p.empty() && readFile(p, data, err)) loadList(data.data(), data.size(), 1000);
    }
    if (dict_.empty() && error_.empty()) error_ = "no dictionary loaded";
    return ok();
}

bool Speller::inDict(const std::string& lw) const { return dict_.count(lw) > 0; }

bool Speller::affixKnown(const std::string& w) const {
    static const std::vector<std::pair<const char*, const char*>> suffixes = {
        {"s", ""},      {"es", ""},     {"ies", "y"},   {"ed", ""},     {"ed", "e"},    {"ied", "y"},  {"ing", ""},
        {"ing", "e"},   {"er", ""},     {"er", "e"},    {"ers", ""},    {"ers", "e"},   {"est", ""},   {"ly", ""},
        {"ily", "y"},   {"ness", ""},   {"ment", ""},   {"ments", ""},  {"able", ""},   {"able", "e"}, {"ation", "ate"},
        {"ations", "ate"}, {"ize", ""}, {"izes", ""},   {"ized", ""},   {"ise", ""},    {"ful", ""},   {"less", ""},
        {"ish", ""},    {"ity", ""},    {"al", ""},     {"ally", ""},   {"ically", "ic"}, {"ability", "able"}};
    static const std::vector<const char*> prefixes = {"un", "re", "pre", "non", "dis", "mis", "sub", "over", "under",
                                                      "co", "anti", "multi", "inter", "auto", "de", "in", "im", "semi"};
    auto baseOk = [&](const std::string& b) { return b.size() >= 3 && inDict(b); };
    for (auto& sfx : suffixes) {
        std::string s = sfx.first;
        if (w.size() > s.size() + 2 && endsWith(w, s)) {
            std::string base = w.substr(0, w.size() - s.size()) + sfx.second;
            if (baseOk(base)) return true;
            // doubled consonant: running -> run, stopped -> stop
            std::string b2 = w.substr(0, w.size() - s.size());
            if ((s == "ing" || s == "ed" || s == "er") && b2.size() >= 3 && b2[b2.size() - 1] == b2[b2.size() - 2] &&
                baseOk(b2.substr(0, b2.size() - 1)))
                return true;
        }
    }
    for (auto p : prefixes) {
        std::string ps = p;
        if (w.size() > ps.size() + 3 && startsWith(w, ps)) {
            std::string rest = w.substr(ps.size());
            if (rest[0] == '-') rest.erase(0, 1);
            if (baseOk(rest)) return true;
            for (auto& sfx : suffixes) {
                std::string s = sfx.first;
                if (rest.size() > s.size() + 2 && endsWith(rest, s) && baseOk(rest.substr(0, rest.size() - s.size()) + sfx.second))
                    return true;
            }
        }
    }
    return false;
}

static std::string normApos(const std::string& w) {
    std::string o;
    for (size_t i = 0; i < w.size(); i++) {
        if ((unsigned char)w[i] == 0xE2 && i + 2 < w.size() && (unsigned char)w[i + 1] == 0x80 &&
            ((unsigned char)w[i + 2] == 0x99 || (unsigned char)w[i + 2] == 0x98)) {
            o += '\'';
            i += 2;
        } else o += w[i];
    }
    return o;
}

bool Speller::known(const std::string& word) const {
    std::string w = toLower(normApos(word));
    if (w.empty() || inDict(w) || ignored_.count(w)) return true;
    size_t ap = w.find('\'');
    if (ap != std::string::npos) {
        std::string base = w.substr(0, ap), suf = w.substr(ap + 1);
        if (suf == "s" || suf == "d" || suf == "m" || suf == "ll" || suf == "re" || suf == "ve" || suf.empty())
            return inDict(base) || affixKnown(base) || base == "i" || base == "let";
        if (suf == "t" && base.size() > 1 && base.back() == 'n') {
            std::string root = base.substr(0, base.size() - 1);
            static const std::unordered_set<std::string> irregular = {"ca", "wo", "ai", "sha", "can", "won", "ain", "shan"};
            return irregular.count(root) || irregular.count(base) || inDict(root);
        }
        return false;
    }
    return affixKnown(w);
}

static void edits1(const std::string& w, std::vector<std::string>& out) {
    static const char letters[] = "abcdefghijklmnopqrstuvwxyz";
    size_t n = w.size();
    for (size_t i = 0; i <= n; i++) {
        if (i < n) out.push_back(w.substr(0, i) + w.substr(i + 1));
        if (i + 1 < n && w[i] != w[i + 1]) {
            std::string t = w;
            std::swap(t[i], t[i + 1]);
            out.push_back(t);
        }
        for (int k = 0; k < 26; k++) {
            char c = letters[k];
            if (i < n && c != w[i]) {
                std::string t = w;
                t[i] = c;
                out.push_back(t);
            }
            std::string t = w;
            t.insert(t.begin() + (long)i, c);
            out.push_back(t);
        }
    }
}

std::vector<std::string> Speller::suggest(const std::string& word, size_t n) const {
    std::string orig = normApos(word);
    std::string lw = toLower(orig);
    struct Cand { int d; uint64_t f; };
    std::unordered_map<std::string, Cand> c;
    auto add = [&](const std::string& s, int d, uint64_t f) {
        auto it = c.find(s);
        if (it == c.end() || it->second.d > d) c[s] = {d, f};
    };
    std::vector<std::string> e1;
    edits1(lw, e1);
    for (auto& x : e1) {
        auto it = dict_.find(x);
        if (it != dict_.end()) add(x, 1, it->second);
    }
    for (size_t i = 1; i + 1 < lw.size(); i++) {  // missing space: "alot" -> "a lot"
        std::string a = lw.substr(0, i), b = lw.substr(i);
        if ((a.size() > 1 || a == "a" || a == "i") && b.size() > 1) {
            auto ia = dict_.find(a), ib = dict_.find(b);
            if (ia != dict_.end() && ib != dict_.end()) add(a + " " + b, 1, std::min(ia->second, ib->second) / 20);
        }
    }
    if (c.size() < n && lw.size() <= 24) {
        std::unordered_set<std::string> seen;
        std::vector<std::string> e2;
        for (auto& x : e1) {
            if (!seen.insert(x).second) continue;
            e2.clear();
            edits1(x, e2);
            for (auto& y : e2) {
                if (y == lw) continue;
                auto it = dict_.find(y);
                if (it != dict_.end()) add(y, 2, it->second);
            }
        }
    }
    std::vector<std::pair<std::string, Cand>> v(c.begin(), c.end());
    std::sort(v.begin(), v.end(), [](const auto& A, const auto& B) {
        if (A.second.d != B.second.d) return A.second.d < B.second.d;
        if (A.second.f != B.second.f) return A.second.f > B.second.f;
        return A.first < B.first;
    });
    bool allCaps = orig.size() > 1 && !std::any_of(orig.begin(), orig.end(), [](char ch) { return islower((unsigned char)ch); });
    bool cap = !orig.empty() && isupper((unsigned char)orig[0]);
    std::vector<std::string> out;
    for (auto& p : v) {
        if (out.size() >= n) break;
        std::string s = p.first;
        if (allCaps) for (auto& ch : s) ch = (char)toupper((unsigned char)ch);
        else if (cap) s[0] = (char)toupper((unsigned char)s[0]);
        out.push_back(s);
    }
    return out;
}

bool Speller::addUserWord(const std::string& w, std::string& err) {
    std::string lw = toLower(normApos(w));
    if (lw.empty()) return true;
    if (!mkdirParents(configDir(), err)) return false;
    if (!appendToFile(userDictPath(), lw + "\n", err)) return false;
    dict_[lw] = 100000;
    return true;
}

void Speller::ignore(const std::string& w) { ignored_.insert(toLower(normApos(w))); }

// ------------------------------------------------------------------ word extraction
static bool isAposAt(const std::string& s, size_t i, size_t& len) {
    if (s[i] == '\'') { len = 1; return true; }
    if ((unsigned char)s[i] == 0xE2 && i + 2 < s.size() && (unsigned char)s[i + 1] == 0x80 &&
        ((unsigned char)s[i + 2] == 0x99 || (unsigned char)s[i + 2] == 0x98)) {
        len = 3;
        return true;
    }
    return false;
}

static void checkChunk(const Speller& sp, const std::string& s, size_t a, size_t b, std::vector<Misspelling>& out) {
    // trim surrounding ASCII punctuation (quotes, brackets, sentence punctuation)
    size_t al;
    while (a < b && (unsigned char)s[a] < 0x80 && !isalnum((unsigned char)s[a])) a++;
    while (a < b && isAposAt(s, a, al)) a += al;
    while (b > a && (unsigned char)s[b - 1] < 0x80 && !isalnum((unsigned char)s[b - 1])) b--;
    if (b >= 3 && b - 3 >= a && isAposAt(s, b - 3, al) && al == 3) b -= 3;
    if (a >= b) return;
    // code-ish chunks are skipped entirely: paths, URLs, identifiers, emails, numbers, markup
    for (size_t i = a; i < b; i++) {
        unsigned char c = (unsigned char)s[i];
        if (isdigit(c) || strchr("_/\\@$=<>{}[]|`~^*#%&+:;().\"", (char)c)) return;
    }
    size_t i = a;
    while (i < b) {
        // word = letters + internal apostrophes
        while (i < b && !isalpha((unsigned char)s[i]) && (unsigned char)s[i] < 0x80) i++;
        size_t ws = i;
        bool nonAscii = false;
        while (i < b) {
            unsigned char c = (unsigned char)s[i];
            size_t l;
            if (isalpha(c)) { i++; continue; }
            if (isAposAt(s, i, l) && i + l < b && isalpha((unsigned char)s[i + l])) { i += l; continue; }
            if (c >= 0x80) {
                if (isAposAt(s, i, l)) break;
                nonAscii = true;
                i = u8next(s, i);
                continue;
            }
            break;
        }
        size_t we = i;
        if (we <= ws) { if (i < b) i++; continue; }
        if (nonAscii) continue;
        std::string w = s.substr(ws, we - ws);
        size_t letters = 0, uppers = 0;
        bool innerUpper = false;
        for (size_t k = 0; k < w.size(); k++) {
            if (isalpha((unsigned char)w[k])) letters++;
            if (isupper((unsigned char)w[k])) { uppers++; if (k > 0) innerUpper = true; }
        }
        if (letters < 3) continue;           // short words / abbreviations
        if (uppers == letters) continue;     // ACRONYMS
        if (innerUpper) continue;            // camelCase / iPhone-style identifiers
        if (!sp.known(w)) out.push_back({ws, we - ws, w});
    }
}

std::vector<Misspelling> findMisspellings(const Speller& sp, LangId lang, const std::string& s,
                                          const std::vector<uint8_t>& hl, bool allText) {
    std::vector<Misspelling> out;
    if (!sp.ok()) return out;
    auto inReg = [&](size_t k) { return allText || (k < hl.size() && spellRegion(lang, hl[k])); };
    size_t n = s.size(), i = 0;
    while (i < n) {
        if (!inReg(i) || isspace((unsigned char)s[i])) { i++; continue; }
        size_t j = i;
        while (j < n && inReg(j) && !isspace((unsigned char)s[j])) j++;
        // a chunk touching code (e.g. string start right after an identifier) is still fine to check
        checkChunk(sp, s, i, j, out);
        i = j;
    }
    return out;
}

}  // namespace hed
