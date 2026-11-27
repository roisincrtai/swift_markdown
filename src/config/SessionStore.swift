import Combine
import Darwin
import Foundation
import os

final class FileAccess: Sendable {
    let url: URL
    private let scopedURL: URL
    let isScoped: Bool

    init(_ url: URL) {
        self.url = url.standardizedFileURL
        scopedURL = url
        isScoped = url.startAccessingSecurityScopedResource()
    }

    convenience init(_ reference: FileReference) {
        var stale = false
        if let bookmark = reference.bookmark,
           let resolved = try? URL(resolvingBookmarkData: bookmark, options: [.withSecurityScope, .withoutUI],
                                   relativeTo: nil, bookmarkDataIsStale: &stale) {
            self.init(resolved)
        } else {
            self.init(reference.url)
        }
    }

    deinit {
        if isScoped { scopedURL.stopAccessingSecurityScopedResource() }
    }

    func reference(for target: URL? = nil) -> FileReference {
        let target = target ?? scopedURL
        let bookmark = try? target.bookmarkData(options: [.withSecurityScope],
                                                includingResourceValuesForKeys: nil, relativeTo: nil)
        return FileReference(target, bookmark: bookmark)
    }

    func contains(_ file: URL) -> Bool {
        let directory = url.resolvingSymlinksInPath().standardizedFileURL.path
        let path = file.resolvingSymlinksInPath().standardizedFileURL.path
        return path.hasPrefix(directory == "/" ? directory : directory + "/")
    }
}

@MainActor
final class SessionStore: ObservableObject {
    static let shared = SessionStore()
    static let realHome: URL = {
        if let home = getpwuid(getuid())?.pointee.pw_dir {
            return URL(fileURLWithPath: String(cString: home), isDirectory: true)
        }
        return FileManager.default.homeDirectoryForCurrentUser
    }()

    @Published private(set) var recentFiles: [FileReference]
    @Published private(set) var directory: FileReference
    private(set) var state: UserSessionState
    private(set) var directoryAccess: FileAccess
    private let writer: SessionWriter
    private var scheduledWrite: DispatchWorkItem?

    private init() {
        let relative = ".config/swift_markdown/config_user.yaml"
        let primary = Self.realHome.appendingPathComponent(relative)
        let container = URL(fileURLWithPath: NSHomeDirectory(), isDirectory: true).appendingPathComponent(relative)
        var locations = primary == container ? [primary] : [primary, container]
        state = UserSessionState()
        for candidate in locations {
            guard let data = try? Data(contentsOf: candidate) else { continue }
            if let restored = try? SessionYAML.decode(data) {
                state = restored
                break
            }
            SessionWriter.logger.error("Could not read the settings at \(candidate.path, privacy: .private)")
            // Preserve a settings file that could not be decoded.
            locations.removeAll { $0 == candidate }
        }
        let selected = state.directory ?? FileReference(Self.realHome.appendingPathComponent("Documents", isDirectory: true))
        let access = FileAccess(selected)
        directory = FileReference(access.url, bookmark: selected.bookmark)
        directoryAccess = access
        recentFiles = state.recentFiles.filter { $0.path.hasPrefix("/") }
        writer = SessionWriter(locations: locations)
    }

    func chooseDirectory(_ url: URL) {
        let access = FileAccess(url)
        let reference = access.reference()
        directoryAccess = access
        directory = reference
        state.directory = reference
        scheduleWrite()
    }

    func noteOpened(_ reference: FileReference, previousPath: String? = nil) {
        guard reference.path.hasPrefix("/") else { return }
        let existing = state.recentFiles.first { $0.path == reference.path }
        let current = FileReference(reference.url, bookmark: reference.bookmark ?? existing?.bookmark)
        if let previousPath, previousPath != current.path, let view = state.views.removeValue(forKey: previousPath) {
            state.views[current.path] = view
        }
        let updated = Array(([current] + state.recentFiles.filter {
            $0.path != current.path && $0.path != previousPath
        }).prefix(50))
        guard state.lastOpened != current || state.recentFiles != updated else { return }
        state.lastOpened = current
        state.recentFiles = updated
        if recentFiles != updated { recentFiles = updated }
        let kept = Set(updated.map(\.path))
        state.views = state.views.filter { kept.contains($0.key) }
        scheduleWrite()
    }

    func updateView(_ view: DocumentViewState, for file: URL?) {
        var changed = false
        if let window = view.window, state.window != window {
            state.window = window
            changed = true
        }
        if let path = file?.standardizedFileURL.path, state.views[path] != view {
            state.views[path] = view
            changed = true
        }
        if changed { scheduleWrite() }
    }

    func flush() {
        scheduledWrite?.cancel()
        scheduledWrite = nil
        writer.queue.sync { writer.write(state) }
    }

    private func scheduleWrite() {
        scheduledWrite?.cancel()
        let snapshot = state
        let writer = writer
        let work = DispatchWorkItem { writer.write(snapshot) }
        scheduledWrite = work
        writer.queue.asyncAfter(deadline: .now() + .milliseconds(350), execute: work)
    }
}

private final class SessionWriter {
    static let logger = Logger(subsystem: "org.swiftmarkdown.viewer", category: "session")
    let queue = DispatchQueue(label: "org.swiftmarkdown.session", qos: .utility)
    private let locations: [URL]

    init(locations: [URL]) { self.locations = locations }

    func write(_ state: UserSessionState) {
        guard let data = try? SessionYAML.encode(state) else { return }
        for destination in locations {
            do {
                try FileManager.default.createDirectory(at: destination.deletingLastPathComponent(),
                                                        withIntermediateDirectories: true)
                try data.write(to: destination, options: .atomic)
                return
            } catch {
                Self.logger.error("Could not save settings at \(destination.path, privacy: .private): \(error.localizedDescription, privacy: .public)")
            }
        }
    }
}
