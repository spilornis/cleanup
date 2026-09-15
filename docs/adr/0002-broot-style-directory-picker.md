# Broot-Style Fuzzy Directory Picker for Configured Destinations

## Status

Accepted

## Context

After picking a Configured Destination's key, the old flow listed only that destination's immediate subdirectories in a numbered two-column list and asked the user to type a number (or Enter for the destination's root). Any subdirectory more than one level deep was unreachable this way — the user had to fall back to Custom Destination and type the full path by hand, losing the one-key convenience Configured Destinations exist for.

## Decision

Replace the numbered subdirectory list with a `broot`-style interactive picker (`pick_dir_broot`). It recursively indexes every directory under the Configured Destination (skipping hidden directories, using `lstat` rather than `stat` so a symlink back up the tree can't loop the walk forever) and lets the user narrow that full list by typing a fuzzy, case-insensitive subsequence filter (e.g. `gam` matches `beta/gamma`), move a highlighted selection with the arrow keys, and confirm with Enter; Esc cancels back to the file menu. The picker takes over the terminal itself — raw mode with echo off, so it fully controls rendering — and hands it back in the program's normal raw-with-echo mode when done, rather than round-tripping through cooked mode the way the old `fgets`-based prompt did.

The walk is capped at `MAX_ALL_DIRS` (4000) entries and cached per destination path (`dir_tree_cache`, 6 trees) so repeat Moves into the same destination during a run don't re-walk disk.

## Considered Options

- **Keep the numbered list, just recurse deeper** — rejected. Beyond a couple of levels the flat numbered list becomes too long to scan, and typing a number as the tree grows offers no way to narrow it down; a filter is necessary once the tree is fully indexed.
- **Shell out to a real `broot`/`fzf` binary** — rejected. Adds an external runtime dependency for a small, self-contained piece of interaction logic, and loses control over exactly how the result maps back into the Move flow.
- **Unbounded tree walk / cache** — rejected. A destination tree of unbounded size could stall the picker's first open or exhaust memory; a fixed cap keeps worst-case cost predictable at the price of silently truncating extremely large trees.

## Consequences

- The first Move into a given Configured Destination pays a one-time recursive disk walk (bounded by `MAX_ALL_DIRS`); subsequent Moves into the same destination during the same run reuse the cache.
- A destination tree with more than `MAX_ALL_DIRS` directories will have some directories missing from the picker; there is no warning shown when this truncation happens.
- `choose_subdir`'s enter-for-base-directory shortcut is gone; the base directory is now just the top entry (shown as `.`) in the same filtered list.
