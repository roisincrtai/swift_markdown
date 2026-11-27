import AppKit
import SwiftUI

final class ViewerWindowController: NSWindowController, NSToolbarDelegate, NSWindowDelegate, NSMenuItemValidation {
    private static let sourceItem = NSToolbarItem.Identifier("markdown-source")
    private let model: DocumentModel
    private weak var markdownDocument: MarkdownDocument?
    private var exporter: MarkdownPDFExporter?

    init(document: MarkdownDocument, initialWindow: WindowPlacement?) {
        model = document.model
        markdownDocument = document
        let host = NSHostingController(rootView: Viewer(document: document, model: document.model))
        let window = NSWindow(contentViewController: host)
        window.setContentSize(NSSize(width: 1080, height: 730))
        window.minSize = NSSize(width: 765, height: 450)
        window.toolbarStyle = .unified
        window.tabbingMode = .preferred
        window.center()
        super.init(window: window)
        restoreWindow(initialWindow)
        window.delegate = self
        let toolbar = NSToolbar(identifier: "markdown-viewer")
        toolbar.delegate = self
        toolbar.displayMode = .iconOnly
        window.toolbar = toolbar
    }

    required init?(coder: NSCoder) { nil }

    func restoreWindow(_ placement: WindowPlacement?) {
        guard let window, let frame = placement?.frame else { return }
        let screens = NSScreen.screens.map(\.visibleFrame)
        guard let screen = screens.max(by: { overlap(frame, $0) < overlap(frame, $1) }),
              overlap(frame, screen) > 0 else { return }
        let width = min(screen.width, max(window.minSize.width, frame.width))
        let height = min(screen.height, max(window.minSize.height, frame.height))
        let x = min(max(frame.minX, screen.minX), screen.maxX - width)
        let y = min(max(frame.minY, screen.minY), screen.maxY - height)
        window.setFrame(CGRect(x: x, y: y, width: width, height: height), display: false)
    }

    private func overlap(_ frame: CGRect, _ screen: CGRect) -> CGFloat {
        let intersection = frame.intersection(screen)
        return intersection.isNull ? 0 : intersection.width * intersection.height
    }

    func windowDidBecomeKey(_ notification: Notification) {
        markdownDocument?.noteActivation()
        recordFrame()
    }

    func windowDidMove(_ notification: Notification) { recordFrame() }
    func windowDidResize(_ notification: Notification) { recordFrame() }
    func windowWillClose(_ notification: Notification) { recordFrame() }

    private func recordFrame() {
        if let window, !window.styleMask.contains(.fullScreen) {
            markdownDocument?.recordWindow(window.frame)
        }
    }

    @objc func exportMarkdownPDF(_ sender: Any?) {
        guard let markdownDocument, let window, exporter == nil else { return }
        let export = MarkdownPDFExporter()
        exporter = export
        export.present(for: markdownDocument, in: window) { [weak self] in self?.exporter = nil }
    }

    func toolbarAllowedItemIdentifiers(_ toolbar: NSToolbar) -> [NSToolbarItem.Identifier] {
        [.flexibleSpace, Self.sourceItem]
    }

    func toolbarDefaultItemIdentifiers(_ toolbar: NSToolbar) -> [NSToolbarItem.Identifier] {
        [.flexibleSpace, Self.sourceItem]
    }

    func toolbar(_ toolbar: NSToolbar, itemForItemIdentifier identifier: NSToolbarItem.Identifier,
                 willBeInsertedIntoToolbar flag: Bool) -> NSToolbarItem? {
        guard identifier == Self.sourceItem else { return nil }
        let item = NSToolbarItem(itemIdentifier: identifier)
        item.label = "Markdown Source"
        item.paletteLabel = "Markdown Source"
        item.toolTip = "Show Markdown Source (⌥⌘E)"
        item.image = NSImage(systemSymbolName: "chevron.left.forwardslash.chevron.right",
                             accessibilityDescription: "Markdown Source")
        item.target = self
        item.action = #selector(toggleMarkdownSource(_:))
        return item
    }

    @objc func toggleMarkdownSource(_ sender: Any?) {
        model.showsSource.toggle()
        window?.toolbar?.items.first { $0.itemIdentifier == Self.sourceItem }?.toolTip =
            model.showsSource ? "Hide Markdown Source (⌥⌘E)" : "Show Markdown Source (⌥⌘E)"
    }

    func validateMenuItem(_ menuItem: NSMenuItem) -> Bool {
        if menuItem.action == #selector(toggleMarkdownSource(_:)) {
            menuItem.title = model.showsSource ? "Hide Markdown Source" : "Show Markdown Source"
        }
        return true
    }
}

struct HeadingJump: Equatable {
    let id: String
    let token = UUID()
}

private struct OutlineNode: Identifiable {
    let heading: Heading
    var children: [OutlineNode]
    var id: String { heading.id }

    static func tree(from headings: [Heading]) -> [Self] {
        var index = 0
        func read(under level: Int) -> [Self] {
            var result: [Self] = []
            while index < headings.count && headings[index].level > level {
                let heading = headings[index]
                index += 1
                result.append(OutlineNode(heading: heading, children: read(under: heading.level)))
            }
            return result
        }
        return read(under: 0)
    }
}

private struct Viewer: View {
    weak var document: MarkdownDocument?
    @ObservedObject var model: DocumentModel
    @ObservedObject private var session = SessionStore.shared

    var body: some View {
        HSplitView {
            sidebar.frame(minWidth: 205, idealWidth: 240, maxWidth: 340)
            WebPreview(blocks: model.rendered.blocks, rootDirectory: model.directory, jump: model.jump,
                       restore: model.restoredPreview) { location in document?.recordPreview(location) }
                .accessibilityLabel("Markdown Preview")
                .frame(minWidth: 320, maxWidth: .infinity, maxHeight: .infinity)
                .layoutPriority(1)
            if model.showsSource {
                VStack(spacing: 0) {
                    paneLabel("Markdown Source", icon: "chevron.left.forwardslash.chevron.right")
                    Divider()
                    SourceEditor(source: $model.source, document: document)
                }
                .frame(minWidth: 230, idealWidth: 325, maxWidth: .infinity, maxHeight: .infinity)
            }
        }
        .background(Color(nsColor: .textBackgroundColor))
    }

    private var sidebar: some View {
        VStack(alignment: .leading, spacing: 0) {
            Picker("Sidebar", selection: $model.sidebarMode) {
                ForEach(SidebarMode.allCases, id: \.self) { mode in
                    Image(systemName: mode.icon)
                        .accessibilityLabel(mode.rawValue)
                        .help(mode.rawValue)
                        .tag(mode)
                }
            }
            .pickerStyle(.segmented)
            .labelsHidden()
            .padding(.horizontal, 9)
            .padding(.vertical, 8)

            switch model.sidebarMode {
            case .outline:
                outline
            case .directory:
                DirectorySidebar(session: session, currentFile: document?.fileURL, open: openFromDirectory)
            case .recent:
                RecentSidebar(files: session.recentFiles, currentFile: document?.fileURL, open: openRecent)
            }
        }
        .frame(maxHeight: .infinity, alignment: .topLeading)
        .background(Color(nsColor: .controlBackgroundColor))
        .accessibilityElement(children: .contain)
        .accessibilityLabel("Document Sidebar")
    }

    @ViewBuilder private var outline: some View {
        if model.rendered.headings.isEmpty {
            sidebarMessage("Headings appear here.")
        } else {
            ScrollView {
                VStack(alignment: .leading, spacing: 2) {
                    ForEach(OutlineNode.tree(from: model.rendered.headings)) { node in
                        OutlineRow(node: node, depth: 0, selected: model.jump?.id, collapsed: $model.collapsed) {
                            model.jump = HeadingJump(id: $0)
                        }
                    }
                }
                .padding(.horizontal, 8)
                .padding(.bottom, 10)
            }
        }
    }

    private func openFromDirectory(_ url: URL) {
        NSDocumentController.shared.openDocument(withContentsOf: url, display: true) { document, _, error in
            (document as? MarkdownDocument)?.model.sidebarMode = .directory
            if let error { NSApp.presentError(error) }
        }
    }

    private func openRecent(_ reference: FileReference) {
        guard let controller = NSDocumentController.shared as? MarkdownDocumentController else { return }
        controller.openRecent(reference) { document, _, error in
            (document as? MarkdownDocument)?.model.sidebarMode = .recent
            if let error { NSApp.presentError(error) }
        }
    }

    private func paneLabel(_ title: String, icon: String) -> some View {
        Label(title, systemImage: icon)
            .font(.system(size: 11, weight: .semibold))
            .foregroundStyle(.secondary)
            .padding(.horizontal, 15)
            .padding(.vertical, 11)
            .frame(maxWidth: .infinity, alignment: .leading)
    }
}

private struct OutlineRow: View {
    let node: OutlineNode
    let depth: Int
    let selected: String?
    @Binding var collapsed: Set<String>
    let select: (String) -> Void

    private var expanded: Bool { !collapsed.contains(node.id) }

    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            HStack(spacing: 2) {
                if node.children.isEmpty {
                    Color.clear.frame(width: 15, height: 1)
                } else {
                    Button {
                        if expanded { collapsed.remove(node.id) }
                        else { collapsed.insert(node.id) }
                    } label: {
                        Image(systemName: expanded ? "chevron.down" : "chevron.right")
                            .font(.system(size: 8, weight: .semibold))
                            .foregroundStyle(.secondary)
                            .frame(width: 15, height: 23)
                            .contentShape(Rectangle())
                    }
                    .buttonStyle(.plain)
                    .accessibilityLabel(expanded ? "Collapse" : "Expand")
                }
                Button {
                    select(node.id)
                } label: {
                    Text(node.heading.title.isEmpty ? "Untitled heading" : node.heading.title)
                        .font(.system(size: 12, weight: depth == 0 ? .medium : .regular))
                        .lineLimit(1)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .padding(.vertical, 5)
                        .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
                .accessibilityAddTraits(selected == node.id ? .isSelected : [])
            }
            .padding(.leading, CGFloat(depth * 13 + 3))
            .padding(.trailing, 6)
            .background(selected == node.id ? Color.accentColor.opacity(0.17) : .clear,
                        in: RoundedRectangle(cornerRadius: 5))

            if expanded {
                ForEach(node.children) { child in
                    OutlineRow(node: child, depth: depth + 1, selected: selected, collapsed: $collapsed, select: select)
                }
            }
        }
    }
}
