Here’s a single‐bash script for macOS that roughly reproduces the behavior of your C program.  It:

  • Reads a config file ~/.config/cleanup.conf with lines of the form  
      key:/some/absolute/path  
    (ignoring blank lines and those beginning with ‘#’).  
  • Iterates over all filenames passed on the command line.  
  • For each file, interactively prompts:  
      q) Quit  
      d) Delete  
      v) Quick‐View (using qlmanage -p)  
      i) Ignore  
      m) Move (choose one of the configured dirs + optional subdir)  
      p) PrevDest (move to last used directory)  
      c) Custom path (with readline‐style editing & tab‐completion)  
  • Keeps track of the last destination directory for “p) PrevDest.”  

Save this as e.g. ~/bin/cleanup.sh, make it executable (chmod +x), and run it with one or more files:

    ~/bin/cleanup.sh foo.txt bar.jpg

```bash
#!/usr/bin/env bash
#
# cleanup.sh — interactive file cleaner/mover for macOS
# Usage: cleanup.sh file1 [file2 …]
#

set -euo pipefail
IFS=$'\n\t'

# colors
RED='\033[31m'
GREEN='\033[32m'
BLUE='\033[34m'
BOLD='\033[1m'
RESET='\033[0m'

# arrays for config
declare -a DIR_KEYS DIR_PATHS
last_target=""

# load ~/.config/cleanup.conf
load_config() {
  local cfg="$HOME/.config/cleanup.conf"
  [[ -r $cfg ]] || return
  while IFS= read -r line; do
    # trim leading whitespace
    line="${line#"${line%%[![:space:]]*}"}"
    # skip empty or commented
    [[ -z $line || ${line:0:1} == "#" ]] && continue
    # must be key:path
    if [[ $line =~ ^([^:]):(.*)$ ]]; then
      key="${BASH_REMATCH[1]}"
      path="${BASH_REMATCH[2]}"
      # trim leading/trailing whitespace from path
      path="${path#"${path%%[![:space:]]*}"}"
      path="${path%"${path##*[![:space:]]}"}"
      DIR_KEYS+=("$key")
      DIR_PATHS+=("$path")
    fi
  done < "$cfg"
}

# list subdirs of $1, return array in SUBS
list_subdirs() {
  local base="$1"
  mapfile -t SUBS < <(find "$base" -maxdepth 1 -mindepth 1 -type d -print0 \
                     | xargs -0 -n1 basename \
                     | sort )
}

# choose one subdir (or base)
choose_subdir() {
  local base="$1"
  list_subdirs "$base"
  if (( ${#SUBS[@]} == 0 )); then
    echo "$base"
    return
  fi
  echo "Subdirectories of $base:"
  local i=1
  for d in "${SUBS[@]}"; do
    printf "%2d) %s\n" "$i" "$d"
    ((i++))
  done
  echo
  read -p "Choose subdir number (ENTER for base): " num
  if [[ -z $num ]]; then
    echo "$base"
  elif (( num >= 1 && num <= ${#SUBS[@]} )); then
    echo "$base/${SUBS[num-1]}"
  else
    echo "invalid" >&2
    return 1
  fi
}

# quickview via qlmanage
quickview() {
  local f="$1"
  qlmanage -p "$f" >& /dev/null
}

# prompt loop per file
process_file() {
  local file="$1"
  if [[ ! -e $file ]]; then
    echo -e "${RED}Error:${RESET} $file does not exist."
    return
  fi

  while true; do
    echo
    echo -e "File: ${BOLD}$file${RESET}"
    echo "q) Quit  d) Delete  v) Quickview  i) Ignore  m) Move  p) PrevDest  c) Custom path"
    read -n1 -s -p "Choose: " opt
    echo
    case $opt in
      q)
        exit 0
        ;;
      d)
        rm -v -- "$file"
        return
        ;;
      v)
        quickview "$file"
        ;;
      i)
        return
        ;;
      p)
        if [[ -z $last_target ]]; then
          echo "No previous target set."
        else
          dest="$last_target/$(basename "$file")"
          mv -v -- "$file" "$dest" && echo "Moved to $dest"
        fi
        return
        ;;
      m)
        if (( ${#DIR_KEYS[@]} == 0 )); then
          echo "No configured dirs (cleanup.conf)."
          continue
        fi
        echo "Configured destinations:"
        for idx in "${!DIR_KEYS[@]}"; do
          printf " %c) %s\n" "${DIR_KEYS[idx]}" "${DIR_PATHS[idx]}"
        done
        read -n1 -s -p "Select dir key: " k
        echo
        found_index=-1
        for idx in "${!DIR_KEYS[@]}"; do
          [[ "${DIR_KEYS[idx]}" == "$k" ]] && found_index=$idx && break
        done
        if (( found_index < 0 )); then
          echo "Invalid key."
          continue
        fi
        base="${DIR_PATHS[found_index]}"
        # pick a subdir or base
        target=$(choose_subdir "$base") || continue
        dest="$target/$(basename "$file")"
        mv -v -- "$file" "$dest" && echo "Moved to $dest"
        last_target="$target"
        return
        ;;
      c)
        # custom path with readline-style editing & tab-completion
        # requires bash with 'read -e'
        read -e -p "Enter target directory: " destdir
        # expand ~ and resolve
        destdir="${destdir/#\~/$HOME}"
        destdir="$(cd "$destdir" 2>/dev/null && pwd || echo "")"
        if [[ -z $destdir || ! -d $destdir ]]; then
          echo "Invalid directory."
          continue
        fi
        dest="$destdir/$(basename "$file")"
        mv -v -- "$file" "$dest" && echo "Moved to $dest"
        last_target="$destdir"
        return
        ;;
      *)
        echo "Invalid choice."
        ;;
    esac
  done
}

main() {
  if (( $# < 1 )); then
    echo "Usage: $0 file1 [file2 …]"
    exit 1
  fi
  load_config
  for f in "$@"; do
    process_file "$f"
  done
}

main "$@"
```

Notes:

1. Make sure you have `bash` (macOS’s default is 3.x; for associative arrays or newer features you may prefer Homebrew’s bash-5).  
2. The script uses `read -n1 -s` for single‐keypress menu selection.  
3. Custom‐path editing uses `read -e` so you can tab‐complete.  
4. Subdirectory selection is a simple numbered list.  
5. Quickview uses `qlmanage -p file`.  
6. Delete is `rm`.  
7. “PrevDest” reuses the last directory you moved into.  

Drop this script in your `PATH` (e.g. `~/bin/cleanup.sh`), ensure it’s executable, and you’re ready to go!
