# Cleanup

A macOS terminal utility that walks the user through a list of files one at a time, letting them delete, move, view, or run a command against each before advancing to the next.

## Language

**Move**:
Relocating a file from its current path to a destination directory, chosen via one of three routes: Configured Destination, Previous Destination, or Custom Destination.

**Configured Destination**:
A named destination directory the user pre-registered in `~/.config/cleanup.conf`, selected during a Move by its single-character key, then narrowed to a specific subdirectory via the Directory Picker.

**Directory Picker**:
The broot-style interactive finder shown after a Configured Destination key is chosen. It indexes every directory in the destination's tree (not just its immediate children) and lets the user narrow the list with a fuzzy-typed filter, move the highlight with the arrow keys, and confirm with Enter; Esc cancels back to the file menu.

**Previous Destination**:
The destination directory used by the most recently completed Move, offered as a one-key shortcut to repeat it for the next file.

**Custom Destination**:
A destination directory typed in by hand (with path completion) for a one-off Move, not saved in config.

**Local Move**:
A Move where source and destination are on the same filesystem, performed by the underlying `rename` syscall. Effectively instantaneous — never queued or backgrounded.

**Cross-Filesystem Move**:
A Move where source and destination are on different filesystems (mounted volumes, network shares), requiring a real data copy rather than a metadata-only rename. Slow enough to be worth backgrounding.
_Avoid_: EXDEV move

**Move Queue**:
The ordered, one-at-a-time backlog of pending Cross-Filesystem Moves waiting to run. At most one Move is In-Flight; the rest wait their turn.

**In-Flight Move**:
The single Cross-Filesystem Move currently executing in the background.

**Drain**:
Waiting for the Move Queue to empty (including the In-Flight Move) before the program is allowed to exit, whether by explicit quit or by running out of files.
