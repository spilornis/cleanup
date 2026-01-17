Below is a self-contained Python3 script for macOS that reproduces the behavior of your C program.  It:

  • Loads `~/.config/cleanup.conf` with entries like  
      a:/path/to/dir  
    (ignoring blank/comment lines).  
  • Iterates over files given on the command line.  
  • For each file, presents an interactive menu:  
      q) quit  
      d) delete  
      v) quickview (macOS Quick Look)  
      i) ignore  
      m) move into one of your preset dirs (with optional sub-dir selection)  
      p) move into your “previous” target  
      c) move into an arbitrary directory, with readline/tab-completion.  
  • Remembers the last “target” directory so you can replay it with “p”.  

Save this as `cleanup.py`, `chmod +x cleanup.py`, and run:

    ./cleanup.py file1 file2 …

—–

```python
#!/usr/bin/env python3
import os
import sys
import stat
import shutil
import subprocess
import readline
import termios
import tty

# ANSI color codes
RED   = "\033[31m"
GREEN = "\033[32m"
BLUE  = "\033[34m"
BOLD  = "\033[1m"
RESET = "\033[0m"

CONFIG_PATH = os.path.expanduser("~/.config/cleanup.conf")
MAX_SUBDIRS = 1000

# Globals
dirs = []           # list of (key, path)
last_target = ""    # last used move-to directory

def load_config():
    """Read ~/.config/cleanup.conf, populate dirs as (key, path)."""
    if not os.path.isfile(CONFIG_PATH):
        return
    with open(CONFIG_PATH, "r") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            # expect: key : path
            if len(line) >= 2 and line[1] == ":":
                key = line[0]
                path = line[2:].strip()
                dirs.append((key, os.path.expanduser(path)))

def list_subdirs(base):
    """Return list of immediate subdirectory names under base, skipping dot-dirs."""
    try:
        names = os.listdir(base)
    except OSError:
        return []
    subs = []
    for nm in names:
        if nm.startswith("."):
            continue
        full = os.path.join(base, nm)
        if os.path.isdir(full):
            subs.append(nm)
            if len(subs) >= MAX_SUBDIRS:
                break
    subs.sort()
    return subs

def print_two_cols(items):
    """Print items in 1 or 2 columns depending on length."""
    n = len(items)
    if n == 0:
        return
    cols = 2 if n > 20 else 1
    per = (n + cols - 1)//cols
    for i in range(per):
        line = ""
        for c in range(cols):
            idx = i + c*per
            if idx < n:
                line += f"{idx+1:3d}: {items[idx]:<30}"
        print(line)

def choose_subdir(base):
    """Let user pick one of base's subdirs or press Enter for base itself.
       Returns the full path or None on cancel/error."""
    subs = list_subdirs(base)
    if not subs:
        return base
    print_two_cols(subs)
    resp = input("Select subdir number (Enter for base): ").strip()
    if not resp:
        return base
    try:
        c = int(resp)
        if 1 <= c <= len(subs):
            return os.path.join(base, subs[c-1])
    except ValueError:
        pass
    return None

def quickview(path):
    """Invoke macOS Quick Look."""
    try:
        # '-p' will keep previewer open until you close it
        subprocess.run(["qlmanage","-p", path], check=False)
    except FileNotFoundError:
        print(RED + "qlmanage not found" + RESET)

def enable_raw_mode():
    """Put stdin into raw mode. Return the old tty attributes."""
    fd = sys.stdin.fileno()
    old = termios.tcgetattr(fd)
    tty.setraw(fd)
    return old

def restore_mode(old):
    """Restore the terminal to a previous state."""
    termios.tcsetattr(sys.stdin.fileno(), termios.TCSANOW, old)

def getch():
    """Read one character from stdin (raw mode assumed)."""
    return sys.stdin.read(1)

def to_absolute(p):
    """Expand ~, ., .., and return a normalized absolute path."""
    return os.path.realpath(os.path.expanduser(p))

def custom_path_prompt():
    """Prompt user with readline(tab-completion) for a directory."""
    # enable filename completion
    readline.parse_and_bind("tab: complete")
    readline.set_completer(readline.get_completer())
    try:
        resp = input("Custom path> ").strip()
    except EOFError:
        return None
    if not resp:
        return None
    final = to_absolute(resp)
    if os.path.isdir(final):
        return final
    else:
        print(RED + f"{final} is not a directory" + RESET)
        return None

def do_menu(file):
    """Present menu for one file, return False if we should quit entirely."""
    global last_target

    print(f"\n File: {BOLD}{file}{RESET}")
    print(" q) Quit   d) Delete   v) Quickview   i) Ignore")
    print(" m) Move       p) PrevDest   c) Custom path")
    opt = getch()
    print()  # newline after the keypress

    if opt == "q":
        return False

    if opt == "d":
        try:
            os.remove(file)
            print(GREEN + "Deleted." + RESET)
        except OSError as e:
            print(RED + f"Delete error: {e}" + RESET)
        return True

    if opt == "v":
        quickview(file)
        return True

    if opt == "i":
        # ignore
        return True

    if opt == "p":
        if not last_target:
            print(RED + "No previous target." + RESET)
            return True
        fname = os.path.basename(file)
        dest = os.path.join(last_target, fname)
        try:
            shutil.move(file, dest)
            print(GREEN + f"Moved to {dest}" + RESET)
        except OSError as e:
            print(RED + f"Move error: {e}" + RESET)
        return True

    if opt == "m":
        if not dirs:
            print(RED + "No dirs in config." + RESET)
            return True
        # list keys
        for (k, p) in dirs:
            print(f" {k}) {p}")
        print("Select dir key: ", end="", flush=True)

        # leave raw for single char
        key = getch()
        print()
        pair = next(((k,p) for (k,p) in dirs if k == key), None)
        if not pair:
            print(RED + "Invalid key." + RESET)
            return True
        _, base = pair

        # switch back to cooked for input()
        old = restore_and_get_cooked()
        sub = choose_subdir(base)
        old = enable_and_discard_raw(old)
        if sub is None:
            print(RED + "Subdir selection cancelled." + RESET)
            return True

        dest = os.path.join(sub, os.path.basename(file))
        try:
            shutil.move(file, dest)
            last_target = sub
            print(GREEN + f"Moved to {dest}" + RESET)
        except OSError as e:
            print(RED + f"Move error: {e}" + RESET)
        return True

    if opt == "c":
        # switch back to cooked to use input()
        old = restore_and_get_cooked()
        sub = custom_path_prompt()
        old = enable_and_discard_raw(old)
        if not sub:
            return True
        dest = os.path.join(sub, os.path.basename(file))
        try:
            shutil.move(file, dest)
            last_target = sub
            print(GREEN + f"Moved to {dest}" + RESET)
        except OSError as e:
            print(RED + f"Move error: {e}" + RESET)
        return True

    # any other key
    print(RED + "Bad option." + RESET)
    return True

def restore_and_get_cooked():
    """Restore terminal from raw to the normal (cooked) mode, return old attrs."""
    fd = sys.stdin.fileno()
    new = termios.tcgetattr(fd)
    termios.tcsetattr(fd, termios.TCSANOW, orig_termios)  # restore original
    return orig_termios

def enable_and_discard_raw(old):
    """Re-enable raw mode, return new old attrs (we ignore the incoming old)."""
    return enable_raw_mode()

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} file1 [file2 …]")
        sys.exit(1)

    load_config()
    # enable raw for single-char menu choices
    orig_termios = enable_raw_mode()

    try:
        for path in sys.argv[1:]:
            if not os.path.exists(path):
                print(RED + f"{path}: no such file" + RESET)
                continue
            keep_going = True
            while keep_going:
                keep_going = do_menu(path)
                # if the file was deleted or moved, stop asking about it:
                if not os.path.exists(path):
                    break
                if not keep_going:
                    sys.exit(0)
                # for d,i,p,m,c ⇒ we break after one action
                # for v (quickview) we loop again
                if keep_going and path and os.path.exists(path):
                    # quickview was the only option that returns True and loops
                    break
    finally:
        # restore original terminal settings
        restore_mode(orig_termios)
```

Explanation of key points:

  • We enter “raw” mode (no line buffering, immediate single-character reads) for the main menu, using `tty.setraw` + `termios`.  
  • For the “move” sub-menu and the custom-path prompt we restore “cooked” mode so that `input()` and `readline` work with Tab-completion.  
  • `load_config()` reads lines like  
      a:/Users/you/Photos  
      b:/Users/you/Documents  
    ignoring blank/comment lines.  
  • `last_target` is updated on each successful “m”, “p” or “c” move and can be replayed with “p”.  
  • Quick-Look is invoked via `qlmanage -p file`.  

You should now have almost feature-parity with your original C program, but in about 200 lines of Python!
