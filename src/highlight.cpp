// SPDX-License-Identifier: MIT
#include "highlight.hpp"

#include <cctype>
#include <cstring>
#include <sstream>
#include <unordered_set>

#include "util.hpp"

namespace hed {
namespace {

using WordSet = std::unordered_set<std::string>;

WordSet W(const char* s) {
    WordSet out;
    std::istringstream is(s);
    std::string w;
    while (is >> w) out.insert(w);
    return out;
}

struct LangDef {
    LangId id;
    const char* name;
    std::vector<std::string> aliases, exts, filenames, interps;
    std::vector<std::string> lineComments;
    std::string blockOpen, blockClose;
    WordSet kw, types, builtins, consts;
    bool icase = false, pyStrings = false, backtick = false, shell = false, fish = false, decorators = false,
         multilineStrings = false, sqlQuotes = false, json = false;
};

const char* JS_KW =
    "break case catch class const continue debugger default delete do else export extends finally for function if "
    "import in instanceof let new return super switch this throw try typeof var void while with yield async await of "
    "static get set from as";
const char* JS_BUILTINS =
    "console window document Math JSON Object Array String Number Boolean Promise Map Set WeakMap WeakSet Symbol Date "
    "RegExp Error TypeError RangeError require module exports process globalThis parseInt parseFloat isNaN setTimeout "
    "setInterval clearTimeout clearInterval fetch Buffer Intl Reflect Proxy BigInt";

std::vector<LangDef> buildDefs() {
    std::vector<LangDef> d;
    {
        LangDef l; l.id = L_TEXT; l.name = "text"; l.aliases = {"txt", "plain", "none"}; l.exts = {"txt", "text", "log"};
        d.push_back(l);
    }
    {
        LangDef l; l.id = L_PYTHON; l.name = "python"; l.aliases = {"py", "python3"};
        l.exts = {"py", "pyw", "pyi"}; l.interps = {"python", "pypy"}; l.filenames = {"SConstruct", "SConscript"};
        l.lineComments = {"#"}; l.pyStrings = true; l.decorators = true;
        l.kw = W("and as assert async await break class continue def del elif else except finally for from global if "
                 "import in is lambda nonlocal not or pass raise return try while with yield match case type");
        l.consts = W("True False None NotImplemented Ellipsis __name__ __file__ __doc__");
        l.builtins = W("print len range int str float list dict set tuple bool bytes bytearray open type isinstance "
                       "issubclass super self cls object enumerate zip map filter sorted reversed min max sum abs any all "
                       "input repr hash id iter next getattr setattr hasattr delattr property staticmethod classmethod "
                       "format vars dir callable round pow divmod chr ord hex oct bin frozenset slice memoryview compile "
                       "exec eval globals locals help breakpoint Exception BaseException ValueError TypeError KeyError "
                       "IndexError AttributeError RuntimeError OSError IOError FileNotFoundError PermissionError "
                       "StopIteration ImportError ModuleNotFoundError NotImplementedError ZeroDivisionError "
                       "KeyboardInterrupt SystemExit AssertionError TimeoutError");
        d.push_back(l);
    }
    {
        LangDef l; l.id = L_BASH; l.name = "bash"; l.aliases = {"sh", "shell", "zsh", "ksh"};
        l.exts = {"sh", "bash", "zsh", "ksh", "bats", "command"};
        l.filenames = {".bashrc", ".bash_profile", ".bash_aliases", ".bash_logout", ".profile", ".zshrc", ".zprofile",
                       ".zshenv", "PKGBUILD", "APKBUILD", ".envrc"};
        l.interps = {"bash", "sh", "zsh", "ksh", "dash", "ash", "busybox"};
        l.lineComments = {"#"}; l.shell = true; l.multilineStrings = true;
        l.kw = W("if then else elif fi case esac for select while until do done in function time coproc");
        l.builtins = W("echo printf read cd pwd source eval exec exit export local declare typeset readonly unset shift "
                       "return set trap test alias unalias let getopts wait kill jobs bg fg type hash command builtin "
                       "true false mapfile readarray pushd popd dirs umask ulimit shopt caller compgen complete enable "
                       "help history logout disown suspend times break continue");
        d.push_back(l);
    }
    {
        LangDef l; l.id = L_FISH; l.name = "fish"; l.aliases = {"fishshell"}; l.exts = {"fish"}; l.interps = {"fish"};
        l.lineComments = {"#"}; l.shell = true; l.fish = true; l.multilineStrings = true;
        l.kw = W("function end if else switch case for in while begin return break continue and or not");
        l.builtins = W("set set_color echo printf read cd source status test string math contains count argparse abbr "
                       "alias bind builtin command commandline complete emit eval exec exit functions fish_add_path "
                       "history jobs random realpath type path fish_config funced funcsave isatty true false prevd nextd "
                       "dirh pushd popd psub wait disown");
        d.push_back(l);
    }
    {
        LangDef l; l.id = L_JS; l.name = "javascript"; l.aliases = {"js", "node", "jsx"};
        l.exts = {"js", "mjs", "cjs", "jsx"}; l.interps = {"node", "nodejs", "bun"};
        l.lineComments = {"//"}; l.blockOpen = "/*"; l.blockClose = "*/"; l.backtick = true;
        l.kw = W(JS_KW); l.consts = W("true false null undefined NaN Infinity"); l.builtins = W(JS_BUILTINS);
        d.push_back(l);
    }
    {
        LangDef l; l.id = L_TS; l.name = "typescript"; l.aliases = {"ts", "tsx"};
        l.exts = {"ts", "tsx", "mts", "cts"}; l.interps = {"ts-node", "deno", "tsx"};
        l.lineComments = {"//"}; l.blockOpen = "/*"; l.blockClose = "*/"; l.backtick = true; l.decorators = true;
        l.kw = W((std::string(JS_KW) + " interface type enum implements namespace declare abstract private protected "
                                       "public readonly keyof infer is asserts satisfies override module unique").c_str());
        l.consts = W("true false null undefined NaN Infinity");
        l.types = W("string number boolean any unknown never void object bigint symbol Record Partial Required "
                    "Readonly Pick Omit Exclude Extract NonNullable ReturnType Parameters Awaited");
        l.builtins = W(JS_BUILTINS);
        d.push_back(l);
    }
    {
        LangDef l; l.id = L_SQL; l.name = "sql"; l.aliases = {"postgres", "postgresql", "pgsql", "psql", "mysql", "sqlite"};
        l.exts = {"sql", "psql", "pgsql", "ddl", "dml"};
        l.lineComments = {"--"}; l.blockOpen = "/*"; l.blockClose = "*/"; l.icase = true; l.multilineStrings = true;
        l.sqlQuotes = true;
        l.kw = W("select from where insert into values update set delete create table drop alter add column index view "
                 "join inner left right outer full cross on as and or not is in exists between like ilike group by order "
                 "having limit offset union all distinct case when then else end primary key foreign references unique "
                 "default check constraint begin commit rollback transaction grant revoke with recursive returning desc "
                 "asc if cascade restrict trigger function procedure return returns language schema database sequence "
                 "truncate explain analyze vacuum natural using over partition window lateral do nothing conflict "
                 "replace temporary temp materialized role user owner to execute declare loop for while raise notice "
                 "exception perform security definer immutable stable volatile extension policy enable disable row "
                 "level each statement before after instead of fetch next rows only");
        l.types = W("int integer bigint smallint serial bigserial smallserial text varchar char character varying "
                    "boolean bool date time timestamp timestamptz interval numeric decimal real double precision float "
                    "json jsonb uuid bytea inet cidr macaddr money xml tsvector oid regclass void record trigger");
        l.consts = W("true false null current_date current_time current_timestamp current_user session_user");
        l.builtins = W("count sum avg min max coalesce nullif now cast lower upper length substring concat round trim "
                       "greatest least array_agg string_agg json_agg jsonb_agg row_number rank dense_rank lag lead "
                       "extract date_trunc to_char to_date to_timestamp generate_series unnest exists");
        d.push_back(l);
    }
    {
        LangDef l; l.id = L_HTML; l.name = "html"; l.aliases = {"htm", "xml", "xhtml", "svg"};
        l.exts = {"html", "htm", "xhtml", "xml", "svg", "vue", "svelte", "plist", "xsd", "xsl"};
        d.push_back(l);
    }
    {
        LangDef l; l.id = L_CSS; l.name = "css"; l.aliases = {"scss", "less"}; l.exts = {"css", "scss", "less"};
        d.push_back(l);
    }
    {
        LangDef l; l.id = L_JSON; l.name = "json"; l.aliases = {"jsonc", "json5"};
        l.exts = {"json", "jsonc", "json5", "jsonl", "ndjson", "geojson", "webmanifest"};
        l.filenames = {".babelrc", ".eslintrc", ".prettierrc", "composer.lock", "package-lock.json"};
        l.lineComments = {"//"}; l.blockOpen = "/*"; l.blockClose = "*/"; l.json = true;
        l.consts = W("true false null");
        d.push_back(l);
    }
    {
        LangDef l; l.id = L_MARKDOWN; l.name = "markdown"; l.aliases = {"md"}; l.exts = {"md", "markdown", "mdx", "mkd"};
        l.filenames = {"README", "CHANGELOG"};
        d.push_back(l);
    }
    return d;
}

const std::vector<LangDef>& defs() {
    static const std::vector<LangDef> d = buildDefs();
    return d;
}

const LangDef& def(LangId id) {
    for (auto& l : defs())
        if (l.id == id) return l;
    return defs()[0];
}

inline bool isIdStart(unsigned char c) { return isalpha(c) || c == '_' || c >= 0x80; }
inline bool isIdChar(unsigned char c) { return isalnum(c) || c == '_' || c >= 0x80; }

inline void fill(std::vector<uint8_t>& hl, size_t a, size_t b, Hl h) {
    if (b > hl.size()) b = hl.size();
    for (size_t k = a; k < b; k++) hl[k] = h;
}

inline bool at(const std::string& s, size_t i, size_t e, const char* lit) {
    size_t n = strlen(lit);
    return i + n <= e && s.compare(i, n, lit) == 0;
}

size_t ifind(const std::string& s, const char* needle, size_t from) {
    size_t n = strlen(needle);
    for (size_t i = from; i + n <= s.size(); i++) {
        size_t k = 0;
        while (k < n && tolower((unsigned char)s[i + k]) == needle[k]) k++;
        if (k == n) return i;
    }
    return std::string::npos;
}

// ------------------------------------------------------------------ generic
enum GMode { G_NORMAL = 0, G_BLOCK, G_DQ, G_SQ, G_TDQ, G_TSQ, G_TPL, G_HEREDOC, G_ANSIC };

size_t scanShellVar(const std::string& s, size_t i, size_t e) {
    size_t j = i + 1;
    if (j >= e) return i + 1;
    if (s[j] == '{') {
        int depth = 1;
        j++;
        while (j < e && depth) {
            if (s[j] == '{') depth++;
            else if (s[j] == '}') depth--;
            j++;
        }
        return j;
    }
    if (s[j] == '(') return j + 1;
    if (isIdStart((unsigned char)s[j])) {
        while (j < e && isIdChar((unsigned char)s[j])) j++;
        return j;
    }
    if (isdigit((unsigned char)s[j]) || strchr("@?#$!*-", s[j])) return j + 1;
    return i + 1;
}

// Scans a string body starting at i. Returns end index; sets closed.
size_t scanString(const LangDef& L, const std::string& s, size_t i, size_t e, const std::string& close, bool escapes,
                  bool shellVars, bool tplVars, std::vector<uint8_t>& hl, bool& closed) {
    closed = false;
    while (i < e) {
        if (escapes && s[i] == '\\' && i + 1 < e) {
            size_t n = u8next(s, i + 1);
            fill(hl, i, n, H_ESCAPE);
            i = n;
            continue;
        }
        if (i + close.size() <= e && s.compare(i, close.size(), close) == 0) {
            if (L.sqlQuotes && close.size() == 1 && i + 1 < e && s[i + 1] == close[0]) {  // SQL '' escape
                fill(hl, i, i + 2, H_ESCAPE);
                i += 2;
                continue;
            }
            fill(hl, i, i + close.size(), H_STRING);
            closed = true;
            return i + close.size();
        }
        if (shellVars && s[i] == '$' && i + 1 < e) {
            size_t j = scanShellVar(s, i, e);
            if (j > i + 1) { fill(hl, i, j, H_VARIABLE); i = j; continue; }
        }
        if (tplVars && s[i] == '$' && i + 1 < e && s[i + 1] == '{') {
            size_t j = s.find('}', i);
            j = (j == std::string::npos || j >= e) ? e : j + 1;
            fill(hl, i, j, H_VARIABLE);
            i = j;
            continue;
        }
        hl[i] = H_STRING;
        i++;
    }
    return e;
}

void hlGeneric(const LangDef& L, const std::string& s, size_t b, size_t e, HlState& st, std::vector<uint8_t>& hl) {
    if (st.mode == G_HEREDOC) {
        std::string t = s.substr(b, e - b);
        std::string cmp = t;
        if (st.depth) {
            size_t k = 0;
            while (k < cmp.size() && cmp[k] == '\t') k++;
            cmp.erase(0, k);
        }
        if (cmp == st.term) {
            fill(hl, b, e, H_PREPROC);
            st.mode = G_NORMAL; st.term.clear(); st.depth = 0;
        } else fill(hl, b, e, H_STRING);
        return;
    }
    size_t i = b;
    bool cmdPos = true, pendingHeredoc = false, afterDefKw = false;
    std::string defKw;
    while (i < e) {
        if (st.mode == G_BLOCK) {
            size_t j = s.find(L.blockClose, i);
            if (j == std::string::npos || j >= e) { fill(hl, i, e, H_COMMENT); i = e; break; }
            j += L.blockClose.size();
            fill(hl, i, j, H_COMMENT);
            i = j;
            st.mode = G_NORMAL;
            continue;
        }
        if (st.mode >= G_DQ && st.mode != G_HEREDOC) {
            std::string close;
            bool esc = true, sv = false, tv = false;
            switch (st.mode) {
                case G_DQ: close = "\""; sv = L.shell; esc = !L.sqlQuotes; break;
                case G_SQ: close = "'"; esc = !(L.shell && !L.fish) && !L.sqlQuotes; break;
                case G_TDQ: close = "\"\"\""; break;
                case G_TSQ: close = "'''"; break;
                case G_TPL: close = "`"; tv = true; break;
                case G_ANSIC: close = "'"; break;
                default: close = "\""; break;
            }
            size_t strStart = i > b ? i - 1 : i;
            bool closed;
            i = scanString(L, s, i, e, close, esc, sv, tv, hl, closed);
            if (closed) {
                if (L.json && close == "\"") {  // object key?
                    size_t k = i;
                    while (k < e && (s[k] == ' ' || s[k] == '\t')) k++;
                    if (k < e && s[k] == ':') {
                        size_t q = strStart;
                        while (q > b && s[q] != '"') q--;
                        fill(hl, q, i, H_PROPERTY);
                    }
                }
                st.mode = G_NORMAL;
            }
            continue;
        }
        unsigned char c = (unsigned char)s[i];
        bool lc = false;
        for (auto& m : L.lineComments) {
            if (i + m.size() <= e && s.compare(i, m.size(), m) == 0) {
                if (L.shell && m == "#" && i > b && !(isspace((unsigned char)s[i - 1]) || strchr(";|&(`", s[i - 1])))
                    continue;
                fill(hl, i, e, H_COMMENT);
                i = e;
                lc = true;
                break;
            }
        }
        if (lc) break;
        if (!L.blockOpen.empty() && i + L.blockOpen.size() <= e && s.compare(i, L.blockOpen.size(), L.blockOpen) == 0) {
            st.mode = G_BLOCK;
            fill(hl, i, i + L.blockOpen.size(), H_COMMENT);
            i += L.blockOpen.size();
            continue;
        }
        if (L.pyStrings && (i == b || !isIdChar((unsigned char)s[i - 1]))) {
            size_t j = i;
            while (j < e && j - i < 2 && strchr("rRbBfFuU", s[j]) && s[j]) j++;
            if (j < e && (s[j] == '"' || s[j] == '\'')) {
                bool triple = at(s, j, e, s[j] == '"' ? "\"\"\"" : "'''");
                st.mode = triple ? (s[j] == '"' ? G_TDQ : G_TSQ) : (s[j] == '"' ? G_DQ : G_SQ);
                size_t n = j + (triple ? 3 : 1);
                fill(hl, i, n, H_STRING);
                i = n;
                continue;
            }
        }
        if (L.shell && c == '\\' && i + 1 < e) {
            size_t n = u8next(s, i + 1);
            fill(hl, i, n, H_ESCAPE);
            i = n;
            cmdPos = false;
            continue;
        }
        if (L.shell && c == '$' && i + 1 < e && s[i + 1] == '\'' && !L.fish) {
            fill(hl, i, i + 2, H_STRING);
            st.mode = G_ANSIC;
            i += 2;
            continue;
        }
        if (c == '"' || c == '\'' || (c == '`' && (L.backtick || L.shell))) {
            if (c == '`' && L.shell) { hl[i] = H_OPERATOR; i++; cmdPos = true; continue; }
            st.mode = c == '"' ? G_DQ : c == '\'' ? G_SQ : G_TPL;
            hl[i] = H_STRING;
            i++;
            cmdPos = false;
            continue;
        }
        if (L.shell && c == '$') {
            size_t j = scanShellVar(s, i, e);
            if (j > i + 1) {
                fill(hl, i, j, H_VARIABLE);
                if (s[j - 1] == '(') cmdPos = true;
                i = j;
                continue;
            }
        }
        if (L.shell && !L.fish && c == '<' && at(s, i, e, "<<") && !at(s, i, e, "<<<")) {
            size_t j = i + 2;
            bool strip = false;
            if (j < e && s[j] == '-') { strip = true; j++; }
            while (j < e && (s[j] == ' ' || s[j] == '\t')) j++;
            char q = 0;
            if (j < e && (s[j] == '\'' || s[j] == '"')) q = s[j++];
            size_t ws = j;
            while (j < e && (isIdChar((unsigned char)s[j]) || s[j] == '-' || s[j] == '.')) j++;
            std::string term = s.substr(ws, j - ws);
            if (q && j < e && s[j] == q) j++;
            if (!term.empty()) {
                fill(hl, i, j, H_PREPROC);
                st.term = term;
                st.depth = strip ? 1 : 0;
                pendingHeredoc = true;
                i = j;
                continue;
            }
        }
        if (L.decorators && c == '@' && i + 1 < e && isIdStart((unsigned char)s[i + 1]) &&
            (i == b || !isIdChar((unsigned char)s[i - 1]))) {
            size_t j = i + 1;
            while (j < e && (isIdChar((unsigned char)s[j]) || s[j] == '.')) j++;
            fill(hl, i, j, H_PREPROC);
            i = j;
            continue;
        }
        if (isdigit(c) && (i == b || (!isIdChar((unsigned char)s[i - 1]) && !(L.shell && strchr("-./", s[i - 1]))))) {
            size_t j = i;
            if (L.shell) {
                while (j < e && isdigit((unsigned char)s[j])) j++;
                if (j < e && !isspace((unsigned char)s[j]) && !strchr(";|&)<>]", s[j])) {
                    while (j < e && !isspace((unsigned char)s[j])) j++;  // a word like 2file: not a number
                    i = j;
                    cmdPos = false;
                    continue;
                }
            } else {
                while (j < e && (isalnum((unsigned char)s[j]) || s[j] == '_' || s[j] == '.' ||
                                 ((s[j] == '-' || s[j] == '+') && (s[j - 1] == 'e' || s[j - 1] == 'E') &&
                                  !(s[i] == '0' && i + 1 < e && (s[i + 1] == 'x' || s[i + 1] == 'X')))))
                    j++;
            }
            fill(hl, i, j, H_NUMBER);
            i = j;
            cmdPos = false;
            continue;
        }
        if (isIdStart(c)) {
            size_t j = i;
            while (j < e && (isIdChar((unsigned char)s[j]) || (s[j] == '$' && (L.id == L_JS || L.id == L_TS)))) j++;
            std::string w = s.substr(i, j - i);
            std::string key = L.icase ? toLower(w) : w;
            bool boundary = true;
            char p = i > b ? s[i - 1] : ' ', n = j < e ? s[j] : ' ';
            if (L.shell) {
                if (p == '-' || p == '.' || p == '/' || p == '=' || n == '-' || n == '/' || n == '.' || n == '=')
                    boundary = false;
            } else if (p == '.' && L.id != L_SQL) boundary = false;
            Hl h = H_NORMAL;
            if (boundary && L.kw.count(key)) h = H_KEYWORD;
            else if (boundary && L.consts.count(key)) h = H_CONSTANT;
            else if (boundary && L.types.count(key)) h = H_TYPE;
            else if (boundary && L.builtins.count(key) && (!L.shell || cmdPos)) h = H_BUILTIN;
            if (L.shell && h == H_KEYWORD && !cmdPos && key != "in" && key != "do" && key != "then") {
                // keywords only count in command position (echo if -> plain word)
                if (!(L.fish && (key == "and" || key == "or" || key == "not"))) h = H_NORMAL;
            }
            if (h == H_NORMAL) {
                if (afterDefKw) {
                    h = (defKw == "class" || defKw == "interface" || defKw == "type" || defKw == "enum") ? H_TYPE : H_FUNCTION;
                } else if (!L.shell) {
                    size_t k = j;
                    while (k < e && s[k] == ' ') k++;
                    if (k < e && s[k] == '(') h = H_FUNCTION;
                } else if (cmdPos && at(s, j, e, "()")) h = H_FUNCTION;
            }
            fill(hl, i, j, h);
            afterDefKw = (h == H_KEYWORD) && (key == "def" || key == "class" || key == "function" || key == "interface" ||
                                              key == "enum" || (key == "type" && L.id == L_TS));
            if (afterDefKw) defKw = key;
            if (L.shell) cmdPos = (h == H_KEYWORD && key != "in" && key != "function");
            i = j;
            continue;
        }
        if (L.shell) {
            if (strchr(";|&(){}", (char)c)) { cmdPos = true; hl[i] = H_OPERATOR; }
            else if (c == '<' || c == '>') hl[i] = H_OPERATOR;
            else if (!isspace(c)) cmdPos = false;
        }
        if (!isspace(c)) afterDefKw = false;
        i++;
    }
    if ((st.mode == G_DQ || st.mode == G_SQ) && !L.multilineStrings) st.mode = G_NORMAL;
    if (pendingHeredoc && st.mode == G_NORMAL) st.mode = G_HEREDOC;
}

// ------------------------------------------------------------------ CSS
enum { C_NORMAL = 0, C_COMMENT, C_DQ, C_SQ };

void hlCss(const std::string& s, size_t b, size_t e, HlState& st, std::vector<uint8_t>& hl) {
    size_t i = b;
    bool inValue = false;
    while (i < e) {
        if (st.mode == C_COMMENT) {
            size_t j = s.find("*/", i);
            if (j == std::string::npos || j >= e) { fill(hl, i, e, H_COMMENT); return; }
            fill(hl, i, j + 2, H_COMMENT);
            i = j + 2;
            st.mode = C_NORMAL;
            continue;
        }
        if (st.mode == C_DQ || st.mode == C_SQ) {
            char q = st.mode == C_DQ ? '"' : '\'';
            while (i < e) {
                if (s[i] == '\\' && i + 1 < e) { hl[i] = hl[i + 1] = H_ESCAPE; i += 2; continue; }
                hl[i] = H_STRING;
                if (s[i++] == q) { st.mode = C_NORMAL; break; }
            }
            continue;
        }
        unsigned char c = (unsigned char)s[i];
        if (at(s, i, e, "/*")) { st.mode = C_COMMENT; fill(hl, i, i + 2, H_COMMENT); i += 2; continue; }
        if (at(s, i, e, "//") && (i == b || isspace((unsigned char)s[i - 1]))) { fill(hl, i, e, H_COMMENT); return; }  // scss/less
        if (c == '"' || c == '\'') { st.mode = c == '"' ? C_DQ : C_SQ; hl[i++] = H_STRING; continue; }
        if (c == '{') { st.depth++; inValue = false; i++; continue; }
        if (c == '}') { if (st.depth > 0) st.depth--; inValue = false; i++; continue; }
        if (c == ';') { inValue = false; i++; continue; }
        if (c == '@') {
            size_t j = i + 1;
            while (j < e && (isalnum((unsigned char)s[j]) || s[j] == '-')) j++;
            fill(hl, i, j, H_KEYWORD);
            i = j;
            continue;
        }
        if (at(s, i, e, "!important")) { fill(hl, i, i + 10, H_KEYWORD); i += 10; continue; }
        bool identStart = isalpha(c) || c == '_' || c == '-' || c >= 0x80;
        if (inValue) {
            if (c == '#') {
                size_t j = i + 1;
                while (j < e && isxdigit((unsigned char)s[j])) j++;
                fill(hl, i, j, H_NUMBER);
                i = j;
                continue;
            }
            if (isdigit(c) || ((c == '.' || c == '-' || c == '+') && i + 1 < e && isdigit((unsigned char)s[i + 1]))) {
                size_t j = i + 1;
                while (j < e && (isalnum((unsigned char)s[j]) || s[j] == '.' || s[j] == '%')) j++;
                fill(hl, i, j, H_NUMBER);
                i = j;
                continue;
            }
            if (identStart) {
                size_t j = i;
                while (j < e && (isalnum((unsigned char)s[j]) || s[j] == '-' || s[j] == '_' || (unsigned char)s[j] >= 0x80)) j++;
                Hl h = at(s, i, e, "--") ? H_VARIABLE : (j < e && s[j] == '(') ? H_FUNCTION : H_BUILTIN;
                fill(hl, i, j, h);
                i = j;
                continue;
            }
            i++;
            continue;
        }
        if (st.depth > 0 && identStart) {
            size_t j = i;
            while (j < e && (isalnum((unsigned char)s[j]) || s[j] == '-' || s[j] == '_' || (unsigned char)s[j] >= 0x80)) j++;
            size_t k = j;
            while (k < e && (s[k] == ' ' || s[k] == '\t')) k++;
            bool isProp = false;
            if (k < e && s[k] == ':') {
                size_t brace = s.find('{', k), semi = s.find(';', k);
                isProp = brace == std::string::npos || brace >= e || (semi != std::string::npos && semi < brace);
            } else if (k >= e) {
                isProp = false;
            }
            if (isProp) {
                fill(hl, i, j, at(s, i, e, "--") ? H_VARIABLE : H_PROPERTY);
                i = k + 1;
                inValue = true;
                continue;
            }
            fill(hl, i, j, H_TAG);
            i = j;
            continue;
        }
        // selector context
        if ((c == '.' || c == '#') && i + 1 < e && (isalpha((unsigned char)s[i + 1]) || s[i + 1] == '_' || s[i + 1] == '-')) {
            size_t j = i + 1;
            while (j < e && (isalnum((unsigned char)s[j]) || s[j] == '-' || s[j] == '_')) j++;
            fill(hl, i, j, H_ATTR);
            i = j;
            continue;
        }
        if (c == ':') {
            size_t j = i + 1;
            if (j < e && s[j] == ':') j++;
            while (j < e && (isalnum((unsigned char)s[j]) || s[j] == '-')) j++;
            fill(hl, i, j, H_BUILTIN);
            i = j;
            continue;
        }
        if (c == '[') {
            size_t j = s.find(']', i);
            j = (j == std::string::npos || j >= e) ? e : j + 1;
            fill(hl, i, j, H_ATTR);
            i = j;
            continue;
        }
        if (identStart) {
            size_t j = i;
            while (j < e && (isalnum((unsigned char)s[j]) || s[j] == '-' || s[j] == '_')) j++;
            fill(hl, i, j, H_TAG);
            i = j;
            continue;
        }
        if (strchr(">+~*,", (char)c)) hl[i] = H_OPERATOR;
        i++;
    }
}

// ------------------------------------------------------------------ HTML
enum { X_TEXT = 0, X_COMMENT, X_TAG, X_ADQ, X_ASQ, X_SCRIPT, X_STYLE };

void hlHtml(const std::string& s, HlState& st, std::vector<uint8_t>& hl) {
    size_t i = 0, e = s.size();
    while (i < e) {
        switch (st.mode) {
            case X_COMMENT: {
                size_t j = s.find("-->", i);
                if (j == std::string::npos) { fill(hl, i, e, H_COMMENT); i = e; }
                else { fill(hl, i, j + 3, H_COMMENT); i = j + 3; st.mode = X_TEXT; }
                break;
            }
            case X_SCRIPT:
            case X_STYLE: {
                bool script = st.mode == X_SCRIPT;
                size_t j = ifind(s, script ? "</script" : "</style", i);
                if (j == std::string::npos) j = e;
                HlState sub;
                sub.mode = st.sub;
                sub.depth = st.depth;
                if (script) hlGeneric(def(L_JS), s, i, j, sub, hl);
                else hlCss(s, i, j, sub, hl);
                st.sub = sub.mode;
                st.depth = sub.depth;
                i = j;
                if (j < e) { st.mode = X_TEXT; st.sub = 0; st.depth = 0; }
                break;
            }
            case X_TAG: {
                char c = s[i];
                if (isspace((unsigned char)c)) { i++; break; }
                if (c == '>') {
                    hl[i++] = H_TAG;
                    st.mode = st.term == "script" ? X_SCRIPT : st.term == "style" ? X_STYLE : X_TEXT;
                    st.sub = 0; st.depth = 0;
                    st.term.clear();
                    break;
                }
                if (c == '/' && i + 1 < e && s[i + 1] == '>') {
                    hl[i] = hl[i + 1] = H_TAG;
                    i += 2;
                    st.mode = X_TEXT;
                    st.term.clear();
                    break;
                }
                if (c == '"') { st.mode = X_ADQ; hl[i++] = H_STRING; break; }
                if (c == '\'') { st.mode = X_ASQ; hl[i++] = H_STRING; break; }
                if (c == '=') { hl[i++] = H_OPERATOR; break; }
                size_t j = i;
                while (j < e && !isspace((unsigned char)s[j]) && !strchr("=>\"'", s[j]) && !(s[j] == '/' && j + 1 < e && s[j + 1] == '>')) j++;
                if (j == i) j = i + 1;
                fill(hl, i, j, H_ATTR);
                i = j;
                break;
            }
            case X_ADQ:
            case X_ASQ: {
                char q = st.mode == X_ADQ ? '"' : '\'';
                size_t j = s.find(q, i);
                if (j == std::string::npos) { fill(hl, i, e, H_STRING); i = e; }
                else { fill(hl, i, j + 1, H_STRING); i = j + 1; st.mode = X_TAG; }
                break;
            }
            default: {
                if (at(s, i, e, "<!--")) { st.mode = X_COMMENT; fill(hl, i, i + 4, H_COMMENT); i += 4; break; }
                if (s[i] == '<' && i + 1 < e && (isalpha((unsigned char)s[i + 1]) || strchr("/!?", s[i + 1]))) {
                    size_t j = i + 1;
                    bool special = s[j] == '!' || s[j] == '?';
                    bool closing = s[j] == '/';
                    if (special || closing) j++;
                    size_t ns = j;
                    while (j < e && (isalnum((unsigned char)s[j]) || strchr("-:_.", s[j]))) j++;
                    fill(hl, i, j, special ? H_PREPROC : H_TAG);
                    st.term = closing ? "" : toLower(s.substr(ns, j - ns));
                    st.mode = X_TAG;
                    i = j;
                    break;
                }
                if (s[i] == '&') {
                    size_t j = i + 1;
                    while (j < e && j - i < 12 && (isalnum((unsigned char)s[j]) || s[j] == '#')) j++;
                    if (j < e && s[j] == ';' && j > i + 1) { fill(hl, i, j + 1, H_ESCAPE); i = j + 1; break; }
                }
                i++;
            }
        }
    }
}

// ------------------------------------------------------------------ Markdown
void hlMarkdown(const std::string& s, HlState& st, std::vector<uint8_t>& hl) {
    size_t e = s.size(), k = 0;
    while (k < e && s[k] == ' ') k++;
    bool fence = at(s, k, e, "```") || at(s, k, e, "~~~");
    if (st.mode == 1) {
        fill(hl, 0, e, fence ? H_PREPROC : H_STRING);
        if (fence) st.mode = 0;
        return;
    }
    if (fence) { fill(hl, 0, e, H_PREPROC); st.mode = 1; return; }
    if (k < e && s[k] == '#') { fill(hl, 0, e, H_KEYWORD); return; }
    if (k < e && s[k] == '>') hl[k] = H_OPERATOR;
    if (k + 1 < e && strchr("-*+", s[k]) && s[k + 1] == ' ') hl[k] = H_OPERATOR;
    size_t j = k;
    while (j < e && isdigit((unsigned char)s[j])) j++;
    if (j > k && j + 1 < e && (s[j] == '.' || s[j] == ')') && s[j + 1] == ' ') fill(hl, k, j + 1, H_OPERATOR);
    for (size_t i = k; i < e;) {
        if (s[i] == '`') {
            size_t q = s.find('`', i + 1);
            if (q != std::string::npos) { fill(hl, i, q + 1, H_STRING); i = q + 1; continue; }
        }
        if (s[i] == ']' && i + 1 < e && s[i + 1] == '(') {
            size_t q = s.find(')', i + 2);
            if (q != std::string::npos) { fill(hl, i + 1, q + 1, H_VARIABLE); i = q + 1; continue; }
        }
        if (at(s, i, e, "<!--")) {
            size_t q = s.find("-->", i);
            size_t end = q == std::string::npos ? e : q + 3;
            fill(hl, i, end, H_COMMENT);
            i = end;
            continue;
        }
        if (s[i] == '<' && (at(s, i + 1, e, "http") || at(s, i + 1, e, "mailto"))) {
            size_t q = s.find('>', i);
            if (q != std::string::npos) { fill(hl, i, q + 1, H_VARIABLE); i = q + 1; continue; }
        }
        i++;
    }
}

}  // namespace

// ------------------------------------------------------------------ public API
const char* langName(LangId id) { return def(id).name; }

bool langFromName(const std::string& nameIn, LangId& out) {
    std::string name = toLower(nameIn);
    for (auto& l : defs()) {
        if (name == l.name) { out = l.id; return true; }
        for (auto& a : l.aliases)
            if (name == a) { out = l.id; return true; }
        for (auto& x : l.exts)
            if (name == x) { out = l.id; return true; }
    }
    return false;
}

LangId detectLang(const std::string& path, const std::string& content) {
    std::string base = baseName(path);
    std::string lbase = toLower(base);
    if (!path.empty() && path != "-") {
        for (auto& l : defs())
            for (auto& f : l.filenames)
                if (base == f) return l.id;
        if (endsWith(lbase, "config.fish")) return L_FISH;
        size_t dot = lbase.find_last_of('.');
        if (dot != std::string::npos && dot + 1 < lbase.size()) {
            std::string ext = lbase.substr(dot + 1);
            for (auto& l : defs())
                for (auto& x : l.exts)
                    if (ext == x) return l.id;
        }
    }
    // shebang
    if (startsWith(content, "#!")) {
        std::string first = content.substr(0, content.find('\n'));
        std::vector<std::string> parts;
        std::istringstream is(first.substr(2));
        std::string p;
        while (is >> p) parts.push_back(p);
        std::string interp;
        for (size_t k = 0; k < parts.size(); k++) {
            std::string b = baseName(parts[k]);
            if (b == "env" || (!b.empty() && b[0] == '-')) continue;
            interp = b;
            break;
        }
        while (!interp.empty() && (isdigit((unsigned char)interp.back()) || interp.back() == '.')) interp.pop_back();
        for (auto& l : defs())
            for (auto& x : l.interps)
                if (interp == x) return l.id;
    }
    // content sniffing
    size_t k = content.find_first_not_of(" \t\r\n");
    if (k != std::string::npos) {
        std::string head = toLower(content.substr(k, 64));
        if (startsWith(head, "<!doctype html") || startsWith(head, "<html") || startsWith(head, "<?xml")) return L_HTML;
        char c = content[k];
        size_t last = content.find_last_not_of(" \t\r\n");
        if ((c == '{' && content[last] == '}') || (c == '[' && content[last] == ']')) {
            size_t q = content.find_first_not_of(" \t\r\n", k + 1);
            if (q != std::string::npos && (content[q] == '"' || content[q] == '}' || content[q] == ']' || c == '[')) return L_JSON;
        }
    }
    return L_TEXT;
}

std::string langTable() {
    std::string out;
    for (auto& l : defs()) {
        char buf[64];
        snprintf(buf, sizeof buf, "  %-11s", l.name);
        out += buf;
        std::string al;
        for (auto& a : l.aliases) al += (al.empty() ? "" : ",") + a;
        std::string ex;
        for (auto& x : l.exts) ex += (ex.empty() ? "." : " .") + x;
        out += "aliases: " + (al.empty() ? std::string("-") : al);
        out += "\n             ext: " + ex + "\n";
    }
    return out;
}

void highlightLine(LangId id, const std::string& line, HlState& st, std::vector<uint8_t>& hl) {
    hl.assign(line.size(), H_NORMAL);
    switch (id) {
        case L_TEXT: return;
        case L_HTML: hlHtml(line, st, hl); return;
        case L_CSS: hlCss(line, 0, line.size(), st, hl); return;
        case L_MARKDOWN: hlMarkdown(line, st, hl); return;
        default: hlGeneric(def(id), line, 0, line.size(), st, hl); return;
    }
}

// ---------------------------------------------------------------------------
// Syntax colour themes.
//
// The editor picks a palette with setTheme() (driven by the `theme` config
// setting: "catppuccin" | "dark" | "light"). Each palette maps a highlight
// token (H_COMMENT, H_STRING, ...) to an SGR escape sequence. The default
// "catppuccin" palette is the original hed look; "dark" is a vivid palette for
// dark terminals; "light" uses darker foregrounds that stay readable on a
// light background. Unknown theme names fall back to catppuccin.
// ---------------------------------------------------------------------------
namespace {
std::string g_theme = "catppuccin";

struct Palette {
    const char* comment, *string, *number, *keyword, *type, *builtin, *func,
        *variable, *constant, *tag, *attr, *preproc, *escape, *property, *op;
};

const Palette PAL_CAT = {
    "\x1b[3;38;5;244m", "\x1b[38;5;114m", "\x1b[38;5;215m", "\x1b[38;5;176m",
    "\x1b[38;5;80m", "\x1b[38;5;110m", "\x1b[38;5;75m", "\x1b[38;5;180m",
    "\x1b[38;5;209m", "\x1b[38;5;203m", "\x1b[38;5;179m", "\x1b[38;5;141m",
    "\x1b[38;5;208m", "\x1b[38;5;117m", "\x1b[38;5;247m",
};
const Palette PAL_DARK = {
    "\x1b[3;38;5;242m", "\x1b[38;5;150m", "\x1b[38;5;216m", "\x1b[38;5;168m",
    "\x1b[38;5;81m", "\x1b[38;5;117m", "\x1b[38;5;39m", "\x1b[38;5;186m",
    "\x1b[38;5;209m", "\x1b[38;5;203m", "\x1b[38;5;179m", "\x1b[38;5;141m",
    "\x1b[38;5;208m", "\x1b[38;5;117m", "\x1b[38;5;250m",
};
const Palette PAL_LIGHT = {
    "\x1b[3;38;5;242m", "\x1b[38;5;28m", "\x1b[38;5;130m", "\x1b[38;5;89m",
    "\x1b[38;5;23m", "\x1b[38;5;31m", "\x1b[38;5;25m", "\x1b[38;5;94m",
    "\x1b[38;5;124m", "\x1b[38;5;124m", "\x1b[38;5;94m", "\x1b[38;5;55m",
    "\x1b[38;5;130m", "\x1b[38;5;25m", "\x1b[38;5;240m",
};

const Palette* palette() {
    if (g_theme == "dark") return &PAL_DARK;
    if (g_theme == "light") return &PAL_LIGHT;
    return &PAL_CAT;
}
}  // namespace

void setTheme(const std::string& name) {
    if (name == "dark" || name == "light") g_theme = name;
    else g_theme = "catppuccin";  // unknown theme -> safe default
}

const char* hlSgr(uint8_t h) {
    const Palette* p = palette();
    switch (h) {
        case H_COMMENT: return p->comment;
        case H_STRING: return p->string;
        case H_NUMBER: return p->number;
        case H_KEYWORD: return p->keyword;
        case H_TYPE: return p->type;
        case H_BUILTIN: return p->builtin;
        case H_FUNCTION: return p->func;
        case H_VARIABLE: return p->variable;
        case H_CONSTANT: return p->constant;
        case H_TAG: return p->tag;
        case H_ATTR: return p->attr;
        case H_PREPROC: return p->preproc;
        case H_ESCAPE: return p->escape;
        case H_PROPERTY: return p->property;
        case H_OPERATOR: return p->op;
        default: return "";
    }
}

bool spellRegion(LangId id, uint8_t h) {
    switch (id) {
        case L_TEXT: return true;
        case L_MARKDOWN: return h == H_NORMAL || h == H_KEYWORD || h == H_COMMENT || h == H_OPERATOR;
        case L_HTML: return h == H_NORMAL || h == H_COMMENT;
        case L_JSON: return h == H_STRING;
        default: return h == H_COMMENT || h == H_STRING;
    }
}

CommentStyle commentStyle(LangId id) {
    switch (id) {
        case L_JS: case L_TS: case L_JSON: return {"//", "", ""};
        case L_SQL: return {"--", "", ""};
        case L_CSS: return {"", "/*", "*/"};
        case L_HTML: case L_MARKDOWN: return {"", "<!--", "-->"};
        default: return {"#", "", ""};
    }
}

bool isBraceLang(LangId id) { return id == L_JS || id == L_TS || id == L_CSS || id == L_JSON || id == L_BASH; }

int defaultIndent(LangId id) {
    switch (id) {
        case L_JS: case L_TS: case L_HTML: case L_CSS: case L_JSON: case L_MARKDOWN: return 2;
        default: return 4;
    }
}

std::string renderAnsi(const std::string& s, const std::vector<uint8_t>& hl,
                       const std::vector<std::pair<size_t, size_t>>* underline) {
    std::string out;
    std::vector<uint8_t> u;
    if (underline && !underline->empty()) {
        u.assign(s.size(), 0);
        for (auto& p : *underline)
            for (size_t k = p.first; k < p.first + p.second && k < s.size(); k++) u[k] = 1;
    }
    int cur = -1;
    for (size_t i = 0; i < s.size(); i++) {
        int uu = u.empty() ? 0 : u[i];
        int a = (i < hl.size() ? hl[i] : 0) | (uu << 8);
        if (a != cur) {
            if (cur != -1) out += "\x1b[0m";
            if (uu) out += "\x1b[4;38;5;203m";
            else out += hlSgr(i < hl.size() ? hl[i] : 0);
            cur = a;
        }
        out += s[i];
    }
    if (cur != -1) out += "\x1b[0m";
    return out;
}

}  // namespace hed
