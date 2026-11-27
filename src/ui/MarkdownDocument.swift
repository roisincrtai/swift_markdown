import AppKit
import Combine
import MarkdownBridge

struct Heading: Identifiable, Equatable {
    let id: String
    let title: String
    let level: Int
    let line: Int
}

struct RenderedDocument: Equatable {
    var blocks: [String] = []
    var headings: [Heading] = []
}

private final class RenderWorker {
    private let queue = DispatchQueue(label: "org.swiftmarkdown.renderer", qos: .userInitiated)
    private let lock = NSLock()
    private let renderer: OpaquePointer?
    private var generation = 0

    init() { renderer = md_renderer_create() }
    deinit { md_renderer_destroy(renderer) }

    func submit(_ source: String, deliver: @escaping (RenderedDocument) -> Void) {
        lock.lock()
        generation += 1
        let revision = generation
        lock.unlock()
        queue.asyncAfter(deadline: .now() + .milliseconds(36)) { [self] in
            guard isLatest(revision) else { return }
            let result = render(source)
            DispatchQueue.main.async { [self] in
                if isLatest(revision) { deliver(result) }
            }
        }
    }

    func snapshot(_ source: String, deliver: @escaping (RenderedDocument) -> Void) {
        queue.async { [self] in
            let result = render(source)
            DispatchQueue.main.async { deliver(result) }
        }
    }

    private func isLatest(_ revision: Int) -> Bool {
        lock.lock()
        defer { lock.unlock() }
        return revision == generation
    }

    private func render(_ source: String) -> RenderedDocument {
        let bytes = source.utf8CString
        guard let result = bytes.withUnsafeBufferPointer({ buffer in
            md_render(renderer, buffer.baseAddress, buffer.count - 1)
        }) else { return RenderedDocument() }
        defer { md_result_destroy(result) }

        return RenderedDocument(
            blocks: (0..<md_block_count(result)).map { String(cString: md_block(result, $0)) },
            headings: (0..<md_heading_count(result)).map { i in
                Heading(id: String(cString: md_heading_id(result, i)),
                        title: String(cString: md_heading_title(result, i)),
                        level: Int(md_heading_level(result, i)),
                        line: md_heading_line(result, i))
            }
        )
    }
}

final class DocumentModel: ObservableObject {
    var source = "" {
        didSet {
            guard source != oldValue else { return }
            worker.submit(source) { [weak self] value in
                guard let self, self.rendered != value else { return }
                self.rendered = value
            }
        }
    }
    @Published private(set) var rendered = RenderedDocument()
    @Published var directory: URL?
    @Published var showsSource = false
    @Published var jump: HeadingJump?
    @Published var collapsed: Set<String> = []
    @Published var sidebarMode: SidebarMode = .outline
    @Published var restoredPreview = PreviewRestore(location: nil)

    func snapshot(deliver: @escaping (RenderedDocument) -> Void) {
        worker.snapshot(source, deliver: deliver)
    }

    private let worker = RenderWorker()
}

@objc(MarkdownDocument)
final class MarkdownDocument: NSDocument {
    let model = DocumentModel()
    private var encoding = String.Encoding.utf8
    private var byteOrderMark = Data()
    private var access: FileAccess?
    private var inheritedAccess: FileAccess?
    private(set) var viewState = DocumentViewState()

    override var fileURL: URL? {
        didSet {
            let changed = fileURL
            guard changed != oldValue else { return }
            DispatchQueue.main.async { [weak self] in
                guard let self, self.fileURL == changed else { return }
                self.model.directory = changed?.deletingLastPathComponent()
                if !self.windowControllers.isEmpty, let changed {
                    let selected = FileAccess(changed)
                    self.keepAccess(selected, inherited: nil)
                    SessionStore.shared.noteOpened(selected.reference())
                    self.persistView()
                }
            }
        }
    }

    func keepAccess(_ access: FileAccess, inherited: FileAccess?) {
        if self.access?.url != access.url || access.isScoped || self.access?.isScoped != true {
            self.access = access
        }
        if let inherited { inheritedAccess = inherited }
        else if let previous = inheritedAccess, !previous.contains(access.url) { inheritedAccess = nil }
    }

    func restoreMovedFile(from path: String) {
        guard let view = SessionStore.shared.state.views[path] else { return }
        viewState = view
        model.restoredPreview = PreviewRestore(location: view.preview)
        (windowControllers.first as? ViewerWindowController)?.restoreWindow(view.window)
    }

    func noteActivation() {
        guard let fileURL else { return }
        let reference = access?.reference() ?? FileReference(fileURL)
        SessionStore.shared.noteOpened(reference)
    }

    func recordWindow(_ frame: CGRect) {
        viewState.window = WindowPlacement(frame)
        persistView()
    }

    func recordEditor(_ location: EditorLocation) {
        viewState.editor = location
        persistView()
    }

    func recordPreview(_ location: PreviewLocation) {
        viewState.preview = location
        persistView()
    }

    func persistView() {
        SessionStore.shared.updateView(viewState, for: fileURL)
    }

    override class var autosavesInPlace: Bool { true }

    override func read(from data: Data, ofType typeName: String) throws {
        let encodings: [([UInt8], String.Encoding)] = [
            ([0xEF, 0xBB, 0xBF], .utf8), ([0xFF, 0xFE], .utf16LittleEndian), ([0xFE, 0xFF], .utf16BigEndian)
        ]
        let identified = encodings.first { data.starts(with: $0.0) }
        let newEncoding = identified?.1 ?? .utf8
        let prefix = Data(identified?.0 ?? [])
        guard let text = String(data: data.dropFirst(prefix.count), encoding: newEncoding) else {
            throw CocoaError(.fileReadInapplicableStringEncoding)
        }
        encoding = newEncoding
        byteOrderMark = prefix
        model.objectWillChange.send()
        model.source = text
    }

    override func data(ofType typeName: String) throws -> Data {
        guard let data = model.source.data(using: encoding) else {
            throw CocoaError(.fileWriteInapplicableStringEncoding)
        }
        return byteOrderMark + data
    }

    override func makeWindowControllers() {
        model.directory = fileURL?.deletingLastPathComponent()
        let session = SessionStore.shared.state
        if let path = fileURL?.standardizedFileURL.path, let saved = session.views[path] {
            viewState = saved
        }
        model.restoredPreview = PreviewRestore(location: viewState.preview)
        addWindowController(ViewerWindowController(document: self, initialWindow: viewState.window ?? session.window))
    }

    func loadExample() {
        model.source = #"""
        # Swift Markdown

        Choose a section in the outline to jump to it, or press **⌥⌘E** to edit the Markdown source.

        ## Markdown

        Use **bold**, *italics*, and ~~strikethrough~~. Break up longer documents with headings.

        | Feature | Example |
        | :--- | :--- |
        | Inline code | `swift --version` |
        | Inline math | $\nabla \cdot \mathbf{E} = \rho/\varepsilon_0$ |
        | Display math | Delimit a formula with two dollar signs. |

        ## LaTeX formulas

        Inline formulas like $\sqrt{x^2+y^2}$ sit alongside text. Display formulas have space for fractions, limits, and matrices.

        $$
        \int_{-\infty}^{\infty} e^{-x^2}\,\mathrm{d}x = \sqrt{\pi}
        $$

        ### Matrices and alignment

        $$
        \begin{aligned}
        A &= \begin{bmatrix}1 & 2 \\ 3 & 4\end{bmatrix} \\
        \det A &= (1)(4) - (2)(3) = -2
        \end{aligned}
        $$

        ## Source editing

        - [x] Open a Markdown document with **⌘O**.
        - [ ] Show the source and edit a formula to see the preview change.
        """#
    }
}
