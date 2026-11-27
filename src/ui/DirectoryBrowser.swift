import AppKit
import SwiftUI

enum SidebarMode: String, CaseIterable {
    case outline = "Outline"
    case directory = "Directory"
    case recent = "Recent"

    var icon: String {
        switch self {
        case .outline: "list.bullet.indent"
        case .directory: "folder"
        case .recent: "clock.arrow.circlepath"
        }
    }
}

struct DirectoryEntry: Identifiable {
    let url: URL
    let isDirectory: Bool
    var id: URL { url }
    var name: String { url.lastPathComponent }
}

struct DirectoryRow: Identifiable {
    let entry: DirectoryEntry
    let depth: Int
    var id: URL { entry.id }
}

@MainActor
final class DirectoryBrowser: ObservableObject {
    @Published private(set) var rows: [DirectoryRow] = []
    @Published private(set) var expanded: Set<URL> = []
    @Published private(set) var rootStatus: RootStatus = .loading

    enum RootStatus { case loading, empty, loaded, inaccessible }

    private let queue = DispatchQueue(label: "org.swiftmarkdown.directory", qos: .userInitiated)
    private var access: FileAccess?
    private var contents: [URL: [DirectoryEntry]] = [:]
    private var pending: Set<URL> = []
    private var generation = 0

    func setRoot(_ root: FileAccess) {
        guard access !== root else { return }
        access = root
        generation += 1
        contents = [:]
        pending = []
        expanded = []
        rows = []
        rootStatus = .loading
        load(root.url)
    }

    func toggle(_ directory: URL) {
        if expanded.remove(directory) == nil {
            expanded.insert(directory)
            if contents[directory] == nil { load(directory) }
        }
        rebuild()
    }

    private func load(_ folder: URL) {
        guard let access, pending.insert(folder).inserted else { return }
        let revision = generation
        queue.async { [weak self, access] in
            let result = try? Self.entries(in: folder)
            DispatchQueue.main.async {
                guard let self, self.generation == revision else { return }
                self.pending.remove(folder)
                self.contents[folder] = result ?? []
                if folder == access.url {
                    self.rootStatus = result.map { $0.isEmpty ? .empty : .loaded } ?? .inaccessible
                }
                self.rebuild()
            }
        }
    }

    nonisolated static func entries(in directory: URL) throws -> [DirectoryEntry] {
        let keys: Set<URLResourceKey> = [.isDirectoryKey, .isRegularFileKey, .isSymbolicLinkKey, .isPackageKey]
        let urls = try FileManager.default.contentsOfDirectory(at: directory, includingPropertiesForKeys: Array(keys),
                                                               options: [.skipsHiddenFiles])
        return urls.compactMap { url -> DirectoryEntry? in
            guard let values = try? url.resourceValues(forKeys: keys), values.isSymbolicLink != true else { return nil }
            if values.isDirectory == true && values.isPackage != true {
                return DirectoryEntry(url: url, isDirectory: true)
            }
            guard values.isRegularFile == true, ["md", "txt"].contains(url.pathExtension.lowercased()) else { return nil }
            return DirectoryEntry(url: url, isDirectory: false)
        }.sorted { left, right in
            if left.isDirectory != right.isDirectory { return left.isDirectory }
            return left.name.localizedStandardCompare(right.name) == .orderedAscending
        }
    }

    private func rebuild() {
        guard let root = access?.url else { return }
        var result: [DirectoryRow] = []
        func visit(_ folder: URL, depth: Int) {
            for entry in contents[folder] ?? [] {
                result.append(DirectoryRow(entry: entry, depth: depth))
                if entry.isDirectory, expanded.contains(entry.url) { visit(entry.url, depth: depth + 1) }
            }
        }
        visit(root, depth: 0)
        rows = result
    }
}

struct DirectorySidebar: View {
    @ObservedObject var session: SessionStore
    @StateObject private var browser = DirectoryBrowser()
    var currentFile: URL?
    let open: (URL) -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 5) {
            HStack(spacing: 7) {
                Image(systemName: "folder")
                Text(session.directory.url.lastPathComponent).lineLimit(1)
                Spacer(minLength: 0)
                Button(action: chooseFolder) {
                    Image(systemName: "folder.badge.plus")
                }
                .buttonStyle(.borderless)
                .help("Choose Folder")
                .accessibilityLabel("Choose Folder")
            }
            .font(.system(size: 12, weight: .semibold))
            .padding(.horizontal, 12)
            .padding(.vertical, 7)
            Divider()
            switch browser.rootStatus {
            case .loading:
                ProgressView().controlSize(.small).padding(12)
            case .empty:
                sidebarMessage("No Markdown or text files in this folder.")
            case .inaccessible:
                VStack(alignment: .leading, spacing: 8) {
                    Text("Choose a folder to grant access.")
                        .font(.system(size: 12)).foregroundStyle(.secondary)
                    Button("Choose Folder…", action: chooseFolder)
                }.padding(12)
            case .loaded:
                ScrollView {
                    LazyVStack(alignment: .leading, spacing: 2) {
                        ForEach(browser.rows) { row in
                            let entry = row.entry
                            let selected = entry.url.standardizedFileURL == currentFile?.standardizedFileURL
                            Button {
                                if entry.isDirectory { browser.toggle(entry.url) }
                                else { open(entry.url) }
                            } label: {
                                HStack(spacing: 6) {
                                    if entry.isDirectory {
                                        Image(systemName: browser.expanded.contains(entry.url) ? "chevron.down" : "chevron.right")
                                            .font(.system(size: 8, weight: .semibold))
                                            .frame(width: 10)
                                    } else {
                                        Color.clear.frame(width: 10, height: 1)
                                    }
                                    Image(systemName: entry.isDirectory ? "folder" : "doc.text")
                                    Text(entry.name).lineLimit(1).truncationMode(.middle)
                                    Spacer(minLength: 0)
                                }
                                .font(.system(size: 12))
                                .padding(.leading, CGFloat(min(row.depth, 12) * 11 + 6))
                                .padding(.vertical, 5)
                                .contentShape(Rectangle())
                                .background(selected ? Color.accentColor.opacity(0.17) : .clear,
                                            in: RoundedRectangle(cornerRadius: 5))
                            }
                            .buttonStyle(.plain)
                            .help(entry.url.path)
                        }
                    }.padding(.horizontal, 7)
                }
            }
            Spacer(minLength: 0)
        }
        .onAppear { browser.setRoot(session.directoryAccess) }
        .onChange(of: session.directory) { _, _ in browser.setRoot(session.directoryAccess) }
    }

    private func chooseFolder() {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.allowsMultipleSelection = false
        panel.directoryURL = session.directory.url
        panel.prompt = "Choose"
        panel.begin { response in
            if response == .OK, let url = panel.url { session.chooseDirectory(url) }
        }
    }
}

struct RecentSidebar: View {
    let files: [FileReference]
    let currentFile: URL?
    let open: (FileReference) -> Void

    var body: some View {
        if files.isEmpty {
            sidebarMessage("No recently opened files.")
        } else {
            ScrollView {
                LazyVStack(alignment: .leading, spacing: 2) {
                    ForEach(files) { file in
                        Button {
                            open(file)
                        } label: {
                            HStack(alignment: .top, spacing: 7) {
                                Image(systemName: "doc.text").foregroundStyle(.secondary)
                                VStack(alignment: .leading, spacing: 2) {
                                    Text(file.url.lastPathComponent).lineLimit(1).truncationMode(.middle)
                                    Text(file.url.deletingLastPathComponent().lastPathComponent)
                                        .font(.system(size: 10)).foregroundStyle(.secondary).lineLimit(1)
                                }
                                Spacer(minLength: 0)
                            }
                            .font(.system(size: 12))
                            .padding(6)
                            .contentShape(Rectangle())
                            .background(file.url == currentFile?.standardizedFileURL ?
                                        Color.accentColor.opacity(0.17) : .clear, in: RoundedRectangle(cornerRadius: 5))
                        }
                        .buttonStyle(.plain)
                        .help(file.path)
                    }
                }
                .padding(.horizontal, 8)
            }
        }
    }
}

func sidebarMessage(_ label: String) -> some View {
    Text(label)
        .font(.system(size: 12))
        .foregroundStyle(.secondary)
        .padding(.horizontal, 14)
        .padding(.top, 8)
        .frame(maxWidth: .infinity, alignment: .leading)
}
