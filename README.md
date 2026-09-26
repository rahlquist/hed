# hed

A single static C++17 binary that is three things at once:

1. **A heredoc replacement** — `hed write FILE` takes stdin or `-c 'text'`, writes atomically, and works the same in bash, zsh and **fish** (which has no heredocs).
2. **An agent-friendly one-shot file tool** — `show`, `search`, `replace`, `insert`, `delete`, `spell`. Never prompts, prints a unified diff of every change, refuses ambiguous edits, and uses meaningful exit codes.
3. **A nano-style editor** — `hed FILE` opens a full-screen editor with syntax highlighting, live search, replace, undo/redo, mark/cut/paste, comment toggling, and an interactive spell checker. `cmd | hed | cmd2` works like `vipe`.

Syntax highlighting: Python, Bash/sh/zsh (including heredoc bodies), fish, JavaScript, TypeScript, SQL, HTML (with embedded `<script>`/`<style>`), CSS/SCSS, JSON, Markdown.

Spell checking: 82,765-word frequency dictionary embedded in the binary, suggestion ranking by edit distance + word frequency, suffix/prefix awareness (running, unconfigured), contractions, plus a built-in tech vocabulary and a personal dictionary. In code, only comments and string literals are checked; identifiers, paths, URLs, camelCase and ACRONYMS are skipped.

No runtime dependencies: no ncurses, no hunspell, no data files.

## Build & install

```bash
make                 # needs g++ >= 9 or clang++ >= 10 (C++17)
make test            # 32 checks, includes fish if installed
sudo make install    # /usr/local/bin/hed + bash & fish completions
make static          # fully static, stripped binary you can scp anywhere
```

## One-shot examples

```bash
# heredoc replacement (bash)
hed write config.yaml <<'EOF'
key: value
EOF

# same thing in fish or bash: indented single-quoted block, dedented automatically
hed write -p -m 755 ~/bin/hello -d -c '
    #!/usr/bin/env bash
    echo "hello $USER"
'

hed show app.py -n -r 40:+20          # numbered slice
hed search 'def main' src/*.py -C 2   # grep-style
hed replace app.py 'retries = 3' 'retries = 5'      # must be unique, prints diff
hed replace app.py 'print(' 'log(' --all --expect 7 -b
hed insert app.py --after-match 'import os' -c 'import sys'
hed delete app.py --range 88:90
hed spell README.md                   # exit 1 if typos found
git log -1 --format=%B | hed spell    # pipes work everywhere
```

Exit codes: `0` ok · `1` not found / misspellings · `2` usage or I/O error · `3` ambiguous or `--expect` mismatch (nothing written).

Backups (`-b`) are named `DIR/YYYYmmddHHMMSS-FILENAME`.

## Editor keys

| Key | Action | Key | Action |
|---|---|---|---|
| ^S | save | ^O | save as |
| ^X | exit (asks to save) | ^Q | quit / abort |
| ^W / ^F | live search (smart-case) | ^N / ^P | next / previous match |
| ^R or ^\\ | replace (y/n/all) | ^_ / ^L | go to line[:col] |
| ^K | cut line (repeat to collect) or selection | ^U | paste |
| ^^ or M-A | set mark (select) | M-6 | copy |
| ^Z / ^Y | undo / redo | M-3 | toggle comment |
| M-Up/Down | move line | M-D | duplicate line |
| ^T or F7 | spell walk (1-9 pick, a add, i ignore, e edit) | M-S | toggle spell underline |
| Tab / Shift-Tab | indent / outdent (selection too) | M-N | toggle line numbers |
| ^G / F1 | help | ^C | position info |

Also: auto-indent (with language-aware extra indent after `:`, `{`, `then`, `function` …), smart Home, bracketed paste, UTF-8/wide characters, CRLF preservation, indentation style detection, `+LINE:COL` on the command line.

## Licensing

hed itself is MIT (see `LICENSE`). The only bundled third-party material is the SymSpell English frequency dictionary (MIT, © Wolf Garbe) — see `NOTICE`.
