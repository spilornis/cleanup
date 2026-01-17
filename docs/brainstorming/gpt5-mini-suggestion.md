PROMPT: What is the best way to make an MacOS app, that will watch the ~/Downloads folder. Whenever there is a new file added to the folder, the app will show a dialog box with a list of directories to choose from. The app will move the newly added app to the chosen directory.



Use a small native macOS app (Swift + SwiftUI or AppKit) that watches the Downloads folder with File System Events (FSEvents) or DispatchSourceFileSystemObject, presents a dialog when a new file appears, and moves the file to the selected destination. Below is a concise, practical plan and a simple implementation outline.

Why native Swift:
- Integrates with macOS notifications, dialogs, sandboxing, and Finder permissions.
- Easy to distribute as a small signed app or notarized app.

High-level design
1. Watch ~/Downloads for new files (FSEvents or DispatchSource).
2. Debounce/coalesce events (many events fire during a single copy).
3. Detect completion of the file write (size stable or exclusive open attempt).
4. Present a modal dialog with a list of directories (or a menu/notification with actions).
5. Move the file atomically to chosen destination (FileManager.moveItem).
6. Handle conflicts (rename or prompt), errors, and permissions (sandbox & security-scoped bookmarks if sandboxed).
7. Run as background app/menubar app or a LaunchAgent for background operation.

Key implementation details
- Watching:
  - For simple apps use DispatchSourceFileSystemObject on the Downloads directory’s file descriptor. It gives fast notifications.
  - For robust recursive watching or high-volume changes, use FSEvents.
- Debounce and completion detection:
  - On notification, wait a short interval (e.g., 0.5–1s) and check file size over 1–2 polls to ensure the file is no longer growing.
  - Alternatively, attempt to open the file with exclusive access; succeed when write is finished.
- UI:
  - Use SwiftUI for a modern dialog; AppKit if you need specific modal behavior or integration with Finder windows.
  - For minimal interruption, consider a macOS Notification with action buttons (list limited) or a sheet dialog from a menubar app.
- Moving files:
  - Use FileManager.moveItem(at:to:) for atomic move within same volume. If cross-volume, moveItem will copy then delete; handle errors.
  - If conflicts, append a suffix like " (1)" or present overwrite/rename options.
- Permissions:
  - If sandboxed, request security-scoped bookmarks for each chosen destination (persist them).
  - For non-sandboxed apps, the user must grant Full Disk Access only if needed; otherwise normal file access to Downloads is allowed.
- Launch behavior:
  - Provide an option to run at login (SMLoginItem for helper app) or create a LaunchAgent plist.

Minimal code outline (Swift + SwiftUI)
- This is a concise skeleton showing the watcher + dialog flow (conceptual — adapt and expand for production):

```swift
import SwiftUI
import Combine

@main
struct DownloadsWatcherApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) var delegate
    var body: some Scene {
        Settings { EmptyView() } // or menu/menubar UI
    }
}

class AppDelegate: NSObject, NSApplicationDelegate {
    var watcher: DownloadsWatcher?

    func applicationDidFinishLaunching(_ notification: Notification) {
        watcher = DownloadsWatcher()
    }
}

final class DownloadsWatcher {
    private var folderURL = FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Downloads")
    private var stream: FSEventStreamRef?
    private var pending = Set<URL>()
    private var checkTimer: Timer?

    init() {
        startWatching()
    }

    deinit { stopWatching() }

    func startWatching() {
        let callback: FSEventStreamCallback = { _, client, numEvents, eventPaths, eventFlags, eventIds in
            guard let client = client else { return }
            let watcher = Unmanaged<DownloadsWatcher>.fromOpaque(client).takeUnretainedValue()
            let paths = UnsafeMutableRawPointer(eventPaths).assumingMemoryBound(to: UnsafePointer<CChar>?.self)
            for i in 0..<numEvents {
                if let c = paths[Int(i)], let p = String(validatingUTF8: c) {
                    // coarse: add any new path under Downloads to pending list
                    watcher.enqueue(URL(fileURLWithPath: p))
                }
            }
        }

        var context = FSEventStreamContext(version: 0, info: UnsafeMutableRawPointer(Unmanaged.passUnretained(self).toOpaque()), retain: nil, release: nil, copyDescription: nil)
        stream = FSEventStreamCreate(nil,
                                     callback,
                                     &context,
                                     [folderURL.path] as CFArray,
                                     FSEventStreamEventId(kFSEventStreamEventIdSinceNow),
                                     0.5,
                                     FSEventStreamCreateFlags(kFSEventStreamCreateFlagFileEvents))
        if let s = stream {
            FSEventStreamScheduleWithRunLoop(s, CFRunLoopGetMain(), CFRunLoopMode.defaultMode.rawValue)
            FSEventStreamStart(s)
        }
    }

    func stopWatching() {
        if let s = stream {
            FSEventStreamStop(s)
            FSEventStreamInvalidate(s)
            FSEventStreamRelease(s)
            stream = nil
        }
    }

    func enqueue(_ url: URL) {
        // filter: only files directly added (not folder-level changes), ignore temp files
        guard url.path.hasPrefix(folderURL.path) else { return }
        DispatchQueue.main.async {
            self.pending.insert(url)
            self.scheduleCheck()
        }
    }

    func scheduleCheck() {
        checkTimer?.invalidate()
        checkTimer = Timer.scheduledTimer(withTimeInterval: 0.7, repeats: false) { _ in
            self.processPending()
        }
    }

    func processPending() {
        let toProcess = pending
        pending.removeAll()
        for url in toProcess {
            self.waitForStableFile(url: url) { stableURL in
                guard let stableURL = stableURL else { return }
                DispatchQueue.main.async {
                    self.promptMove(fileURL: stableURL)
                }
            }
        }
    }

    func waitForStableFile(url: URL, completion: @escaping (URL?) -> Void) {
        // Poll size twice to ensure stable
        let checkInterval: TimeInterval = 0.4
        func sizeOf(_ u: URL) -> UInt64? {
            (try? FileManager.default.attributesOfItem(atPath: u.path)[.size] as? UInt64) ?? nil
        }
        var first = sizeOf(url)
        DispatchQueue.global().asyncAfter(deadline: .now() + checkInterval) {
            let second = sizeOf(url)
            if first != nil && second != nil && first == second {
                completion(url)
            } else {
                // try again once
                first = second
                DispatchQueue.global().asyncAfter(deadline: .now() + checkInterval) {
                    let third = sizeOf(url)
                    if first != nil && third != nil && first == third {
                        completion(url)
                    } else {
                        completion(nil)
                    }
                }
            }
        }
    }

    func promptMove(fileURL: URL) {
        // Present a simple dialog listing target directories.
        // In production, build a nice SwiftUI sheet or NSAlert with NSPopUpButton.
        let targets = [
            FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Documents"),
            FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Desktop"),
            FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Archives")
        ]

        let alert = NSAlert()
        alert.messageText = "Move downloaded file?"
        alert.informativeText = fileURL.lastPathComponent
        let popup = NSPopUpButton(frame: NSRect(x: 0, y: 0, width: 300, height: 24), pullsDown: false)
        for t in targets { popup.addItem(withTitle: t.path) }
        alert.accessoryView = popup
        alert.addButton(withTitle: "Move")
        alert.addButton(withTitle: "Cancel")
        let resp = alert.runModal()
        if resp == .alertFirstButtonReturn, let chosen = popup.selectedItem?.title {
            let destURL = URL(fileURLWithPath: chosen).appendingPathComponent(fileURL.lastPathComponent)
            move(fileURL: fileURL, to: destURL)
        }
    }

    func move(fileURL: URL, to dest: URL) {
        do {
            try FileManager.default.moveItem(at: fileURL, to: dest)
        } catch {
            // handle conflict by renaming
            let destWithSuffix = uniqueURL(for: dest)
            do { try FileManager.default.moveItem(at: fileURL, to: destWithSuffix) } catch {
                // show error to user
                let alert = NSAlert(error: error)
                alert.runModal()
            }
        }
    }

    func uniqueURL(for url: URL) -> URL {
        var candidate = url
        let fm = FileManager.default
        var i = 1
        while fm.fileExists(atPath: candidate.path) {
            let base = url.deletingPathExtension().lastPathComponent
            let ext = url.pathExtension
            let dir = url.deletingLastPathComponent()
            let newName = "\(base) (\(i))" + (ext.isEmpty ? "" : ".\(ext)")
            candidate = dir.appendingPathComponent(newName)
            i += 1
        }
        return candidate
    }
}
```

Additional production considerations
- Exclude temporary download partial files (Safari/Chrome use .download or .crdownload suffixes) until completed.
- Use security-scoped bookmarks if sandboxing and you need persistent access to user-selected folders.
- Add preferences: destinations list, run at login, ignore patterns, auto-move rules (based on extension), notifications, and logging.
- Test cross-volume moves, permission-denied cases, and Finder integration (e.g., reveal moved file).
- Sign and notarize if distributing to users.

If you want, I can:
- Provide a ready-to-build Xcode sample project with UI and preferences.
- Replace FSEvents with DispatchSource approach and show that code.
- Add rules to auto-classify and auto-move without prompting. Which would you prefer?
