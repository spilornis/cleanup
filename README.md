# cleanup
Minimal utility to declutter your Downloads folder (or any directory). It walks the current directory and asks where each file should go.


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
   2. Quickview (macOS Quick Look)
   3. Move (pick from `cleanup.conf`)
   4. PrevDest (reuse last destination)
   5. Custom (enter a path)

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
zig cc cleanup.c -o cleanup
```
