# Background Cross-Filesystem Moves via a Serialized, Threadless Queue

## Status

Accepted

## Context

Cross-Filesystem Moves shell out to `mv -f` and block the main loop on `waitpid`, so the UI freezes for the duration of the copy on a large file or slow volume. Local Moves (`rename`) are unaffected — they're metadata-only and already instantaneous.

## Decision

Only Cross-Filesystem Moves are backgrounded. They are placed on a Move Queue and run one at a time (never concurrently) by forking `mv` and polling its exit status with non-blocking `waitpid(pid, &status, WNOHANG)` — no `pthread`s are introduced. The main input loop moves from a blocking `getchar()` to `select()`-based polling on stdin so queue/completion status can be printed live, without waiting for the next keypress. The UI advances to the next file as soon as a Move is queued (optimistic advance), not when it completes; failures are reported after the fact via an error line rather than blocking progress. On quit or on running out of files, the program Drains the Move Queue before exiting rather than risking a truncated file at the destination.

## Considered Options

- **`pthread`-based worker** — rejected. We don't need byte-level progress, only start/complete signals, so a dedicated thread would exist purely to babysit `waitpid`, adding a mutex-protected shared status and the codebase's first cross-thread `stdout` usage for no real gain over non-blocking process polling.
- **Concurrent Moves** — rejected. Cross-Filesystem Moves are disk/volume-bound; running several at once tends to slow all of them down rather than speed anything up, and a single In-Flight Move keeps the status line to one line instead of a variable-length list.
- **Real percentage progress via a custom copy loop** — rejected. It would mean replacing `execlp("mv", ...)` with our own chunked read/write loop to track bytes copied. Decided against it: a simple "Moving file X" / "Moved file X" pair of messages was judged sufficient, so the added implementation surface wasn't justified.

## Consequences

- Failures surface asynchronously (a red error line), not by keeping the user on the failed file — the user must notice the line and re-move the file by hand; the source file is left untouched on failure.
- `select()`-based polling only runs inside `main()`'s own loop. The `readline()`-driven prompts (Custom Destination entry, external command entry) pause live status updates for their duration, resuming once control returns to the main loop.
