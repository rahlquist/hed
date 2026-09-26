// SPDX-License-Identifier: MIT
// hed - heredoc replacement, agent-friendly one-shot file tool, and nano-style editor.
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "editor.hpp"
#include "highlight.hpp"
#include "spell.hpp"
#include "util.hpp"

using namespace hed;

static const char* VERSION = "1.0.0";
static bool g_color = false;

enum Exit { EX_OK = 0, EX_NOTFOUND = 1, EX_USAGE = 2, EX_AMBIG = 3 };

// ------------------------------------------------------------------ arg parsing
struct OptSpec { const char* lng; char shrt; bool arg; };
struct Parsed {
    std::vector<std::string> pos;
    std::map<std::string, std::vector<std::string>> vals;
    std::set<std::string> flags;
    bool has(const std::string& k) const { return flags.count(k) || vals.count(k); }
    std::string get(const std::string& k, const std::string& def = "") const {
        auto it = vals.find(k);
        return it == vals.end() || it->second.empty() ? def : it->second.back();
    }
};

static bool parseArgs(const std::vector<std::string>& a, size_t start, std::vector<OptSpec> spec, Parsed& p, std::string& err) {
    spec.push_back({"color", 0, true});
    spec.push_back({"no-color", 0, false});
    spec.push_back({"help", 'h', false});
    bool endOpts = false;
    for (size_t i = start; i < a.size(); i++) {
        const std::string& s = a[i];
        if (endOpts || s == "-" || s.size() < 2 || s[0] != '-' || isdigit((unsigned char)s[1])) { p.pos.push_back(s); continue; }
        if (s == "--") { endOpts = true; continue; }
        if (s[1] == '-') {
            std::string name = s.substr(2), val;
            bool hasVal = false;
            size_t eq = name.find('=');
            if (eq != std::string::npos) { val = name.substr(eq + 1); name = name.substr(0, eq); hasVal = true; }
            const OptSpec* o = nullptr;
            for (auto& x : spec) if (name == x.lng) o = &x;
            if (!o) { err = "unknown option --" + name; return false; }
            if (o->arg) {
                if (!hasVal) {
                    if (i + 1 >= a.size()) { err = "option --" + name + " needs a value"; return false; }
                    val = a[++i];
                }
                p.vals[o->lng].push_back(val);
            } else {
                if (hasVal) { err = "option --" + name + " takes no value"; return false; }
                p.flags.insert(o->lng);
            }
        } else {
            for (size_t k = 1; k < s.size(); k++) {
                const OptSpec* o = nullptr;
                for (auto& x : spec) if (x.shrt && x.shrt == s[k]) o = &x;
                if (!o) { err = std::string("unknown option -") + s[k]; return false; }
                if (o->arg) {
                    std::string val = s.substr(k + 1);
                    if (val.empty()) {
                        if (i + 1 >= a.size()) { err = std::string("option -") + s[k] + " needs a value"; return false; }
                        val = a[++i];
                    }
                    p.vals[o->lng].push_back(val);
                    break;
                }
                p.flags.insert(o->lng);
            }
        }
    }
    std::string when = p.get("color", "auto");
    if (p.has("no-color")) when = "never";
    const char* term = getenv("TERM");
    if (when == "always") g_color = true;
    else if (when == "never") g_color = false;
    else if (when == "auto") g_color = isatty(1) && !getenv("NO_COLOR") && !(term && strcmp(term, "dumb") == 0);
    else { err = "--color must be auto, always or never"; return false; }
    return true;
}

static int usageErr(const std::string& cmd, const std::string& m) {
    fprintf(stderr, "hed %s: %s\nTry 'hed %s --help'.\n", cmd.c_str(), m.c_str(), cmd.c_str());
    return EX_USAGE;
}
static int fail(const std::string& m, int code = EX_USAGE) {
    fprintf(stderr, "hed: %s\n", m.c_str());
    return code;
}

// ------------------------------------------------------------------ help text
static const char* MAIN_HELP = R"(hed %s - heredoc replacement, one-shot file tool and nano-style editor

USAGE
  hed [FILE] [+LINE[:COL]]        interactive editor (nano-like; ^G inside for help)
  cmd | hed [| cmd2]              edit piped text; ^X writes the result to stdout
  hed <command> [options] [args]  one-shot mode: never prompts, safe for scripts/agents

COMMANDS
  write   FILE            write stdin or -c TEXT to FILE (replaces cat > FILE <<'EOF')
  show    [FILE...]       print with syntax colors, line numbers (-n) and ranges (-r)
  search  PATTERN [FILE...]  find matches, grep-style output or --json
  replace FILE OLD NEW    exact-text replace; must match exactly once unless --all/--nth
  insert  FILE            insert stdin or -c TEXT at a line or relative to a match
  delete  FILE            delete a line range or lines matching a pattern
  spell   [FILE...]       spell-check (code: comments+strings only; prose: everything)
  langs                   list languages for syntax highlighting / --lang
  config                  view or edit ~/.config/hed/config (editor settings)
  log     [N]             show the last N status messages from the editor log
  edit    [FILE]          interactive editor (use when FILE is named like a command)

  'hed <command> --help' shows every option of a command.

LANGUAGES  python bash fish javascript typescript sql html css json markdown text
EXIT CODES 0 ok | 1 no match / misspellings found | 2 usage or I/O error | 3 ambiguous match or --expect mismatch
COLOR      --color=auto|always|never (auto honours NO_COLOR and non-tty output)
FILES      ~/.config/hed/words.txt personal dictionary; HED_DICT replaces the built-in dictionary,
           HED_EXTRA_DICTS=a.txt:b.txt adds word lists (one word per line, optional frequency)
)";

static const char* WRITE_HELP = R"(hed write FILE [options]      (aliases: put, w)

Writes text to FILE atomically (temp file + rename, keeps the old file's permissions).
Text comes from, in priority order: -c/--content, --from FILE, or stdin.
Text is literal, like a quoted heredoc (<<'EOF'): no $var or backslash expansion unless -e.
A trailing newline is added if missing (like a heredoc) unless --no-eol. FILE '-' = stdout.

  -c, --content TEXT   text to write (repeat -c to write several lines)
  -f, --from FILE      read text from FILE ('-' = stdin)
  -a, --append         append instead of overwrite
  -d, --dedent         strip common indentation + a leading/trailing blank line
                       (lets you indent a multi-line -c '...' block inside a script)
  -t, --strip-tabs     strip leading tabs from each line (like <<-EOF)
  -e, --escapes        interpret \n \t \\ \xHH \uHHHH in the text
  -m, --mode OCTAL     set file mode, e.g. 755
  -p, --parents        create missing parent directories
  -b, --backup         copy existing FILE to DIR/YYYYmmddHHMMSS-NAME first
  -n, --no-clobber     fail (exit 1) if FILE already exists
      --no-eol         do not add a trailing newline
  -D, --diff           print a unified diff against the previous content
  -s, --show           print the written file with highlighting and line numbers
      --spell          report misspellings in the written text (stderr; exit code unaffected)
  -l, --lang LANG      language for --show/--spell (default: detect)
  -q, --quiet          no summary line on stderr

EXAMPLES
  hed write app.py <<'EOF'            # bash/zsh heredoc still works
  printf '%%s\n' 'a' 'b' | hed write list.txt
  hed write -p -m 755 bin/run.sh -d -c '
      #!/usr/bin/env bash
      echo "hi from $USER"
  '                                   # works identically in bash and fish
)";

static const char* SHOW_HELP = R"(hed show [FILE...] [options]      (aliases: cat, view)

Prints files (or stdin) with syntax highlighting when stdout is a terminal.

  -n, --number         prefix line numbers (tab-separated when not colored)
  -r, --range SPEC     only lines SPEC: N  A:B  A:  :B  A:+N  -N: (last N lines)
  -l, --lang LANG      force language (see 'hed langs')
  -s, --spell          underline misspelled words (needs color)
      --color WHEN     auto|always|never
)";

static const char* SEARCH_HELP = R"(hed search PATTERN [FILE...] [options]      (aliases: grep, find)

Literal search by default. Reads stdin when no FILE is given. Output: [FILE:]LINE:COL:TEXT

  -E, --regex          PATTERN is an ECMAScript regular expression
  -i, --ignore-case    case-insensitive
  -S, --smart-case     case-insensitive unless PATTERN has uppercase
  -w, --word           match whole words only
  -C, --context N      show N lines of context
  -c, --count          print match counts only
  -l, --files          print names of files with matches only
  -m, --max N          stop after N matches per file
      --json           JSON array: [{file,line,col,length,text}]
Exit status: 0 found, 1 not found, 2 error.
)";

static const char* REPLACE_HELP = R"(hed replace FILE OLD NEW [options]      (alias: sub)

Replaces text. By default OLD must occur EXACTLY ONCE (exit 3 if ambiguous, 1 if absent):
add surrounding context to OLD, or use --all / --nth. OLD/NEW may span lines.
Prints a unified diff of the change to stdout (unless -q).

  --old TEXT / --new TEXT        alternative to positional OLD/NEW
  --old-file F / --new-file F    read OLD/NEW literally from a file ('-' = stdin)
  -a, --all            replace every occurrence
      --nth N          replace only the Nth occurrence (1-based)
      --expect N       fail (exit 3) unless exactly N occurrences would be replaced
  -E, --regex          OLD is an ECMAScript regex; NEW may use $1 $2 $& (multiline: ^ $ per line)
  -i, --ignore-case    case-insensitive match
  -r, --range SPEC     only match inside these lines (see 'show --range')
  -e, --escapes        interpret \n \t etc. in inline OLD/NEW
  -n, --dry-run        show the diff, do not write
  -b, --backup         timestamped backup before writing
  -C, --context N      diff context lines (default 3)
  -q, --quiet          no diff / summary
      --json           machine-readable result
CRLF files are handled transparently (match with plain \n).
)";

static const char* INSERT_HELP = R"(hed insert FILE POSITION [options]      (alias: ins)

Inserts stdin / -c TEXT as whole lines. Exactly one POSITION:
  --at N / --before N  new text becomes line N (N = last+1 appends)
  --after N            after line N (0 = top of file)
  --start / --end      top / bottom of file
  --after-match TEXT   after the line containing TEXT (must be unique unless --first)
  --before-match TEXT  before the line containing TEXT
  --first              use the first matching line if several match

  -c, --content TEXT   text to insert (repeatable, one line each); else --from FILE or stdin
  -f, --from FILE      read text from FILE
  -I, --match-indent   re-indent the (dedented) block to the anchor line's indentation
  -d, --dedent  -t, --strip-tabs  -e, --escapes   same as 'hed write'
  -n, --dry-run  -b, --backup  -q, --quiet  -C N   same as 'hed replace'
)";

static const char* DELETE_HELP = R"(hed delete FILE (--range SPEC | --match TEXT) [options]      (aliases: del)

  -r, --range SPEC     delete lines SPEC (N, A:B, A:+N, -N: ...)
      --match TEXT     delete the line containing TEXT (unique unless --all)
  -E, --regex          --match is a regex
  -a, --all            delete every matching line
  -n, --dry-run  -b, --backup  -q, --quiet  -C N
)";

static const char* SPELL_HELP = R"(hed spell [FILE...] [options]

Spell-checks files or stdin. In source code only comments and string literals are
checked; identifiers, paths, URLs, camelCase and ACRONYMS are skipped. Markdown/text/
HTML text content is checked fully. Output: [FILE:]LINE:COL: word -> suggestions

  -l, --lang LANG      force language
  -a, --all-text       check every word, ignoring code structure
  -k, --suggest N      suggestions per word (default 5, 0 = none)
  -w, --words          print only the unique misspelled words
      --add WORD       add WORD to ~/.config/hed/words.txt (repeatable; no FILE needed)
      --json           JSON array: [{file,line,col,word,suggestions}]
  -q, --quiet          no output, exit status only
Exit status: 0 clean, 1 misspellings found, 2 error.
)";

static const char* EDIT_HELP = R"(hed [edit] [FILE] [options]

  +N, +N:C             open at line N (negative = from end), column C
  -l, --lang LANG      force language
  -T, --tabsize N      tab display width (default 4)
      --tabs           indent with tabs      --spaces N   indent with N spaces
      --no-spell       start with spell underlining off
      --no-numbers     hide line numbers
  -R, --readonly       view only
Keys (nano-like): ^S save ^O save-as ^X exit ^W find ^R replace ^K cut ^U paste ^Z undo
^Y redo ^_ go-to-line ^T spell ^^ mark M-3 comment. ^G inside the editor shows everything.
Piping: 'cmd | hed', 'hed > out.txt' and 'cmd | hed | cmd2' open the editor on the
terminal and emit the final buffer on stdout when you press ^X (^Q aborts, exit 1).
)";

static const char* CONFIG_HELP = R"(hed config [get KEY | set KEY VALUE]

Views or edits the editor configuration file (~/.config/hed/config, or
$XDG_CONFIG_HOME/hed/config). The file is INI-style with an [editor] section.

  hed config              print the config file path and the effective settings
  hed config get KEY      print one setting
  hed config set KEY VAL  set one setting and save it to the file

Settings (precedence: command-line flags > HED_* env vars > this file > defaults):
  tabsize   tab display width (1-32)
  indent    auto | tabs | spaces
  spaces    indent width in columns when indent = spaces (1-16)
  spell     true | false
  numbers   true | false
  theme     catppuccin | dark | light
  log_size  status messages kept in memory / shown by 'hed log' (0 = unlimited)
)";

static const char* LOG_HELP = R"(hed log [N] [options]

Shows the last N status messages the editor logged to
~/.config/hed/log/YYYY-MM-DD.log (one file per day). Default N = 50.

  -n, --lines N   number of messages to show (default 50)
)";

static const char* LANGS_HELP = R"(hed langs      (aliases: languages)

Lists the languages hed knows for syntax highlighting and --lang.

  python bash fish javascript typescript sql html css json markdown text
)";

static bool wantsHelp(const std::vector<std::string>& a) {
    for (auto& s : a) {
        if (s == "--") return false;
        if (s == "-h" || s == "--help") return true;
    }
    return false;
}

// ------------------------------------------------------------------ shared helpers
static bool readInput(const std::string& name, std::string& out, std::string& err) {
    if (name == "-") {
        if (!readFd(0, out)) { err = "stdin: read error"; return false; }
        return true;
    }
    return readFile(name, out, err);
}

static bool getText(const Parsed& p, std::string& out, std::string& err, bool& inlineSrc) {
    inlineSrc = false;
    if (p.vals.count("content")) {
        const auto& v = p.vals.at("content");
        out.clear();
        for (size_t i = 0; i < v.size(); i++) { if (i) out += '\n'; out += v[i]; }
        inlineSrc = true;
    } else if (p.vals.count("from")) {
        if (!readInput(p.get("from"), out, err)) return false;
    } else {
        if (isatty(0)) fprintf(stderr, "hed: reading text from the terminal; finish with Ctrl-D (or use -c TEXT)\n");
        if (!readFd(0, out)) { err = "stdin: read error"; return false; }
    }
    if (p.has("escapes")) out = interpretEscapes(out);
    if (p.has("strip-tabs")) out = stripLeadingTabs(out);
    if (p.has("dedent")) out = dedent(out);
    return true;
}

static bool resolveLang(const Parsed& p, const std::string& file, const std::string& content, LangId& out, std::string& err) {
    if (p.vals.count("lang")) {
        if (!langFromName(p.get("lang"), out)) { err = "unknown language '" + p.get("lang") + "' (see 'hed langs')"; return false; }
        return true;
    }
    out = detectLang(file == "-" ? "" : file, content);
    return true;
}

static std::vector<std::string> linesOf(const std::string& s) {
    Text t = splitText(s);
    if (t.lines.size() == 1 && t.lines[0].empty() && !t.trailingNewline) t.lines.clear();
    return t.lines;
}

static std::string plural(size_t n, const char* w) { return std::to_string(n) + " " + w + (n == 1 ? "" : "s"); }

static void printDiff(const std::string& before, const std::string& after, const std::string& file, int ctx) {
    std::string d = unifiedDiff(linesOf(before), linesOf(after), "a/" + file, "b/" + file, ctx, g_color);
    fwrite(d.data(), 1, d.size(), stdout);
}

static int parseInt(const std::string& s, long& v) {
    char* e = nullptr;
    errno = 0;
    v = strtol(s.c_str(), &e, 10);
    return s.empty() || *e || errno ? 0 : 1;
}

static std::string showText(const std::string& content, LangId lang, bool number, size_t a, size_t b, bool spell) {
    Text t = splitText(content);
    std::vector<std::string>& L = t.lines;
    if (L.size() == 1 && L[0].empty() && !t.trailingNewline) return "";
    if (b == 0 || b > L.size()) b = L.size();
    int width = (int)std::to_string(b).size();
    HlState st;
    std::vector<uint8_t> hl;
    Speller& sp = Speller::instance();
    if (spell && g_color) sp.ensureLoaded();
    std::string out;
    for (size_t i = 0; i < b; i++) {
        if (g_color) highlightLine(lang, L[i], st, hl);
        if (i + 1 < a) continue;
        if (number) {
            char buf[96];
            if (g_color) snprintf(buf, sizeof buf, "\x1b[38;5;240m%*zu \x1b[38;5;237m\xe2\x94\x82\x1b[0m ", width, i + 1);
            else snprintf(buf, sizeof buf, "%*zu\t", width, i + 1);
            out += buf;
        }
        if (g_color) {
            std::vector<std::pair<size_t, size_t>> u;
            if (spell)
                for (auto& m : findMisspellings(sp, lang, L[i], hl, false)) u.push_back({m.start, m.len});
            out += renderAnsi(L[i], hl, &u);
        } else out += L[i];
        if (i + 1 < L.size() || t.trailingNewline) out += '\n';
        else if (number) out += '\n';
    }
    return out;
}

// ------------------------------------------------------------------ write
static int cmdWrite(const std::vector<std::string>& a) {
    if (wantsHelp(a)) { printf("%s", WRITE_HELP); return 0; }
    Parsed p;
    std::string err;
    if (!parseArgs(a, 1, {{"content", 'c', true}, {"from", 'f', true}, {"append", 'a', false}, {"mode", 'm', true},
                          {"parents", 'p', false}, {"backup", 'b', false}, {"no-clobber", 'n', false},
                          {"strip-tabs", 't', false}, {"dedent", 'd', false}, {"escapes", 'e', false},
                          {"no-eol", 0, false}, {"quiet", 'q', false}, {"show", 's', false}, {"diff", 'D', false},
                          {"spell", 0, false}, {"lang", 'l', true}},
                   p, err))
        return usageErr("write", err);
    if (p.pos.size() != 1) return usageErr("write", p.pos.empty() ? "missing FILE" : "expected exactly one FILE (quote text and pass it with -c)");
    std::string file = p.pos[0];
    std::string content;
    bool inl;
    if (!getText(p, content, err, inl)) return fail(err);
    if (!p.has("no-eol") && !content.empty() && content.back() != '\n') content += '\n';
    if (file == "-") { writeAll(1, content); return 0; }
    int mode = -1;
    if (p.has("mode")) {
        char* e = nullptr;
        long m = strtol(p.get("mode").c_str(), &e, 8);
        if (!e || *e || m < 0 || m > 07777) return usageErr("write", "bad --mode (octal, e.g. 644 or 0755)");
        mode = (int)m;
    }
    bool exists = pathExists(file);
    if (exists && isDir(file)) return fail(file + " is a directory");
    if (exists && p.has("no-clobber")) return fail(file + " already exists (--no-clobber)", EX_NOTFOUND);
    std::string dir = dirName(file);
    if (p.has("parents")) { if (!mkdirParents(dir, err)) return fail(err); }
    else if (!isDir(dir)) return fail("directory does not exist: " + dir + " (use -p to create it)");
    std::string old, backup;
    if (exists && !readFile(file, old, err)) return fail(err);
    if (exists && p.has("backup") && !makeBackup(file, backup, err)) return fail("backup failed: " + err);
    bool append = p.has("append");
    bool ok = append ? appendToFile(file, content, err, mode) : atomicWrite(file, content, err, mode);
    if (!ok) return fail(err);
    std::string final = append ? old + content : content;
    if (!p.has("quiet")) {
        size_t nl = (size_t)std::count(content.begin(), content.end(), '\n') + (!content.empty() && content.back() != '\n');
        std::string what = plural(nl, "line") + " (" + plural(content.size(), "byte") + ")";
        std::string line = append ? "appended " + what + " to " + file
                                  : (exists ? "overwrote " + file + " with " + what : "wrote " + what + " to " + file);
        if (!backup.empty()) line += " (backup: " + backup + ")";
        fprintf(stderr, "hed: %s\n", line.c_str());
    }
    if (p.has("diff") && exists) printDiff(old, final, file, 3);
    LangId lang;
    if (!resolveLang(p, file, final, lang, err)) return usageErr("write", err);
    if (p.has("show")) { std::string s = showText(final, lang, true, 1, 0, false); fwrite(s.data(), 1, s.size(), stdout); }
    if (p.has("spell")) {
        Speller& sp = Speller::instance();
        if (sp.ensureLoaded()) {
            Text t = splitText(content);
            HlState st;
            std::vector<uint8_t> hl;
            for (size_t i = 0; i < t.lines.size(); i++) {
                highlightLine(lang, t.lines[i], st, hl);
                for (auto& m : findMisspellings(sp, lang, t.lines[i], hl, false)) {
                    auto s = sp.suggest(m.word, 3);
                    std::string j;
                    for (auto& x : s) j += (j.empty() ? "" : ", ") + x;
                    fprintf(stderr, "hed: spell: %s:%zu:%zu: %s -> %s\n", file.c_str(), i + 1, m.start + 1, m.word.c_str(),
                            j.empty() ? "?" : j.c_str());
                }
            }
        }
    }
    return 0;
}

// ------------------------------------------------------------------ show
static int cmdShow(const std::vector<std::string>& a) {
    if (wantsHelp(a)) { printf("%s", SHOW_HELP); return 0; }
    Parsed p;
    std::string err;
    if (!parseArgs(a, 1, {{"number", 'n', false}, {"range", 'r', true}, {"lang", 'l', true}, {"spell", 's', false}}, p, err))
        return usageErr("show", err);
    std::vector<std::string> files = p.pos;
    if (files.empty()) {
        if (isatty(0)) return usageErr("show", "no FILE given and nothing piped on stdin");
        files.push_back("-");
    }
    int rc = 0;
    for (size_t f = 0; f < files.size(); f++) {
        std::string content;
        if (!readInput(files[f], content, err)) { fprintf(stderr, "hed: %s\n", err.c_str()); rc = EX_USAGE; continue; }
        LangId lang;
        if (!resolveLang(p, files[f], content, lang, err)) return usageErr("show", err);
        size_t A = 1, B = 0;
        if (p.has("range")) {
            size_t total = linesOf(content).size();
            if (!parseRange(p.get("range"), total, A, B, err)) { fprintf(stderr, "hed: %s: %s\n", files[f].c_str(), err.c_str()); rc = EX_USAGE; continue; }
            if (B == 0) continue;
        }
        if (files.size() > 1) printf("%s==> %s <==%s\n", g_color ? "\x1b[1m" : "", files[f].c_str(), g_color ? "\x1b[0m" : "");
        std::string s = showText(content, lang, p.has("number"), A, B, p.has("spell"));
        fwrite(s.data(), 1, s.size(), stdout);
    }
    return rc;
}

// ------------------------------------------------------------------ search
struct Hit { size_t line, col, len; };

static int cmdSearch(const std::vector<std::string>& a) {
    if (wantsHelp(a)) { printf("%s", SEARCH_HELP); return 0; }
    Parsed p;
    std::string err;
    if (!parseArgs(a, 1, {{"regex", 'E', false}, {"ignore-case", 'i', false}, {"smart-case", 'S', false}, {"word", 'w', false},
                          {"context", 'C', true}, {"count", 'c', false}, {"files", 'l', false}, {"max", 'm', true},
                          {"json", 0, false}, {"fixed", 'F', false}, {"line-number", 'n', false}},
                   p, err))
        return usageErr("search", err);
    if (p.pos.empty()) return usageErr("search", "missing PATTERN");
    std::string pat = p.pos[0];
    std::vector<std::string> files(p.pos.begin() + 1, p.pos.end());
    if (files.empty()) {
        if (isatty(0)) return usageErr("search", "no FILE given and nothing piped on stdin");
        files.push_back("-");
    }
    bool icase = p.has("ignore-case") || (p.has("smart-case") && !hasUpper(pat));
    bool re = p.has("regex") && !p.has("fixed"), word = p.has("word");
    long ctx = 0, maxN = -1;
    if (p.has("context") && !parseInt(p.get("context"), ctx)) return usageErr("search", "bad --context");
    if (p.has("max") && !parseInt(p.get("max"), maxN)) return usageErr("search", "bad --max");
    std::regex rx;
    if (re) {
        try {
            auto fl = std::regex::ECMAScript | (icase ? std::regex::icase : std::regex::ECMAScript);
            rx = std::regex(word ? "\\b(?:" + pat + ")\\b" : pat, fl);
        } catch (const std::regex_error& e) { return fail(std::string("bad regex: ") + e.what()); }
    }
    std::string npat = icase ? toLower(pat) : pat;
    auto isW = [](char c) { return isalnum((unsigned char)c) || c == '_'; };
    bool multi = files.size() > 1, any = false, json = p.has("json");
    std::string jsonOut = "[";
    bool firstJ = true;
    const char *MAG = g_color ? "\x1b[35m" : "", *GRN = g_color ? "\x1b[32m" : "", *R = g_color ? "\x1b[0m" : "",
               *HL = g_color ? "\x1b[1;31m" : "", *DIM = g_color ? "\x1b[38;5;244m" : "";
    int rc = 0;
    for (auto& f : files) {
        std::string content;
        if (!readInput(f, content, err)) { fprintf(stderr, "hed: %s\n", err.c_str()); rc = EX_USAGE; continue; }
        std::vector<std::string> L = linesOf(content);
        std::vector<Hit> hits;
        for (size_t i = 0; i < L.size() && (maxN < 0 || (long)hits.size() < maxN); i++) {
            const std::string& s = L[i];
            if (re) {
                for (auto it = std::sregex_iterator(s.begin(), s.end(), rx); it != std::sregex_iterator(); ++it) {
                    if (it->length() == 0 && s.size() > 0) continue;
                    hits.push_back({i, (size_t)it->position(), (size_t)it->length()});
                    if (maxN >= 0 && (long)hits.size() >= maxN) break;
                }
            } else {
                std::string h = icase ? toLower(s) : s;
                size_t pos = 0;
                while (!npat.empty() && (pos = h.find(npat, pos)) != std::string::npos) {
                    bool ok = !word || ((pos == 0 || !isW(h[pos - 1])) && (pos + npat.size() >= h.size() || !isW(h[pos + npat.size()])));
                    if (ok) { hits.push_back({i, pos, npat.size()}); if (maxN >= 0 && (long)hits.size() >= maxN) break; }
                    pos += npat.size();
                }
            }
        }
        if (!hits.empty()) any = true;
        std::string fname = f == "-" ? "(stdin)" : f;
        if (json) {
            for (auto& h : hits) {
                jsonOut += std::string(firstJ ? "\n" : ",\n") + "  {\"file\":" + jsonEscape(fname) + ",\"line\":" + std::to_string(h.line + 1) +
                           ",\"col\":" + std::to_string(h.col + 1) + ",\"length\":" + std::to_string(h.len) +
                           ",\"text\":" + jsonEscape(L[h.line]) + "}";
                firstJ = false;
            }
            continue;
        }
        if (p.has("files")) { if (!hits.empty()) printf("%s%s%s\n", MAG, fname.c_str(), R); continue; }
        if (p.has("count")) {
            if (multi) printf("%s%s%s:", MAG, fname.c_str(), R);
            printf("%zu\n", hits.size());
            continue;
        }
        // group hits by line
        std::vector<size_t> hitLines;
        for (auto& h : hits) if (hitLines.empty() || hitLines.back() != h.line) hitLines.push_back(h.line);
        long lastPrinted = -1;
        for (size_t hi = 0; hi < hitLines.size(); hi++) {
            size_t ln = hitLines[hi];
            size_t from = ln >= (size_t)ctx ? ln - (size_t)ctx : 0;
            if (lastPrinted >= 0 && (long)from > lastPrinted + 1 && ctx > 0) printf("%s--%s\n", DIM, R);
            for (size_t k = std::max<long>((long)from, lastPrinted + 1); k <= std::min(L.size() - 1, ln + (size_t)ctx); k++) {
                bool isHit = std::binary_search(hitLines.begin(), hitLines.end(), k);
                if (multi) printf("%s%s%s%c", MAG, fname.c_str(), R, isHit ? ':' : '-');
                if (isHit) {
                    size_t firstCol = 0;
                    for (auto& h : hits) if (h.line == k) { firstCol = h.col; break; }
                    printf("%s%zu%s:%zu:", GRN, k + 1, R, firstCol + 1);
                    std::string out;
                    size_t last = 0;
                    for (auto& h : hits) {
                        if (h.line != k) continue;
                        out += L[k].substr(last, h.col - last) + HL + L[k].substr(h.col, h.len) + R;
                        last = h.col + h.len;
                    }
                    out += L[k].substr(last);
                    printf("%s\n", out.c_str());
                } else printf("%s%zu-%s%s\n", GRN, k + 1, R, L[k].c_str());
                lastPrinted = (long)k;
            }
        }
    }
    if (json) printf("%s%s]\n", jsonOut.c_str(), firstJ ? "" : "\n");
    if (rc) return rc;
    return any ? EX_OK : EX_NOTFOUND;
}

// ------------------------------------------------------------------ edit helpers (replace/insert/delete)
struct FileBuf {
    std::string raw;   // as on disk
    std::string text;  // LF-normalised
    bool crlf = false;
};

static bool loadBuf(const std::string& file, FileBuf& fb, std::string& err) {
    if (!pathExists(file)) { err = file + ": no such file (use 'hed write' to create it)"; return false; }
    if (!readFile(file, fb.raw, err)) return false;
    Text t = splitText(fb.raw);
    fb.crlf = t.crlf;
    if (t.crlf) { t.crlf = false; fb.text = joinText(t); }
    else fb.text = fb.raw;
    return true;
}

static std::string toDisk(const FileBuf& fb, const std::string& text) {
    if (!fb.crlf) return text;
    Text t = splitText(text);
    t.crlf = true;
    return joinText(t);
}

static int finishEdit(const Parsed& p, const std::string& file, const FileBuf& fb, const std::string& newText,
                      const std::string& summary, const std::string& jsonExtra) {
    long ctx = 3;
    if (p.has("context")) parseInt(p.get("context"), ctx);
    bool dry = p.has("dry-run"), quiet = p.has("quiet"), json = p.has("json");
    std::string backup, err;
    bool changed = newText != fb.text;
    if (!dry && changed) {
        if (p.has("backup") && !makeBackup(file, backup, err)) return fail("backup failed: " + err);
        if (!atomicWrite(file, toDisk(fb, newText), err)) return fail(err);
    }
    if (json) {
        bool col = g_color;
        g_color = false;
        std::string d = unifiedDiff(linesOf(fb.text), linesOf(newText), "a/" + file, "b/" + file, (int)ctx, false);
        g_color = col;
        printf("{\"file\":%s,\"changed\":%s,\"written\":%s%s,\"backup\":%s,\"diff\":%s}\n", jsonEscape(file).c_str(),
               changed ? "true" : "false", (!dry && changed) ? "true" : "false", jsonExtra.c_str(),
               backup.empty() ? "null" : jsonEscape(backup).c_str(), jsonEscape(d).c_str());
        return 0;
    }
    if (!quiet) {
        if (changed) printDiff(fb.text, newText, file, (int)ctx);
        fprintf(stderr, "hed: %s%s%s\n", dry ? "[dry-run] would have " : "", summary.c_str(),
                changed ? (backup.empty() ? "" : (" (backup: " + backup + ")").c_str()) : " (content unchanged)");
    }
    return 0;
}

static size_t lineOfPos(const std::string& s, size_t pos) { return (size_t)std::count(s.begin(), s.begin() + (long)std::min(pos, s.size()), '\n') + 1; }

// ------------------------------------------------------------------ replace
static int cmdReplace(const std::vector<std::string>& a) {
    if (wantsHelp(a)) { printf("%s", REPLACE_HELP); return 0; }
    Parsed p;
    std::string err;
    if (!parseArgs(a, 1, {{"old", 0, true}, {"new", 0, true}, {"old-file", 0, true}, {"new-file", 0, true},
                          {"all", 'a', false}, {"nth", 0, true}, {"expect", 0, true}, {"regex", 'E', false},
                          {"ignore-case", 'i', false}, {"range", 'r', true}, {"escapes", 'e', false},
                          {"dry-run", 'n', false}, {"backup", 'b', false}, {"context", 'C', true}, {"quiet", 'q', false},
                          {"json", 0, false}},
                   p, err))
        return usageErr("replace", err);
    if (p.pos.empty()) return usageErr("replace", "missing FILE");
    std::string file = p.pos[0];
    std::string oldS, newS;
    bool haveOld = false, haveNew = false, oldInline = false, newInline = false;
    if (p.pos.size() > 1) { oldS = p.pos[1]; haveOld = oldInline = true; }
    if (p.pos.size() > 2) { newS = p.pos[2]; haveNew = newInline = true; }
    if (p.pos.size() > 3) return usageErr("replace", "too many arguments (quote OLD and NEW)");
    if (p.has("old")) { oldS = p.get("old"); haveOld = oldInline = true; }
    if (p.has("new")) { newS = p.get("new"); haveNew = newInline = true; }
    if (p.has("old-file")) { if (!readInput(p.get("old-file"), oldS, err)) return fail(err); haveOld = true; oldInline = false; }
    if (p.has("new-file")) { if (!readInput(p.get("new-file"), newS, err)) return fail(err); haveNew = true; newInline = false; }
    if (!haveOld) return usageErr("replace", "missing OLD text");
    if (!haveNew) return usageErr("replace", "missing NEW text (use \"\" to delete)");
    if (p.has("escapes")) {
        if (oldInline) oldS = interpretEscapes(oldS);
        if (newInline) newS = interpretEscapes(newS);
    }
    if (oldS.empty()) return usageErr("replace", "OLD must not be empty");
    FileBuf fb;
    if (!loadBuf(file, fb, err)) return fail(err);
    const std::string& text = fb.text;
    bool icase = p.has("ignore-case"), re = p.has("regex");
    struct M { size_t pos, len; std::string repl; };
    std::vector<M> ms;
    if (re) {
        try {
            auto fl = std::regex::ECMAScript | std::regex::multiline;
            if (icase) fl |= std::regex::icase;
            std::regex rx(oldS, fl);
            for (auto it = std::sregex_iterator(text.begin(), text.end(), rx); it != std::sregex_iterator(); ++it) {
                if (it->length() == 0) continue;
                ms.push_back({(size_t)it->position(), (size_t)it->length(), it->format(newS)});
            }
        } catch (const std::regex_error& e) { return fail(std::string("bad regex: ") + e.what()); }
    } else {
        std::string h = icase ? toLower(text) : text, n = icase ? toLower(oldS) : oldS;
        size_t pos = 0;
        while ((pos = h.find(n, pos)) != std::string::npos) { ms.push_back({pos, n.size(), newS}); pos += n.size(); }
    }
    if (p.has("range")) {
        std::vector<std::string> L = linesOf(text);
        size_t A, B;
        if (!parseRange(p.get("range"), L.size(), A, B, err)) return usageErr("replace", err);
        std::vector<M> keep;
        for (auto& m : ms) {
            size_t l1 = lineOfPos(text, m.pos), l2 = lineOfPos(text, m.pos + m.len - 1);
            if (l1 >= A && l2 <= B) keep.push_back(m);
        }
        ms.swap(keep);
    }
    if (ms.empty()) {
        std::string msg = "no match for the OLD text in " + file;
        if (!re) {  // whitespace-insensitive hint
            auto norm = [](const std::string& s, std::vector<size_t>* map) {
                std::string o;
                bool ws = false;
                for (size_t i = 0; i < s.size(); i++) {
                    if (isspace((unsigned char)s[i])) { ws = true; continue; }
                    if (ws && !o.empty()) { o += ' '; if (map) map->push_back(i); }
                    ws = false;
                    o += s[i];
                    if (map) map->push_back(i);
                }
                return o;
            };
            std::vector<size_t> map;
            std::string nt = norm(icase ? toLower(text) : text, &map), no = norm(icase ? toLower(oldS) : oldS, nullptr);
            size_t k = no.empty() ? std::string::npos : nt.find(no);
            if (k != std::string::npos) {
                size_t ln = lineOfPos(text, map[k]);
                msg += "\nhint: it matches at line " + std::to_string(ln) +
                       " if whitespace/indentation is ignored. Copy the exact text, e.g.: hed show " + file + " -n -r " +
                       std::to_string(ln) + ":+" + std::to_string(std::count(oldS.begin(), oldS.end(), '\n') + 1);
            } else if (!icase && toLower(text).find(toLower(oldS)) != std::string::npos) {
                msg += "\nhint: a case-insensitive match exists (add -i)";
            }
        }
        if (p.has("json")) { printf("{\"file\":%s,\"changed\":false,\"matches\":0,\"error\":%s}\n", jsonEscape(file).c_str(), jsonEscape(msg).c_str()); return EX_NOTFOUND; }
        return fail(msg, EX_NOTFOUND);
    }
    std::vector<M> sel;
    if (p.has("nth")) {
        long n;
        if (!parseInt(p.get("nth"), n) || n < 1) return usageErr("replace", "bad --nth");
        if ((size_t)n > ms.size()) return fail("--nth " + std::to_string(n) + " but only " + plural(ms.size(), "match") + " found", EX_NOTFOUND);
        sel.push_back(ms[(size_t)n - 1]);
    } else if (p.has("all") || p.has("expect")) sel = ms;
    else if (ms.size() > 1) {
        std::string lines;
        std::vector<size_t> seen;
        for (auto& m : ms) { size_t l = lineOfPos(text, m.pos); if (seen.empty() || seen.back() != l) seen.push_back(l); }
        for (size_t i = 0; i < seen.size() && i < 12; i++) lines += (i ? ", " : "") + std::to_string(seen[i]);
        if (seen.size() > 12) lines += ", ...";
        std::string m = "OLD matches " + std::to_string(ms.size()) + " times in " + file + " (lines " + lines +
                        "). Add surrounding context to make it unique, or use --all / --nth N.";
        if (p.has("json")) { printf("{\"file\":%s,\"changed\":false,\"matches\":%zu,\"error\":%s}\n", jsonEscape(file).c_str(), ms.size(), jsonEscape(m).c_str()); return EX_AMBIG; }
        return fail(m, EX_AMBIG);
    } else sel = ms;
    if (p.has("expect")) {
        long n;
        if (!parseInt(p.get("expect"), n)) return usageErr("replace", "bad --expect");
        if ((size_t)n != sel.size()) return fail("--expect " + std::to_string(n) + " but " + plural(sel.size(), "match") + " found; nothing written", EX_AMBIG);
    }
    std::string out;
    size_t last = 0;
    std::string lineList;
    for (auto& m : sel) {
        out += text.substr(last, m.pos - last) + m.repl;
        last = m.pos + m.len;
        lineList += (lineList.empty() ? "" : ",") + std::to_string(lineOfPos(text, m.pos));
    }
    out += text.substr(last);
    return finishEdit(p, file, fb, out,
                      "replaced " + plural(sel.size(), "occurrence") + " in " + file + " (line " + lineList + ")",
                      ",\"replacements\":" + std::to_string(sel.size()) + ",\"lines\":[" + lineList + "]");
}

// ------------------------------------------------------------------ insert
static int cmdInsert(const std::vector<std::string>& a) {
    if (wantsHelp(a)) { printf("%s", INSERT_HELP); return 0; }
    Parsed p;
    std::string err;
    if (!parseArgs(a, 1, {{"content", 'c', true}, {"from", 'f', true}, {"at", 0, true}, {"before", 0, true}, {"after", 0, true},
                          {"start", 0, false}, {"end", 0, false}, {"after-match", 0, true}, {"before-match", 0, true},
                          {"first", 0, false}, {"match-indent", 'I', false}, {"dedent", 'd', false}, {"strip-tabs", 't', false},
                          {"escapes", 'e', false}, {"dry-run", 'n', false}, {"backup", 'b', false}, {"context", 'C', true},
                          {"quiet", 'q', false}, {"json", 0, false}},
                   p, err))
        return usageErr("insert", err);
    if (p.pos.size() != 1) return usageErr("insert", p.pos.empty() ? "missing FILE" : "expected one FILE (pass text with -c or stdin)");
    std::string file = p.pos[0];
    int npos = 0;
    for (auto k : {"at", "before", "after", "start", "end", "after-match", "before-match"}) npos += p.has(k);
    if (npos != 1) return usageErr("insert", "give exactly one position: --at/--before/--after N, --start, --end, --after-match/--before-match TEXT");
    FileBuf fb;
    if (!loadBuf(file, fb, err)) return fail(err);
    Text t = splitText(fb.text);
    std::vector<std::string>& L = t.lines;
    if (L.size() == 1 && L[0].empty() && !t.trailingNewline) L.clear();
    size_t n = L.size(), idx = 0;
    long anchor = -1;
    auto num = [&](const char* k, long lo, long hi, long& v) {
        if (!parseInt(p.get(k), v) || v < lo || v > hi) {
            err = std::string("--") + k + " must be between " + std::to_string(lo) + " and " + std::to_string(hi);
            return false;
        }
        return true;
    };
    long v;
    if (p.has("at") || p.has("before")) {
        const char* k = p.has("at") ? "at" : "before";
        if (!num(k, 1, (long)n + 1, v)) return usageErr("insert", err);
        idx = (size_t)v - 1;
        anchor = (long)idx < (long)n ? (long)idx : (long)n - 1;
    } else if (p.has("after")) {
        if (!num("after", 0, (long)n, v)) return usageErr("insert", err);
        idx = (size_t)v;
        anchor = v - 1;
    } else if (p.has("start")) { idx = 0; anchor = n ? 0 : -1; }
    else if (p.has("end")) { idx = n; anchor = (long)n - 1; }
    else {
        bool after = p.has("after-match");
        std::string pat = p.get(after ? "after-match" : "before-match");
        std::vector<size_t> hits;
        for (size_t i = 0; i < n; i++) if (L[i].find(pat) != std::string::npos) hits.push_back(i);
        if (hits.empty()) return fail("no line contains " + jsonEscape(pat) + " in " + file, EX_NOTFOUND);
        if (hits.size() > 1 && !p.has("first")) {
            std::string ls;
            for (size_t i = 0; i < hits.size() && i < 12; i++) ls += (i ? ", " : "") + std::to_string(hits[i] + 1);
            return fail(plural(hits.size(), "line") + " match (" + ls + "); make the text unique or add --first", EX_AMBIG);
        }
        anchor = (long)hits[0];
        idx = after ? hits[0] + 1 : hits[0];
    }
    std::string text;
    bool inl;
    if (!getText(p, text, err, inl)) return fail(err);
    if (!text.empty() && text.back() == '\n') text.pop_back();
    if (text.empty() && !inl) return fail("nothing to insert (empty input)");
    if (p.has("match-indent")) {
        std::string ind = anchor >= 0 && (size_t)anchor < n ? leadingWs(L[(size_t)anchor]) : "";
        std::string d = dedent(text + "\n");
        if (!d.empty() && d.back() == '\n') d.pop_back();
        std::vector<std::string> parts = splitSimple(d, '\n');
        text.clear();
        for (size_t i = 0; i < parts.size(); i++) text += (i ? "\n" : "") + (parts[i].empty() ? "" : ind + parts[i]);
    }
    std::vector<std::string> ins = splitSimple(text, '\n');
    L.insert(L.begin() + (long)idx, ins.begin(), ins.end());
    if (idx == n) t.trailingNewline = true;
    if (L.size() && n == 0) t.trailingNewline = true;
    std::string out = joinText(t);
    return finishEdit(p, file, fb, out, "inserted " + plural(ins.size(), "line") + " at line " + std::to_string(idx + 1) + " of " + file,
                      ",\"inserted_at\":" + std::to_string(idx + 1) + ",\"lines\":" + std::to_string(ins.size()));
}

// ------------------------------------------------------------------ delete
static int cmdDelete(const std::vector<std::string>& a) {
    if (wantsHelp(a)) { printf("%s", DELETE_HELP); return 0; }
    Parsed p;
    std::string err;
    if (!parseArgs(a, 1, {{"range", 'r', true}, {"lines", 'L', true}, {"match", 0, true}, {"regex", 'E', false}, {"all", 'a', false},
                          {"dry-run", 'n', false}, {"backup", 'b', false}, {"context", 'C', true}, {"quiet", 'q', false},
                          {"json", 0, false}},
                   p, err))
        return usageErr("delete", err);
    if (p.pos.size() != 1) return usageErr("delete", "expected exactly one FILE");
    std::string file = p.pos[0];
    std::string range = p.has("range") ? p.get("range") : p.get("lines");
    if (range.empty() == !p.has("match")) return usageErr("delete", "give exactly one of --range SPEC or --match TEXT");
    FileBuf fb;
    if (!loadBuf(file, fb, err)) return fail(err);
    Text t = splitText(fb.text);
    if (t.lines.size() == 1 && t.lines[0].empty() && !t.trailingNewline) t.lines.clear();
    std::vector<bool> del(t.lines.size(), false);
    size_t count = 0;
    if (!range.empty()) {
        size_t A, B;
        if (!parseRange(range, t.lines.size(), A, B, err)) return usageErr("delete", err);
        for (size_t i = A; i <= B && i >= 1; i++) { del[i - 1] = true; count++; }
    } else {
        std::string pat = p.get("match");
        std::regex rx;
        bool re = p.has("regex");
        if (re) {
            try { rx = std::regex(pat, std::regex::ECMAScript); } catch (const std::regex_error& e) { return fail(std::string("bad regex: ") + e.what()); }
        }
        std::vector<size_t> hits;
        for (size_t i = 0; i < t.lines.size(); i++)
            if (re ? std::regex_search(t.lines[i], rx) : t.lines[i].find(pat) != std::string::npos) hits.push_back(i);
        if (hits.empty()) return fail("no line matches in " + file, EX_NOTFOUND);
        if (hits.size() > 1 && !p.has("all")) {
            std::string ls;
            for (size_t i = 0; i < hits.size() && i < 12; i++) ls += (i ? ", " : "") + std::to_string(hits[i] + 1);
            return fail(plural(hits.size(), "line") + " match (" + ls + "); use --all or a more specific --match", EX_AMBIG);
        }
        for (auto h : hits) { del[h] = true; count++; }
    }
    std::vector<std::string> keep;
    for (size_t i = 0; i < t.lines.size(); i++) if (!del[i]) keep.push_back(t.lines[i]);
    t.lines = keep;
    if (t.lines.empty()) { t.lines.push_back(""); t.trailingNewline = false; }
    return finishEdit(p, file, fb, joinText(t), "deleted " + plural(count, "line") + " from " + file,
                      ",\"deleted\":" + std::to_string(count));
}

// ------------------------------------------------------------------ spell
static int cmdSpell(const std::vector<std::string>& a) {
    if (wantsHelp(a)) { printf("%s", SPELL_HELP); return 0; }
    Parsed p;
    std::string err;
    if (!parseArgs(a, 1, {{"lang", 'l', true}, {"all-text", 'a', false}, {"suggest", 'k', true}, {"words", 'w', false},
                          {"add", 0, true}, {"json", 0, false}, {"quiet", 'q', false}},
                   p, err))
        return usageErr("spell", err);
    Speller& sp = Speller::instance();
    if (!sp.ensureLoaded()) return fail("spell checker unavailable: " + sp.error());
    if (p.vals.count("add")) {
        for (auto& w : p.vals.at("add")) {
            if (!sp.addUserWord(w, err)) return fail(err);
            if (!p.has("quiet")) fprintf(stderr, "hed: added '%s' to %s\n", w.c_str(), userDictPath().c_str());
        }
        if (p.pos.empty() && isatty(0)) return 0;
    }
    long k = 5;
    if (p.has("suggest") && (!parseInt(p.get("suggest"), k) || k < 0)) return usageErr("spell", "bad --suggest");
    std::vector<std::string> files = p.pos;
    if (files.empty()) {
        if (isatty(0)) return usageErr("spell", "no FILE given and nothing piped on stdin");
        files.push_back("-");
    }
    bool multi = files.size() > 1, json = p.has("json"), quiet = p.has("quiet"), wordsOnly = p.has("words");
    std::unordered_map<std::string, std::vector<std::string>> cache;
    std::set<std::string> uniq;
    std::string jsonOut = "[";
    bool firstJ = true, any = false;
    const char *MAG = g_color ? "\x1b[35m" : "", *GRN = g_color ? "\x1b[32m" : "", *R = g_color ? "\x1b[0m" : "",
               *RED = g_color ? "\x1b[1;31m" : "";
    int rc = 0;
    for (auto& f : files) {
        std::string content;
        if (!readInput(f, content, err)) { fprintf(stderr, "hed: %s\n", err.c_str()); rc = EX_USAGE; continue; }
        LangId lang;
        if (!resolveLang(p, f, content, lang, err)) return usageErr("spell", err);
        std::string fname = f == "-" ? "(stdin)" : f;
        Text t = splitText(content);
        HlState st;
        std::vector<uint8_t> hl;
        for (size_t i = 0; i < t.lines.size(); i++) {
            highlightLine(lang, t.lines[i], st, hl);
            for (auto& m : findMisspellings(sp, lang, t.lines[i], hl, p.has("all-text"))) {
                any = true;
                if (quiet) continue;
                if (wordsOnly) { uniq.insert(m.word); continue; }
                auto it = cache.find(m.word);
                if (it == cache.end()) it = cache.emplace(m.word, k > 0 ? sp.suggest(m.word, (size_t)k) : std::vector<std::string>{}).first;
                if (json) {
                    std::string sj = "[";
                    for (size_t s = 0; s < it->second.size(); s++) sj += (s ? "," : "") + jsonEscape(it->second[s]);
                    jsonOut += std::string(firstJ ? "\n" : ",\n") + "  {\"file\":" + jsonEscape(fname) + ",\"line\":" +
                               std::to_string(i + 1) + ",\"col\":" + std::to_string(m.start + 1) + ",\"word\":" +
                               jsonEscape(m.word) + ",\"suggestions\":" + sj + "]}";
                    firstJ = false;
                    continue;
                }
                std::string sugg;
                for (auto& s : it->second) sugg += (sugg.empty() ? "" : ", ") + s;
                if (multi || f != "-") printf("%s%s%s:", MAG, fname.c_str(), R);
                printf("%s%zu%s:%zu: %s%s%s", GRN, i + 1, R, m.start + 1, RED, m.word.c_str(), R);
                if (k > 0) printf(" -> %s", sugg.empty() ? "(no suggestions)" : sugg.c_str());
                printf("\n");
            }
        }
    }
    if (json && !quiet) printf("%s%s]\n", jsonOut.c_str(), firstJ ? "" : "\n");
    if (wordsOnly && !quiet) for (auto& w : uniq) printf("%s\n", w.c_str());
    if (rc) return rc;
    return any ? EX_NOTFOUND : EX_OK;
}

// ------------------------------------------------------------------ edit
static int cmdEdit(const std::vector<std::string>& a, size_t start) {
    if (wantsHelp(std::vector<std::string>(a.begin() + (long)start, a.end()))) { printf("%s", EDIT_HELP); return 0; }
    Parsed p;
    std::string err;
    if (!parseArgs(a, start, {{"lang", 'l', true}, {"tabsize", 'T', true}, {"tabs", 0, false}, {"spaces", 0, true},
                              {"no-spell", 0, false}, {"no-numbers", 0, false}, {"readonly", 'R', false}, {"line", 0, true},
                              {"log-size", 0, true}},
                   p, err))
        return usageErr("edit", err);
    EditorOptions o;
    for (auto& s : p.pos) {
        if (s.size() > 1 && s[0] == '+') {
            auto parts = splitSimple(s.substr(1), ':');
            o.line = atol(parts[0].c_str());
            if (parts.size() > 1) o.col = atol(parts[1].c_str());
        } else if (o.file.empty()) o.file = s;
        else return usageErr("edit", "only one FILE can be edited at a time");
    }
    if (p.has("line")) o.line = atol(p.get("line").c_str());
    if (p.has("lang")) {
        if (!langFromName(p.get("lang"), o.lang)) return usageErr("edit", "unknown language (see 'hed langs')");
        o.langSet = true;
    }
    // Apply config + environment defaults, then let explicit CLI flags win.
    // Precedence: CLI flag > HED_* env var > config file > built-in default.
    Config cfg = loadConfig();
    if (!p.has("tabsize")) o.tabsize = cfg.tabsize;
    if (!p.has("tabs") && !p.has("spaces")) {
        o.indentMode = cfg.indent;
        if (cfg.indent == 2 && cfg.spaces > 0) o.indentWidth = cfg.spaces;
    }
    if (!p.has("no-spell")) o.spell = cfg.spell;
    if (!p.has("no-numbers")) o.numbers = cfg.numbers;
    o.theme = cfg.theme;
    o.logSize = cfg.logSize;
    if (p.has("tabsize")) o.tabsize = atoi(p.get("tabsize").c_str());
    if (p.has("tabs")) o.indentMode = 1;
    if (p.has("spaces")) { o.indentMode = 2; o.indentWidth = atoi(p.get("spaces").c_str()); if (o.indentWidth < 1) o.indentWidth = 4; }
    if (p.has("log-size")) {
        long v;
        if (!parseInt(p.get("log-size"), v) || v < 0) return usageErr("edit", "bad --log-size (0 = unlimited)");
        o.logSize = (size_t)v;
    }
    o.spell = !p.has("no-spell");
    o.numbers = !p.has("no-numbers");
    o.readOnly = p.has("readonly");
    if (o.file.empty() || o.file == "-") {
        o.file.clear();
        o.pipeMode = !isatty(0) || !isatty(1);
        if (!isatty(0)) readFd(0, o.initial);
    }
    return runEditor(o);
}

// ------------------------------------------------------------------ config
// Apply a config key/value pair to a Config, validating the value. Returns
// false (with a message in err) if the key is unknown or the value is bad.
static bool configSet(Config& c, const std::string& key, const std::string& val, std::string& err) {
    long n;
    if (key == "tabsize") {
        if (!parseInt(val, n) || n < 1 || n > 32) { err = "tabsize must be an integer 1-32"; return false; }
        c.tabsize = (int)n; return true;
    }
    if (key == "indent") {
        std::string v = toLower(val);
        if (v == "auto" || v == "0") { c.indent = 0; return true; }
        if (v == "tabs" || v == "1") { c.indent = 1; return true; }
        if (v == "spaces" || v == "2") { c.indent = 2; return true; }
        err = "indent must be auto, tabs or spaces"; return false;
    }
    if (key == "spaces") {
        if (!parseInt(val, n) || n < 1 || n > 16) { err = "spaces must be an integer 1-16"; return false; }
        c.spaces = (int)n; return true;
    }
    if (key == "spell") {
        std::string v = toLower(val);
        if (v == "true" || v == "yes" || v == "on" || v == "1") { c.spell = true; return true; }
        if (v == "false" || v == "no" || v == "off" || v == "0") { c.spell = false; return true; }
        err = "spell must be true or false"; return false;
    }
    if (key == "numbers") {
        std::string v = toLower(val);
        if (v == "true" || v == "yes" || v == "on" || v == "1") { c.numbers = true; return true; }
        if (v == "false" || v == "no" || v == "off" || v == "0") { c.numbers = false; return true; }
        err = "numbers must be true or false"; return false;
    }
    if (key == "theme") {
        std::string v = toLower(val);
        if (v == "catppuccin" || v == "dark" || v == "light") { c.theme = v; return true; }
        err = "theme must be catppuccin, dark or light"; return false;
    }
    if (key == "log_size") {
        if (!parseInt(val, n) || n < 0) { err = "log_size must be a non-negative integer (0 = unlimited)"; return false; }
        c.logSize = (size_t)n; return true;
    }
    err = "unknown setting '" + key + "' (see 'hed config --help')";
    return false;
}

// Return the current string value of a config key (for `hed config get`).
static std::string configValue(const Config& c, const std::string& key) {
    if (key == "tabsize") return std::to_string(c.tabsize);
    if (key == "indent") return std::string(c.indent == 1 ? "tabs" : c.indent == 2 ? "spaces" : "auto");
    if (key == "spaces") return std::to_string(c.spaces);
    if (key == "spell") return std::string(c.spell ? "true" : "false");
    if (key == "numbers") return std::string(c.numbers ? "true" : "false");
    if (key == "theme") return c.theme;
    if (key == "log_size") return std::to_string(c.logSize);
    return "";
}

static void configPrint(const Config& c) {
    printf("config file: %s\n", configFilePath().c_str());
    printf("tabsize     = %d\n", c.tabsize);
    printf("indent      = %s\n", c.indent == 1 ? "tabs" : c.indent == 2 ? "spaces" : "auto");
    printf("spaces      = %d\n", c.spaces);
    printf("spell       = %s\n", c.spell ? "true" : "false");
    printf("numbers     = %s\n", c.numbers ? "true" : "false");
    printf("theme       = %s\n", c.theme.c_str());
    printf("log_size    = %zu\n", c.logSize);
}

static int cmdConfig(const std::vector<std::string>& a) {
    if (wantsHelp(a)) { printf("%s", CONFIG_HELP); return 0; }
    Parsed p;
    std::string err;
    if (!parseArgs(a, 1, {}, p, err)) return usageErr("config", err);
    Config c = loadConfig();  // defaults + file + HED_* env vars
    if (p.pos.empty()) { configPrint(c); return 0; }
    const std::string& op = p.pos[0];
    if (op == "get") {
        if (p.pos.size() != 2) return usageErr("config", "usage: hed config get KEY");
        std::string v = configValue(c, p.pos[1]);
        if (v.empty()) return fail("unknown setting '" + p.pos[1] + "' (see 'hed config --help')");
        printf("%s\n", v.c_str());
        return 0;
    }
    if (op == "set") {
        if (p.pos.size() != 3) return usageErr("config", "usage: hed config set KEY VALUE");
        if (!configSet(c, p.pos[1], p.pos[2], err)) return fail(err);
        if (!saveConfig(c, err)) return fail("could not write config: " + err);
        printf("hed: %s = %s  (saved to %s)\n", p.pos[1].c_str(), p.pos[2].c_str(), configFilePath().c_str());
        return 0;
    }
    return usageErr("config", "usage: hed config [get KEY | set KEY VALUE]");
}

// ------------------------------------------------------------------ log
static int cmdLog(const std::vector<std::string>& a) {
    if (wantsHelp(a)) { printf("%s", LOG_HELP); return 0; }
    Parsed p;
    std::string err;
    if (!parseArgs(a, 1, {{"lines", 'n', true}}, p, err)) return usageErr("log", err);
    long n = 50;
    if (p.has("lines") && (!parseInt(p.get("lines"), n) || n < 0)) return usageErr("log", "bad --lines");
    if (!p.pos.empty()) {
        if (!parseInt(p.pos[0], n) || n < 0) return usageErr("log", "bad argument (expected a message count)");
    }
    // Today's log file: ~/.config/hed/log/YYYY-MM-DD.log
    char ts[64];
    time_t now = time(nullptr);
    struct tm tmv;
    localtime_r(&now, &tmv);
    strftime(ts, sizeof ts, "%Y-%m-%d", &tmv);
    std::string path = configDir() + "/log/" + ts + ".log";
    std::string content;
    if (!readFile(path, content, err)) { printf("hed: no status log yet (%s)\n", path.c_str()); return 0; }
    std::vector<std::string> lines = linesOf(content);
    size_t start = lines.size() > (size_t)n ? lines.size() - (size_t)n : 0;
    for (size_t i = start; i < lines.size(); i++) printf("%s\n", lines[i].c_str());
    return 0;
}

// ------------------------------------------------------------------ main
int main(int argc, char** argv) {
    if (!setlocale(LC_ALL, "") || !strstr(setlocale(LC_CTYPE, nullptr), "UTF")) {
        if (!setlocale(LC_CTYPE, "C.UTF-8")) setlocale(LC_CTYPE, "en_US.UTF-8");
    }
    std::vector<std::string> a(argv + 1, argv + argc);
    if (a.empty()) return cmdEdit(a, 0);
    const std::string& c = a[0];
    if (c == "-h" || c == "--help" || c == "help") {
        if (a.size() > 1) {
            std::vector<std::string> b = {a[1], "--help"};
            const std::string& s = a[1];
            if (s == "write" || s == "put" || s == "w") return cmdWrite(b);
            if (s == "show" || s == "cat" || s == "view") return cmdShow(b);
            if (s == "search" || s == "grep" || s == "find") return cmdSearch(b);
            if (s == "replace" || s == "sub") return cmdReplace(b);
            if (s == "insert" || s == "ins") return cmdInsert(b);
            if (s == "delete" || s == "del") return cmdDelete(b);
            if (s == "spell") return cmdSpell(b);
            if (s == "config") return cmdConfig(b);
            if (s == "log") return cmdLog(b);
            if (s == "langs" || s == "languages") { printf("%s", LANGS_HELP); return 0; }
            if (s == "edit") { printf("%s", EDIT_HELP); return 0; }
        }
        printf(MAIN_HELP, VERSION);
        return 0;
    }
    if (c == "-V" || c == "--version" || c == "version") {
        printf("hed %s (C++17; dictionary: SymSpell en 82,765 words, MIT)\n", VERSION);
        return 0;
    }
    if (c == "write" || c == "put" || c == "w") return cmdWrite(a);
    if (c == "show" || c == "cat" || c == "view") return cmdShow(a);
    if (c == "search" || c == "grep" || c == "find") return cmdSearch(a);
    if (c == "replace" || c == "sub") return cmdReplace(a);
    if (c == "insert" || c == "ins") return cmdInsert(a);
    if (c == "delete" || c == "del") return cmdDelete(a);
    if (c == "spell") return cmdSpell(a);
    if (c == "langs" || c == "languages") {
        if (wantsHelp(a)) { printf("%s", LANGS_HELP); return 0; }
        printf("%s", langTable().c_str());
        return 0;
    }
    if (c == "config") return cmdConfig(a);
    if (c == "log") return cmdLog(a);
    if (c == "edit" || c == "e") return cmdEdit(a, 1);
    return cmdEdit(a, 0);
}
