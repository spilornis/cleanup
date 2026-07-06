# cleanup
Minimal utility to declutter your Downloads folder (or any directory). It walks the current directory and asks where each file should go.

## Warnings

- **Use with care:** This tool can permanently delete files or move them without additional confirmation.
- **No undo:** Deleted files are removed from the filesystem immediately. There is no built-in recovery or trash-bin support.
- **Path errors can be costly:** Double-check destination paths before moving files, especially when using custom paths or configured targets.
- **Existing files may be overwritten:** Moves may replace files at the destination depending on the underlying system command behavior.
- **Cross-filesystem moves use `mv`:** If a file is moved across different filesystems, the program may invoke the system `mv` command.
- **Config paths are trusted:** The program loads destination directories from `~/.config/cleanup.conf`. Incorrect entries may cause unexpected behavior.
- **External command execution is powerful:** The `x` option runs user-supplied commands. Only enter commands you trust.
- **Terminal mode changes:** The program temporarily changes terminal input behavior. If it exits unexpectedly, you may need to run `reset` to restore your terminal.
- **Quick Look dependency:** The preview feature relies on macOS `qlmanage`. It may not work in all environments.
- **Not intended for critical data cleanup:** Back up important files before using this tool on production or irreplaceable data.

## Installation
Build and place `cleanup` on your PATH.

## Requirements
- macOS (uses `qlmanage` for Quick Look previews)

## Setup
Create a config file listing shortcut keys and destination directories.
Config lives at `~/.config/cleanup.conf`.

```bash
touch ~/.config/cleanup.conf
echo "s:/Users/user_name/standing/" >> ~/.config/cleanup.conf
echo "m:/Users/user_name/Movies/" >> ~/.config/cleanup.conf
echo "p:/Users/user_name/Pictures/" >> ~/.config/cleanup.conf
echo "a:/Users/user_name/Music/" >> ~/.config/cleanup.conf
echo "w:/Users/"

```
Config format is `key:/absolute/path/` (one per line). The key is the shortcut you press at the prompt.

## Usage
1. Navigate to the directory you want to declutter.
2. Run `cleanup *`.
3. For each file, choose:
   1. Quit
   2. Delete (**BEWARE**: **DELETES** the file. NO RECOVERY!)
   3. Quickview (macOS Quick Look)
   4. Ignore
   5. Move (pick from `cleanup.conf`)
   6. PrevDest (reuse last destination)
   7. Custom (enter a path)
   8. Run external program

## Options
No flags. Provide one or more files:
`cleanup file1 [file2...]`

## Example workflow
When you drop a handful of files into Downloads, run `cleanup *`, preview with Quick Look as needed, and tap the shortcut key for common destinations.

## Troubleshooting
- If no destinations show up, check that `~/.config/cleanup.conf` exists and has `key:/absolute/path/` lines.
- If Quick Look does not appear, ensure `qlmanage` is available and the file type is supported.

## Safety
Moves are executed via `rename(2)` when possible, falling back to `mv -f` across filesystems.

## Compiling

```bash
clang -Wall -Wextra -std=c11 cleanup.c -o cleanup -lreadline
```
OR

```bash
zig cc cleanup.c -o cleanup
