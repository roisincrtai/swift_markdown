import AppKit
import UniformTypeIdentifiers
import WebKit

@MainActor
final class MarkdownPDFExporter: NSObject, WKNavigationDelegate {
    private var webView: WKWebView?
    private var output: FileAccess?
    private var imageAccess: MarkdownDocument?
    private var timeout: DispatchWorkItem?
    private var completion: (() -> Void)?
    private var progressPanel: NSAlert?
    private var progressBar: NSProgressIndicator?
    private var writer: PDFDocumentWriter?
    private var pages: [PDFSlice] = []
    private var title = "Markdown"

    func present(for document: MarkdownDocument, in window: NSWindow, completion: @escaping () -> Void) {
        self.completion = completion
        title = document.displayName
        let panel = NSSavePanel()
        panel.allowedContentTypes = [.pdf]
        panel.canCreateDirectories = true
        panel.nameFieldStringValue = document.fileURL?.deletingPathExtension().lastPathComponent ?? title
        panel.prompt = "Export"
        panel.beginSheetModal(for: window) { [weak self, weak document, weak window] button in
            guard let self else { return }
            guard button == .OK, let url = panel.url, let document, let window else {
                self.finish()
                return
            }
            self.output = FileAccess(url)
            self.showProgress(in: window)
            self.armTimeout()
            let directory = document.model.directory
            document.model.snapshot { [weak self, document] rendered in
                self?.load(rendered.blocks, directory: directory, retaining: document)
            }
        }
    }

    private func showProgress(in window: NSWindow) {
        let alert = NSAlert()
        alert.messageText = "Exporting PDF…"
        alert.addButton(withTitle: "Cancel")
        let progress = NSProgressIndicator(frame: NSRect(x: 0, y: 0, width: 240, height: 14))
        progress.style = .bar
        progress.isIndeterminate = true
        progress.isDisplayedWhenStopped = true
        progress.startAnimation(nil)
        alert.accessoryView = progress
        progressPanel = alert
        progressBar = progress
        alert.beginSheetModal(for: window) { [weak self] _ in
            if self?.progressPanel != nil { self?.finish() }
        }
    }

    private func armTimeout() {
        timeout?.cancel()
        let timer = DispatchWorkItem { [weak self] in self?.finish(error: URLError(.timedOut)) }
        timeout = timer
        DispatchQueue.main.asyncAfter(deadline: .now() + .seconds(25), execute: timer)
    }

    private func load(_ blocks: [String], directory: URL?, retaining document: MarkdownDocument) {
        guard completion != nil else { return }
        do {
            let html = try PreviewPage.printPage(blocks: blocks)
            imageAccess = document
            let files = LocalImages()
            files.root = directory
            let view = WKWebView(frame: CGRect(x: 0, y: 0, width: PDFDocumentWriter.contentWidth,
                                              height: PDFDocumentWriter.contentHeight),
                                configuration: PreviewPage.configuration(files: files))
            self.webView = view
            view.navigationDelegate = self
            armTimeout()
            view.loadHTMLString(html, baseURL: nil)
        } catch {
            finish(error: error)
        }
    }

    func webView(_ view: WKWebView, didFinish navigation: WKNavigation!) {
        guard view === webView, writer == nil else { return }
        armTimeout()
        view.callAsyncJavaScript(Self.measure, arguments: ["heightLimit": PDFDocumentWriter.contentHeight],
                                 in: nil, in: .defaultClient) { [weak self, weak view] result in
            guard let self, let view, view === self.webView else { return }
            do {
                guard let layout = try result.get() as? [String: Any],
                      let height = layout["height"] as? Double,
                      let breaks = layout["breaks"] as? [Double] else { throw CocoaError(.fileReadCorruptFile) }
                self.pages = try PDFPagination.plan(height: height, pageHeight: PDFDocumentWriter.contentHeight,
                                                    breakpoints: breaks)
                self.writer = try PDFDocumentWriter(title: self.title)
                self.progressBar?.stopAnimation(nil)
                self.progressBar?.isIndeterminate = false
                self.progressBar?.maxValue = Double(self.pages.count)
                self.renderPage(0, view: view)
            } catch {
                self.finish(error: error)
            }
        }
    }

    private func renderPage(_ index: Int, view: WKWebView) {
        guard let writer, let output, view === webView else { return }
        if index == pages.count {
            do {
                try writer.save(to: output.url, pageCount: pages.count)
                finish()
            } catch {
                finish(error: error)
            }
            return
        }

        armTimeout()
        let slice = pages[index]
        view.callAsyncJavaScript(
            """
            const article = document.getElementById('document');
            article.style.top = -start + 'px';
            article.getBoundingClientRect();
            """,
            arguments: ["start": slice.start], in: nil, in: .defaultClient
        ) { [weak self, weak view] result in
            guard let self, let view, view === self.webView else { return }
            do {
                _ = try result.get()
                self.capture(slice, index: index, view: view)
            } catch {
                self.finish(error: error)
            }
        }
    }

    private func capture(_ slice: PDFSlice, index: Int, view: WKWebView) {
        let configuration = WKPDFConfiguration()
        configuration.rect = CGRect(x: 0, y: 0, width: PDFDocumentWriter.contentWidth, height: slice.height)
        view.createPDF(configuration: configuration) { [weak self, weak view] result in
            guard let self, let view, view === self.webView, let writer = self.writer else { return }
            do {
                try writer.append(result.get(), usedHeight: slice.height)
                self.progressBar?.doubleValue = Double(index + 1)
                self.renderPage(index + 1, view: view)
            } catch {
                self.finish(error: error)
            }
        }
    }

    func webView(_ view: WKWebView, didFail navigation: WKNavigation!, withError error: any Error) {
        finish(error: error)
    }

    func webView(_ view: WKWebView, didFailProvisionalNavigation navigation: WKNavigation!, withError error: any Error) {
        finish(error: error)
    }

    func webViewWebContentProcessDidTerminate(_ view: WKWebView) {
        finish(error: CocoaError(.fileWriteUnknown))
    }

    private func finish(error: (any Error)? = nil) {
        guard let done = completion else { return }
        completion = nil
        timeout?.cancel()
        timeout = nil
        webView?.navigationDelegate = nil
        webView?.stopLoading()
        webView = nil
        imageAccess = nil
        output = nil
        writer = nil
        let progress = progressPanel
        progressPanel = nil
        progressBar = nil
        if let sheet = progress?.window, let parent = sheet.sheetParent {
            parent.endSheet(sheet)
        }
        done()
        if let error { DispatchQueue.main.async { NSApp.presentError(error) } }
    }

    private static let measure = """
    const main = document.getElementById('document');
    const blocks = Array.from(main.children);
    const breaks = [];
    let priorWasHeading = false;
    for (const block of blocks) {
        const rect = block.getBoundingClientRect();
        if (!priorWasHeading) breaks.push(rect.top);
        priorWasHeading = /^H[1-6]$/.test(block.firstElementChild?.tagName || '');
        if (rect.height <= heightLimit) continue;
        for (const element of block.querySelectorAll('li, tr')) {
            breaks.push(element.getBoundingClientRect().top);
        }
        const protectedRects = Array.from(block.querySelectorAll('math, img'), node => node.getBoundingClientRect());
        const walker = document.createTreeWalker(block, NodeFilter.SHOW_TEXT);
        const range = document.createRange();
        while (walker.nextNode()) {
            const node = walker.currentNode;
            if (node.parentElement.closest('math')) continue;
            range.selectNodeContents(node);
            for (const line of range.getClientRects()) {
                const end = line.bottom;
                if (!protectedRects.some(rect => rect.top < end && rect.bottom > end)) breaks.push(end);
            }
        }
    }
    return {height: main.getBoundingClientRect().bottom, breaks};
    """
}
