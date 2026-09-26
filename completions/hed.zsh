#compdef hed
# zsh completion for hed  (SPDX-License-Identifier: MIT)
# Install to /usr/share/zsh/site-functions/_hed (or add to fpath).

_hed_langs=(python bash fish javascript typescript sql html css json markdown text)

_hed() {
  local -a cmds
  cmds=(write show search replace insert delete spell langs config log edit help)
  if (( CURRENT == 2 )); then
    _describe 'command' cmds
    _files
    return
  fi
  local cmd=${words[2]}
  local -a opts
  case $cmd in
    write)   opts=(--content --from --append --mode --parents --backup --no-clobber --strip-tabs --dedent --escapes --no-eol --diff --show --spell --quiet);;
    show)    opts=(--number --range --lang --spell --color);;
    search)  opts=(--regex --ignore-case --smart-case --word --context --count --files --max --json --fixed --line-number);;
    replace) opts=(--old --new --old-file --new-file --all --nth --expect --regex --ignore-case --range --escapes --dry-run --backup --context --quiet --json);;
    insert)  opts=(--content --from --at --before --after --start --end --after-match --before-match --first --match-indent --dedent --strip-tabs --escapes --dry-run --backup --quiet --json);;
    delete)  opts=(--range --lines --match --regex --all --dry-run --backup --quiet --json);;
    spell)   opts=(--lang --all-text --suggest --words --add --json --quiet);;
    config)  opts=(get set);;
    log)     opts=(--lines);;
    edit)    opts=(--tabsize --tabs --spaces --no-spell --no-numbers --readonly --line --log-size);;
    *)       opts=(--help --color --lang);;
  esac
  # value completions for options that take a fixed set
  case ${words[CURRENT-1]} in
    -l|--lang) _values 'language' $_hed_langs; return;;
    --color)   _values 'color' auto always never; return;;
    --theme)   _values 'theme' catppuccin dark light; return;;
    --indent)  _values 'indent' auto tabs spaces; return;;
  esac
  _arguments -s : $opts
  _files
}

_hed "$@"
