# hed — CLI Reference

Complete reference for every command, option, environment variable, and syntax element of `hed` (v1.0.0).

---

## Table of Contents

- [Global Options](#global-options)
- [Exit Codes](#exit-codes)
- [Command Aliases](#command-aliases)
- [Range Syntax](#range-syntax)
- [Environment Variables](#environment-variables)
- [Common Behaviors](#common-behaviors)
- [Commands](#commands)
  - [write](#write)
  - [show](#show)
  - [search](#search)
  - [replace](#replace)
  - [insert](#insert)
  - [delete](#delete)
  - [spell](#spell)
  - [langs](#langs)
  - [edit](#edit)

---

## Global Options

These options are accepted by every command and must appear before or after the command name but before positional arguments.

| Option | Value | Description |
|--------|-------|-------------|
| `--color` | `auto` \| `always` \| `never` | Control ANSI color output. Default: `auto`. |
| `--no-color` | — | Flag. Equivalent to `--color=never`. Overrides `--color`. |
| `-h`, `--help` | — | Print help text (global or per-command) and exit 0. |
| `--version`, `-V` | — | Print version string and exit 0. |

### Color auto-detection

When `--color=auto` (the default), color is enabled if **all** of the following are true:

1. stdout is a TTY (`isatty(1)`).
2. The `NO_COLOR` environment variable is **not** set.
3. The `TERM` environment variable is **not** `dumb`.

### Option parsing

- Long options accept `--name value` or `--name=value`.
- Short options accept `-n value`, `-nvalue`, or bundled flags like `-ab`.
- `--` stops option parsing; everything after is positional.
- A leading `-` followed by a digit (e.g. `-1`) is treated as a positional argument, not an option.

---

## Exit Codes

| Code | Constant | Meaning |
|------|----------|---------|
| `0` | `EX_OK` | Success. |
| `1` | `EX_NOTFOUND` | Pattern/text not found; misspellings detected; file does not exist; `--no-clobber` triggered. |
| `2` | `EX_USAGE` | Usage error, bad option value, or I/O error (read/write/permission). |
| `3` | `EX_AMBIG` | Ambiguous match (multiple occurrences and not resolved); `--expect` count mismatch; `--after-match`/`--before-match` matched multiple lines without `--first`. |

---

## Command Aliases

Every command can be invoked by any of its aliases. They are interchangeable.

| Canonical | Aliases |
|-----------|---------|
| `write` | `put`, `w` |
| `show` | `cat`, `view` |
| `search` | `grep`, `find` |
| `replace` | `sub` |
| `insert` | `ins` |
| `delete` | `del` |
| `edit` | `e` |
| `langs` | `languages` |

---

## Range Syntax

Used by `show --range`, `replace --range`, and `delete --range` (or `--lines`).

All line numbers are **1-based** and **inclusive**.

| Form | Meaning | Example (file has 100 lines) |
|------|---------|------------------------------|
| `N` | Single line N. | `5` → line 5 only. |
| `A:B` | Lines A through B inclusive. | `10:20` → lines 10–20. |
| `A:` | From line A to the end of the file. | `90:` → lines 90–100. |
| `:B` | From line 1 through line B. | `:15` → lines 1–15. |
| `A:+N` | Starting at line A, N lines total. | `50:+10` → lines 50–59. |
| `-N:` | The last N lines. | `-10:` → lines 91–100. |
| `-$` | Last line only. | `-$` → line 100. |
| `A:-N` | From line A to the line N lines before the end. | `10:-5` → lines 10–96. |

### Additional rules

- `B` may be `$` to mean the last line: `A:$`, `:$`.
- A bare negative number like `-5` is a single-line selector meaning "5 lines from the end" (line 96 in a 100-line file).
- Negative B in `A:B` form: B is relative to end (`total + B + 1`).
- Commas in range specs are treated as colons: `5,10` is equivalent to `5:10`.
- An empty range (B < A) is an error (exit 2).
- A range starting past EOF is an error (exit 2).

---

## Environment Variables

| Variable | Description |
|----------|-------------|
| `HED_DICT` | Path to a replacement spell-check dictionary file. Format: one word per line, optionally followed by whitespace and a frequency number. Overrides the built-in 82,765-word SymSpell English dictionary. |
| `HED_EXTRA_DICTS` | Colon-separated (`:`) list of additional dictionary files. Same format as `HED_DICT`. Loaded in addition to the primary dictionary. Words are assigned frequency 1000. |
| `NO_COLOR` | When set (any value), disables color output under `--color=auto`. Ignored with `--color=always`. |
| `TERM` | When set to `dumb`, disables color output under `--color=auto`. |

### Dictionary file format

```
word
anotherword 50000
thirdword
```

- One word per line.
- Words are lowercased on load.
- Optional trailing number sets the frequency (used for suggestion ranking). Default frequency: 1000.
- The built-in tech vocabulary always has frequency 50,000.
- The personal dictionary (`~/.config/hed/words.txt`) always has frequency 100,000.
- UTF-8 BOM is stripped if present.

### Personal dictionary path

`~/.config/hed/words.txt` (or `$XDG_CONFIG_HOME/hed/words.txt` if `XDG_CONFIG_HOME` is set). Created automatically by `hed spell --add`.

---

## Common Behaviors

### Atomic writes

All file-modifying commands (`write`, `replace`, `insert`, `delete`) write atomically: content is written to a temporary file (`.hed-tmp-XXXXXX` in the same directory), fsynced, then renamed over the target. If the directory is not writable, the command falls back to an in-place write. Existing file permissions are preserved.

### Backups

`-b`/`--backup` copies the existing file to `DIR/YYYYmmddHHMMSS-NAME` before making any change. The backup preserves the original file's mode.

### CRLF handling

Files with CRLF (`\r\n`) line endings are detected and preserved. All matching and editing operations work on the LF-normalized content internally; the on-disk result restores CRLF if the original had it.

### stdin convention

- A bare `-` as a filename means stdin (for reading) or stdout (for writing).
- If no file argument is given and stdin is not a TTY, stdin is read automatically.
- If no file argument is given and stdin **is** a TTY, most commands print a usage error.

### Diff output

`replace`, `insert`, and `delete` print a unified diff (Myers algorithm) of the change to stdout. Context is controlled by `-C`/`--context` (default: 3 lines). Suppressed by `-q`/`--quiet`.

### JSON output

`search`, `replace`, `insert`, `delete`, and `spell` support `--json` for machine-readable output on stdout.

---

## Commands

### write

```
hed write FILE [options]
hed put FILE [options]
hed w FILE [options]
```

Writes text to `FILE` atomically. Replaces the contents (or appends with `-a`). This is the heredoc replacement — works identically in bash, zsh, and fish.

#### Text sources (priority order)

1. `-c`/`--content` — inline text (repeatable for multiple lines).
2. `-f`/`--from FILE` — read from a file (`-` = stdin).
3. stdin — if neither `-c` nor `--from` is given.

A trailing newline is appended to the content if missing (like `<<'EOF'`), unless `--no-eol` is given.

Special file target `-` writes to stdout and skips all filesystem operations.

#### Options

| Option | Short | Value | Description |
|--------|-------|-------|-------------|
| `--content` | `-c` | TEXT | Text to write. Repeatable; each occurrence is one line. |
| `--from` | `-f` | FILE | Read text from FILE (`-` = stdin). |
| `--append` | `-a` | — | Append to FILE instead of overwriting. |
| `--mode` | `-m` | OCTAL | Set file permissions (octal, e.g. `644`, `755`). Must be `0`–`07777`. |
| `--parents` | `-p` | — | Create missing parent directories. |
| `--backup` | `-b` | — | Copy existing FILE to `DIR/YYYYmmddHHMMSS-NAME` before writing. |
| `--no-clobber` | `-n` | — | Fail with exit 1 if FILE already exists. |
| `--strip-tabs` | `-t` | — | Strip leading tabs from each line (like `<<-EOF`). |
| `--dedent` | `-d` | — | Strip common indentation and a leading/trailing blank line. |
| `--escapes` | `-e` | — | Interpret `\n` `\t` `\r` `\0` `\a` `\b` `\e` `\f` `\v` `\\` `\'` `\"` `\xHH` `\uHHHH` `\UHHHHHHHH` in the text. |
| `--no-eol` | — | — | Do not append a trailing newline. |
| `--diff` | `-D` | — | Print a unified diff of the previous content vs. the new content to stdout. |
| `--show` | `-s` | — | Print the written file with syntax highlighting and line numbers to stdout. |
| `--spell` | — | — | Report misspellings in the written text to stderr. Does not affect exit code. |
| `--lang` | `-l` | LANG | Language for `--show` and `--spell`. Default: auto-detect from filename. |
| `--quiet` | `-q` | — | Suppress the summary line on stderr. |

#### Examples

```bash
# Heredoc replacement (bash/zsh)
hed write config.yaml <<'EOF'
key: value
EOF

# From stdin
printf '%s\n' 'a' 'b' | hed write list.txt

# With dedent + mode + parents (works in fish too)
hed write -p -m 755 bin/run.sh -d -c '
    #!/usr/bin/env bash
    echo "hi from $USER"
'

# Append
echo "extra line" >> file.txt   # classic
echo "extra line" | hed write -a file.txt   # hed equivalent

# Write to stdout
hed write - -c 'hello'

# No-clobber (fail if exists)
hed write -n important.conf -c 'key: val'

# With diff and show
hed write app.py -c 'print("hello")' -D -s

# Spell check the written content
hed write README.md --spell -c 'This is a misspeld word.'
```

#### Exit codes

| Code | Condition |
|------|-----------|
| 0 | Success. |
| 1 | `--no-clobber` and FILE already exists. |
| 2 | Missing FILE argument, bad `--mode`, directory does not exist (without `-p`), I/O error, or FILE is a directory. |

---

### show

```
hed show [FILE...] [options]
hed cat [FILE...] [options]
hed view [FILE...] [options]
```

Prints files (or stdin) with syntax highlighting when stdout is a terminal. Multiple files are separated by `==> FILE <==` headers.

#### Options

| Option | Short | Value | Description |
|--------|-------|-------|-------------|
| `--number` | `-n` | — | Prefix each line with its line number. Tab-separated when output is not colored. |
| `--range` | `-r` | SPEC | Print only lines matching SPEC. See [Range Syntax](#range-syntax). |
| `--lang` | `-l` | LANG | Force language for syntax highlighting. See `hed langs`. |
| `--spell` | `-s` | — | Underline misspelled words (requires color output). |

#### Examples

```bash
# Show a file with highlighting
hed show app.py

# Numbered, language-specific
hed show app.py -n -l python

# Show a range (lines 40–59)
hed show app.py -r 40:+20

# Show last 10 lines
hed show app.py -r -10:

# Multiple files with headers
hed show src/*.py

# Pipe through
cat app.py | hed show -n

# Spell check view
hed show README.md -s
```

#### Exit codes

| Code | Condition |
|------|-----------|
| 0 | Success (files printed, or stdin read). |
| 2 | No FILE given and stdin is a TTY; unreadable file; bad range or language. |

---

### search

```
hed search PATTERN [FILE...] [options]
hed grep PATTERN [FILE...] [options]
hed find PATTERN [FILE...] [options]
```

Searches for PATTERN in files or stdin. Literal search by default. Output format: `[FILE:]LINE:COL:TEXT` with the matching portion highlighted in color. Multiple files prefix each line with the filename.

#### Options

| Option | Short | Value | Description |
|--------|-------|-------|-------------|
| `--regex` | `-E` | — | Treat PATTERN as an ECMAScript regular expression. |
| `--ignore-case` | `-i` | — | Case-insensitive match. |
| `--smart-case` | `-S` | — | Case-insensitive unless PATTERN contains uppercase. |
| `--word` | `-w` | — | Match whole words only (word boundaries). |
| `--context` | `-C` | N | Show N lines of context around each match. |
| `--count` | `-c` | — | Print only the number of matches per file. |
| `--files` | `-l` | — | Print only the names of files that contain matches. |
| `--max` | `-m` | N | Stop after N matches per file. |
| `--json` | — | — | Output a JSON array of match objects. |
| `--fixed` | `-F` | — | Treat PATTERN as a fixed string (disables `-E`). |
| `--line-number` | `-n` | — | (Accepted for grep compatibility; output always includes line numbers.) |

#### Output format

Default (single file):
```
12:8:    def main():
```

Default (multiple files):
```
src/app.py:12:8:    def main():
```

With `--count`:
```
42
```

With `--files`:
```
src/app.py
src/utils.py
```

With `--json`:
```json
[
  {"file":"src/app.py","line":12,"col":8,"length":4,"text":"    def main():"}
]
```

#### Examples

```bash
# Literal search
hed search 'def main' src/*.py

# Regex
hed search -E 'def\s+\w+\(' src/*.py

# Case-insensitive
hed search -i 'error' log.txt

# Whole word
hed search -w 'True' app.py

# With context
hed search -C 3 'TODO' src/*.py

# Count matches per file
hed search -c 'import' src/*.py

# Files with matches only
hed search -l 'deprecated' src/*.py

# Limit results
hed search -m 10 'the' large_file.txt

# JSON output
hed search --json 'error' log.txt | jq .

# Pipe
cat app.py | hed search 'def'
```

#### Exit codes

| Code | Condition |
|------|-----------|
| 0 | At least one match found. |
| 1 | No matches found. |
| 2 | Missing PATTERN, bad regex, bad `--context`/`--max` value, or unreadable file. |

---

### replace

```
hed replace FILE OLD NEW [options]
hed sub FILE OLD NEW [options]
```

Replaces text in FILE. By default, OLD must match **exactly once** — the command is strict to prevent accidental mass edits. If OLD matches multiple times, the command exits with code 3 and suggests adding context or using `--all`/`--nth`. OLD and NEW may span multiple lines.

Prints a unified diff of the change to stdout (unless `-q`).

#### Specifying OLD and NEW

OLD and NEW can be given as positional arguments or via options:

- Positional: `hed replace FILE 'old text' 'new text'`
- Inline options: `--old 'old text' --new 'new text'`
- From files: `--old-file old.txt --new-file new.txt` (read literally; `-` = stdin)

At least one of positional OLD or `--old`/`--old-file` must be provided. Same for NEW. NEW may be empty (`""`) to delete the matched text.

#### Options

| Option | Short | Value | Description |
|--------|-------|-------|-------------|
| `--old` | — | TEXT | Replacement for positional OLD. |
| `--new` | — | TEXT | Replacement for positional NEW. |
| `--old-file` | — | FILE | Read OLD from FILE (`-` = stdin). Literal, no escape processing. |
| `--new-file` | — | FILE | Read NEW from FILE (`-` = stdin). Literal, no escape processing. |
| `--all` | `-a` | — | Replace every occurrence of OLD. |
| `--nth` | — | N | Replace only the Nth occurrence (1-based). |
| `--expect` | — | N | Fail (exit 3) unless exactly N occurrences would be replaced. |
| `--regex` | `-E` | — | Treat OLD as an ECMAScript regex. NEW may contain `$1`, `$2`, `$&` capture group references. Multiline: `^` and `$` match per line. |
| `--ignore-case` | `-i` | — | Case-insensitive match. |
| `--range` | `-r` | SPEC | Only match within the specified line range. See [Range Syntax](#range-syntax). |
| `--escapes` | `-e` | — | Interpret `\n`, `\t`, etc. in inline OLD/NEW (not applied to `--old-file`/`--new-file`). |
| `--dry-run` | `-n` | — | Show the diff but do not write. |
| `--backup` | `-b` | — | Create a timestamped backup before writing. |
| `--context` | `-C` | N | Diff context lines (default: 3). |
| `--quiet` | `-q` | — | Suppress diff and summary output. |
| `--json` | — | — | Output a JSON result object. |

#### Ambiguity resolution

When OLD matches multiple times and neither `--all`, `--nth`, nor `--expect` is given:

- The command exits with code **3**.
- An error message lists the line numbers of all matches (up to 12).
- Suggestions: add surrounding context to OLD, or use `--all` / `--nth N`.

#### Hints

When no match is found, the error message may include:

- A hint if a whitespace-insensitive match exists at a specific line, with a suggested `hed show` command to copy the exact text.
- A hint if a case-insensitive match exists (suggesting `-i`).

#### JSON output

```json
{
  "file": "app.py",
  "changed": true,
  "written": true,
  "replacements": 1,
  "lines": ["42"],
  "backup": "/path/to/20240101120000-app.py",
  "diff": "--- a/app.py\n+++ b/app.py\n..."
}
```

On failure (ambiguous or not found), `changed` is `false` and an `error` field is included.

#### Examples

```bash
# Unique replacement (default strict mode)
hed replace app.py 'retries = 3' 'retries = 5'

# Replace all occurrences
hed replace app.py 'print(' 'log(' --all

# Replace with expected count
hed replace app.py 'TODO' 'DONE' --all --expect 7

# Replace Nth occurrence
hed replace app.py 'x = 1' 'x = 2' --nth 2

# Regex replace (capture groups)
hed replace app.py -E '(\w+)\s*=\s*(\d+)' '$1 = $2  # was $2'

# Using files for OLD/NEW
hed replace config.yaml --old-file section_old.txt --new-file section_new.txt

# Limit to a range
hed replace app.py 'foo' 'bar' -r 10:20

# Dry run
hed replace app.py 'old_func' 'new_func' -n

# With backup
hed replace app.py 'v1' 'v2' -b

# Case-insensitive
hed replace app.py -i 'hello' 'Goodbye'

# Delete text (empty NEW)
hed replace app.py 'deprecated_code()' ''
```

#### Exit codes

| Code | Condition |
|------|-----------|
| 0 | Replacement applied (or would be, with `--dry-run`). |
| 1 | OLD not found; `--nth` exceeds match count; file does not exist. |
| 2 | Missing arguments, bad `--nth`/`--expect` value, bad regex, bad range, I/O error. |
| 3 | Ambiguous match (multiple occurrences, none selected); `--expect` count mismatch. |

---

### insert

```
hed insert FILE [options]
hed ins FILE [options]
```

Inserts text (from `-c`, `--from`, or stdin) as whole lines into FILE at a specified position. Exactly one position option is required.

#### Position options (exactly one required)

| Option | Value | Description |
|--------|-------|-------------|
| `--at` | N | New text becomes line N (shifts existing line N down). N = `last+1` appends. Range: `1` to `lines+1`. |
| `--before` | N | Same as `--at`. |
| `--after` | N | Insert after line N. N = 0 means top of file. Range: `0` to `lines`. |
| `--start` | — | Insert at the top of the file (line 1). |
| `--end` | — | Append at the end of the file. |
| `--after-match` | TEXT | Insert after the line containing TEXT. Must be unique unless `--first`. |
| `--before-match` | TEXT | Insert before the line containing TEXT. Must be unique unless `--first`. |

#### Options

| Option | Short | Value | Description |
|--------|-------|-------|-------------|
| `--content` | `-c` | TEXT | Text to insert. Repeatable; each occurrence is one line. |
| `--from` | `-f` | FILE | Read text to insert from FILE (`-` = stdin). |
| `--at` | — | N | Insert at line N (becomes the new line N). |
| `--before` | — | N | Same as `--at`. |
| `--after` | — | N | Insert after line N (0 = top). |
| `--start` | — | — | Insert at top of file. |
| `--end` | — | — | Insert at end of file. |
| `--after-match` | — | TEXT | Insert after the line containing TEXT. |
| `--before-match` | — | TEXT | Insert before the line containing TEXT. |
| `--first` | — | — | Use the first matching line if several match (for `--after-match`/`--before-match`). |
| `--match-indent` | `-I` | — | Re-indent the inserted block to match the anchor line's indentation. |
| `--dedent` | `-d` | — | Strip common indentation + leading/trailing blank line from inserted text. |
| `--strip-tabs` | `-t` | — | Strip leading tabs from each inserted line. |
| `--escapes` | `-e` | — | Interpret `\n`, `\t`, etc. in inline text. |
| `--dry-run` | `-n` | — | Show the diff but do not write. |
| `--backup` | `-b` | — | Create a timestamped backup before writing. |
| `--context` | `-C` | N | Diff context lines (default: 3). |
| `--quiet` | `-q` | — | Suppress diff and summary output. |
| `--json` | — | — | Output a JSON result object. |

#### Examples

```bash
# Insert at top
hed insert app.py --start -c '"""Module docstring."""'

# Insert at a specific line
hed insert app.py --at 10 -c 'import sys'

# Insert after a line
hed insert app.py --after 5 -c '# TODO: fix this'

# Insert at end
hed insert app.py --end -c 'print("done")'

# Insert after a match
hed insert app.py --after-match 'import os' -c 'import sys'

# Insert before a match (with --first for duplicates)
hed insert app.py --before-match 'def main():' --first -c '# Entry point'

# Insert with dedent (indented in script, dedented in file)
hed insert app.py --at 5 -d -c '
    def helper():
        return True
'

# Match indentation of anchor line
hed insert app.py --after-match 'def process():' -I -c '
    step1()
    step2()
'

# From stdin
echo "extra line" | hed insert app.py --end

# Dry run
hed insert app.py --at 3 -c 'new line' -n

# Multiple lines via repeated -c
hed insert app.py --at 2 -c 'line one' -c 'line two' -c 'line three'
```

#### Exit codes

| Code | Condition |
|------|-----------|
| 0 | Insert applied (or would be, with `--dry-run`). |
| 1 | `--after-match`/`--before-match` text not found; empty input. |
| 2 | Missing FILE, no position or multiple positions, bad line number, file does not exist, I/O error. |
| 3 | `--after-match`/`--before-match` matched multiple lines without `--first`. |

---

### delete

```
hed delete FILE [options]
hed del FILE [options]
```

Deletes lines from FILE. Requires exactly one of `--range`/`--lines` or `--match`.

#### Options

| Option | Short | Value | Description |
|--------|-------|-------|-------------|
| `--range` | `-r` | SPEC | Delete lines matching SPEC. See [Range Syntax](#range-syntax). |
| `--lines` | `-L` | SPEC | Same as `--range`. |
| `--match` | — | TEXT | Delete the line containing TEXT. Must be unique unless `--all`. |
| `--regex` | `-E` | — | Treat `--match` as an ECMAScript regex (delete all lines matching). |
| `--all` | `-a` | — | Delete every line matching `--match` (or every match of the regex). |
| `--dry-run` | `-n` | — | Show the diff but do not write. |
| `--backup` | `-b` | — | Create a timestamped backup before writing. |
| `--context` | `-C` | N | Diff context lines (default: 3). |
| `--quiet` | `-q` | — | Suppress diff and summary output. |
| `--json` | — | — | Output a JSON result object. |

#### Examples

```bash
# Delete a range of lines
hed delete app.py --range 88:90

# Delete last 5 lines
hed delete app.py -r -5:

# Delete lines 10-20 with context
hed delete app.py -r 10:20

# Delete a specific line by content
hed delete app.py --match '# TODO: remove this'

# Delete all lines matching a pattern
hed delete app.py --match '^import ' -a

# Delete with regex
hed delete app.py --match '^\s*$' -E -a   # delete all blank lines

# Using --lines (equivalent to --range)
hed delete app.py -L 5:10

# Dry run
hed delete app.py -r 1:3 -n

# With backup
hed delete app.py --match 'deprecated' -b
```

#### Exit codes

| Code | Condition |
|------|-----------|
| 0 | Lines deleted (or would be, with `--dry-run`). |
| 1 | No line matches `--match` or regex; file does not exist. |
|2 | Missing FILE, both/neither `--range` and `--match` given, bad range, bad regex, I/O error. |
| 3 | `--match` matched multiple lines without `--all`. |

---

### spell

```
hed spell [FILE...]
```

Spell-checks files or stdin. Uses an 82,765-word frequency-ranked English dictionary (SymSpell, MIT) embedded in the binary, plus a built-in tech vocabulary, affix analysis (suffixes/prefixes), and contraction handling.

In source code files, only **comments and string literals** are checked. Identifiers, paths, URLs, camelCase, and ACRONYMS are skipped. In Markdown, text, and HTML files, all text content is checked.

Output format: `[FILE:]LINE:COL: word -> suggestion1, suggestion2, ...`

#### Options

| Option | Short | Value | Description |
|--------|-------|-------|-------------|
| `--lang` | `-l` | LANG | Force language. Affects which regions are checked. |
| `--all-text` | `-a` | — | Check every word, ignoring code structure (comments/strings only). |
| `--suggest` | `-k` | N | Number of suggestions per word (default: 5). `0` = no suggestions. |
| `--words` | `-w` | — | Print only the unique misspelled words (one per line), no positions or suggestions. |
| `--add` | — | WORD | Add WORD to `~/.config/hed/words.txt`. Repeatable. No FILE needed. |
| `--json` | — | — | Output a JSON array of misspelling objects. |
| `--quiet` | `-q` | — | No output; exit status only. |

#### What is checked

| File type | Regions checked |
|-----------|-----------------|
| Source code (Python, JS, Bash, etc.) | Comments and string literals only. |
| Markdown / text / HTML | All text content. |
| JSON | String values. |

Skipped in all modes: identifiers, file paths, URLs, email addresses, numbers, camelCase, PascalCase, ALL-CAPS acronyms, words shorter than 3 letters.

#### JSON output

```json
[
  {"file":"README.md","line":3,"col":12,"word":"misspeld","suggestions":["misspelled","misplayed"]}
]
```

#### Examples

```bash
# Check a file
hed spell README.md

# Check multiple files
hed spell src/*.py docs/*.md

# Pipe input
git log -1 --format=%B | hed spell

# Check all text (ignore code structure)
hed spell app.py -a

# Get 3 suggestions per word
hed spell README.md -k 3

# No suggestions (just flag)
hed spell README.md -k 0

# Unique misspelled words only
hed spell README.md -w

# Add a word to the personal dictionary
hed spell --add kubernetes
hed spell --add myproject --add myword

# JSON output
hed spell README.md --json | jq .

# Quiet (CI check)
hed spell README.md -q && echo "clean" || echo "typos found"
```

#### Exit codes

| Code | Condition |
|------|-----------|
| 0 | No misspellings found. |
| 1 | Misspellings found. |
| 2 | Dictionary unavailable, missing file, bad `--suggest` value, or I/O error. |

---

### langs

```
hed langs
hed languages
```

Lists all supported language identifiers for syntax highlighting and the `--lang` option. Prints a table with language names, aliases, and file extensions. `hed langs --help` prints a compact one-line list of the language names.

#### Supported languages

| Language | Aliases | File extensions |
|----------|---------|-----------------|
| text | txt, plain, none | .txt, .text, .log |
| python | py, python3 | .py, .pyw, .pyi |
| bash | sh, shell, zsh, ksh | .sh, .bash, .zsh, .ksh, .bats, .command |
| fish | fishshell | .fish |
| javascript | js, node, jsx | .js, .mjs, .cjs, .jsx |
| typescript | ts, tsx | .ts, .tsx, .mts, .cts |
| sql | — | .sql |
| html | — | .html, .htm |
| css | — | .css, .scss |
| json | — | .json |
| markdown | md | .md, .markdown |

#### Exit codes

| Code | Condition |
|------|-----------|
| 0 | Always. |

---

### edit

```
hed [FILE] [+LINE[:COL]] [options]
hed edit [FILE] [+LINE[:COL]] [options]
hed e [FILE] [+LINE[:COL]] [options]
```

Opens a full-screen nano-style editor on FILE (or stdin). Provides syntax highlighting, live search, replace, undo/redo, mark/cut/paste, comment toggling, spell checking, **mouse support**, and **crash recovery**. Editor settings (tab size, indent, spell, line numbers, syntax theme, log size) are read from `~/.config/hed/config` (see [config](#config)); three syntax themes are built in (`catppuccin`, `dark`, `light`) and custom themes can be added (see [theme](#theme)).

When no command name is given, `hed FILE` opens the editor. This is the default action when the first argument is not a recognized command.

#### Invoking

| Form | Behavior |
|------|----------|
| `hed` | Open editor on empty buffer. |
| `hed FILE` | Open FILE in editor. |
| `hed edit FILE` | Same as above (use when FILE looks like a command name). |
| `hed +10 FILE` | Open FILE at line 10. |
| `hed +10:5 FILE` | Open FILE at line 10, column 5. |
| `cmd \| hed` | Edit piped stdin; write result to stdout on `^X`. |
| `cmd \| hed \| cmd2` | Pipe-through editor (like `vipe`). |
| `hed > out.txt` | Edit stdin, save to stdout redirect. |

#### Options

| Option | Short | Value | Description |
|--------|-------|-------|-------------|
| `--lang` | `-l` | LANG | Force language for syntax highlighting. |
| `--tabsize` | `-T` | N | Tab display width (default: 4). |
| `--tabs` | — | — | Indent with tabs. |
| `--spaces` | — | N | Indent with N spaces (minimum 1, default 4). |
| `--no-spell` | — | — | Disable spell-check underlining at startup. |
| `--no-numbers` | — | — | Hide line numbers. |
| `--readonly` | `-R` | — | Open in view-only mode (no saving). |
| `--line` | — | N | Open at line N (negative = from end). |

#### Position syntax

| Syntax | Description |
|--------|-------------|
| `+N` | Open at line N. Negative = from end (`-1` = last line). |
| `+N:C` | Open at line N, column C (1-based). |

#### Editor keys

| Key | Action |
|-----|--------|
| `^S` | Save |
| `^O` | Save as (prompt for filename) |
| `^X` | Exit (prompt to save if modified) |
| `^W` | Find (search) |
| `^R` | Replace |
| `^K` | Cut line |
| `^U` | Paste |
| `^Z` | Undo |
| `^Y` | Redo |
| `^_` | Go to line (prompt) |
| `^T` | Spell check |
| `^^` | Mark / set mark |
| `M-3` | Toggle comment |
| `^Q` | Quit pipe mode (abort, exit 1) |
| `^G` | Help (inside editor) |
| Mouse | Click moves the cursor; scroll wheel scrolls |

#### Editor feedback

- **Search match counter:** live search shows `match X of Y` in the status bar, updated as you type and kept after confirming.
- **Replace improvements:** each replace prompt shows `(X of Y)` (which occurrence) plus a context snippet — `ln N: "…pre [MATCH] post…"` — so you can confirm you are replacing the right text.
- **Undo/redo feedback:** the status bar shows remaining undo/redo depth (`U n R n`) whenever no prompt is active.
- **Comment toggle feedback:** toggling comments reports the affected range, e.g. `Commented lines 3-5` / `Uncommented lines 3-5`.
- **Permanent position:** the status bar always shows `Ln X, Col Y` on the right, even when a message is displayed.
- **Read-only red bar:** in read-only mode the entire title row is drawn as a solid red bar (white on red).
- **Title bar ellipsis:** long filenames are truncated with a leading `…` marker to fit the terminal width.
- **Help bar ellipsis:** on narrow terminals, help-bar labels are truncated with an ellipsis (a label is dropped entirely if it cannot fit).
- **Spell suggestions:** the spell walk offers up to **6** suggestions per misspelling (keys `1`–`6`).

#### Crash recovery (swap files)

While editing a named file, hed keeps a swap file at `~/.config/hed/swap/FILENAME.swp`, flushed every ~30 seconds while the buffer is dirty. If hed is killed (crash, power loss, terminal close), at most 30 seconds of edits are lost. On the next start, if the swap file is newer than the original file, hed shows **"Swap file found, press R to recover"** — press `R` to restore, any other key to discard. The swap file is removed on a clean exit.

#### Missing-terminal error

If the editor is launched without a usable terminal (e.g. from a script or agent with no TTY), hed prints a clear message and exits 2, pointing you to the one-shot commands instead:

```
hed: interactive editor needs a terminal (/dev/tty unavailable).
Use a one-shot command instead, e.g.:  hed write FILE ...  |  hed show FILE  |  hed replace FILE OLD NEW
```

#### Pipe mode

When stdin is not a TTY, the editor reads the piped content into the buffer. On `^X`, the final buffer is written to stdout. On `^Q`, it aborts with exit code 1.

#### Examples

```bash
# Open a file
hed app.py

# Open at a specific line
hed app.py +42
hed +42 app.py

# Open with a specific language
hed script -l bash
hed +10 script --lang python

# Read-only view
hed config.yaml -R

# Pipe-through
cat app.py | hed | tee app.py
git diff | hed | grep '^[+-]'

# Save as on exit
# (press ^O inside the editor, enter filename)
```

#### Exit codes

| Code | Condition |
|------|-----------|
| 0 | Exited normally (saved or intentionally unsaved). |
| 1 | Pipe mode aborted (`^Q`), or I/O error. |

---

### config

```
hed config
hed config get KEY
hed config set KEY VALUE
```

Views or edits the editor configuration file `~/.config/hed/config` (or `$XDG_CONFIG_HOME/hed/config`), an INI-style file with an `[editor]` section. With no arguments it prints the file path and the effective settings; `get KEY` prints one setting; `set KEY VALUE` validates, saves, and confirms.

| Setting | Values | Meaning |
|---------|--------|---------|
| `tabsize` | 1–32 | Tab display width |
| `indent` | `auto` \| `tabs` \| `spaces` | Indent style |
| `spaces` | 1–16 | Indent width when `indent = spaces` |
| `spell` | `true` \| `false` | Start with misspelling underlining on |
| `numbers` | `true` \| `false` | Show the line-number gutter |
| `theme` | `catppuccin` \| `dark` \| `light` \| custom | Syntax colour palette (custom = name of a file in `~/.config/hed/themes/`; see [theme](#theme)) |
| `log_size` | 0+ | Status messages kept in memory (0 = unlimited) |

Precedence: **command-line flags > `HED_*` environment variables > config file > defaults**. The environment variables are `HED_TABSIZE`, `HED_INDENT`, `HED_SPACES`, `HED_SPELL`, `HED_NUMBERS`, `HED_THEME`, and `HED_LOG_SIZE`.

### theme

```
hed theme
hed theme create NAME
```

Lists the available syntax themes, or creates a template for a new one. Three themes are built in — `catppuccin` (default), `dark`, and `light` — and users can add their own without recompiling.

| Form | Behavior |
|------|----------|
| `hed theme` | List built-in and custom themes (marks the current one). |
| `hed theme create NAME` | Write a template to `~/.config/hed/themes/NAME.theme`, seeded with the current theme's colours. |

Custom themes are INI-style files in `~/.config/hed/themes/` (or `$XDG_CONFIG_HOME/hed/themes`), e.g. `~/.config/hed/themes/mytheme.theme`:

```ini
[theme]
comment = 3;38;5;244    # italic + grey
string  = 38;5;114      # 256-colour blue
number  = 38;5;215
```

Each key maps a highlight type to the numeric part of an SGR escape sequence (the editor wraps values in `ESC[<value>m`). The recognised keys are `comment`, `string`, `number`, `keyword`, `type`, `builtin`, `function`, `variable`, `constant`, `tag`, `attr`, `preproc`, `escape`, `property`, `operator`. Missing keys fall back to catppuccin. Select a theme with `hed config set theme NAME` or `HED_THEME=NAME`. If the selected theme file is missing or malformed, hed warns on stderr and falls back to catppuccin. A custom theme with the same name as a built-in overrides it.

### log

```
hed log [N]
hed log -n N
```

Shows the last N status messages the editor logged to `~/.config/hed/log/YYYY-MM-DD.log` (one file per day). Default N = 50.

---

## Shell completions

`make install` installs tab-completion for **bash**, **fish**, and **zsh** (installed to the system completion directories). Completions cover all commands (`write`, `show`, `search`, `replace`, `insert`, `delete`, `spell`, `langs`, `config`, `theme`, `log`, `edit`, `help`), their options, the language identifiers, and the theme values (`catppuccin`, `dark`, `light`). The zsh completion also completes `config get`/`set` keys and `--lang`/`--theme` values.

---

## Quick Reference

```
# Write (heredoc replacement)
  hed write FILE [options]
  Text: -c TEXT | -f FILE | stdin

# Show (with highlighting)
  hed show [FILE...] [-n] [-r RANGE] [-l LANG] [-s]

# Search (grep-style)
  hed search PATTERN [FILE...] [-E] [-i] [-S] [-w] [-C N] [-c] [-l] [-m N] [--json]

# Replace (strict by default)
  hed replace FILE OLD NEW [--all | --nth N | --expect N] [-E] [-i] [-r RANGE] [-n] [-b]

# Insert (at a position)
  hed insert FILE (--at N | --after N | --start | --end | --match ...) [options]

# Delete (range or match)
  hed delete FILE (-r RANGE | --match TEXT) [-a] [-n] [-b]

# Spell check
  hed spell [FILE...] [-l LANG] [-a] [-k N] [-w] [--add WORD] [--json] [-q]

# List languages
  hed langs [--help]

# Editor config & status log
  hed config [get KEY | set KEY VALUE]
  hed theme [create NAME]
  hed log [N]

# Interactive editor
  hed [FILE] [+LINE[:COL]] [-l LANG] [-T N] [--tabs | --spaces N] [-R] [--log-size N]
```

## Summary of exit codes

| Code | Meaning |
|------|---------|
| 0 | OK / success |
| 1 | Not found (no match, misspellings, missing file) |
| 2 | Usage or I/O error |
| 3 | Ambiguous match or `--expect` mismatch |
