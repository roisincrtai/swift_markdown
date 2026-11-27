import AppKit
import SwiftUI
import UniformTypeIdentifiers
import WebKit

struct PreviewRestore: Equatable {
    let location: PreviewLocation?
    let token = UUID()
}

struct WebPreview: NSViewRepresentable {
    let blocks: [String]
    let rootDirectory: URL?
    let jump: HeadingJump?
    let restore: PreviewRestore
    let recordLocation: (PreviewLocation) -> Void

    func makeCoordinator() -> Coordinator { Coordinator() }

    func makeNSView(context: Context) -> WKWebView {
        let config = PreviewPage.configuration(files: context.coordinator.files)
        let controller = config.userContentController
        controller.add(context.coordinator, contentWorld: .defaultClient, name: "markdownScroll")
        controller.addUserScript(WKUserScript(source: Self.trackScroll, injectionTime: .atDocumentEnd,
                                               forMainFrameOnly: true, in: .defaultClient))

        let view = WKWebView(frame: .zero, configuration: config)
        view.navigationDelegate = context.coordinator
        context.coordinator.loadShell(view)
        return view
    }

    func updateNSView(_ view: WKWebView, context: Context) {
        context.coordinator.update(view, blocks: blocks, directory: rootDirectory, jump: jump,
                                   restore: restore, recordLocation: recordLocation)
    }

    static func dismantleNSView(_ view: WKWebView, coordinator: Coordinator) {
        view.configuration.userContentController.removeScriptMessageHandler(forName: "markdownScroll", contentWorld: .defaultClient)
    }

    private static let trackScroll = """
    let pending = false;
    window.addEventListener('scroll', () => {
        if (pending) return;
        pending = true;
        requestAnimationFrame(() => {
            pending = false;
            const blocks = document.getElementById('document')?.children;
            if (!blocks?.length) return;
            let left = 0, right = blocks.length;
            while (left + 1 < right) {
                const mid = (left + right) >> 1;
                if (blocks[mid].getBoundingClientRect().top <= 0) left = mid;
                else right = mid;
            }
            const block = blocks[left];
            window.webkit.messageHandlers.markdownScroll.postMessage({
                blockIndex: left,
                heading: block.firstElementChild?.id || '',
                offset: -block.getBoundingClientRect().top,
                scrollY: window.scrollY
            });
        });
    }, { passive: true });
    """

    final class Coordinator: NSObject, WKNavigationDelegate, WKScriptMessageHandler {
        fileprivate let files = LocalImages()
        private var ready = false
        private var patching = false
        private var loadGeneration = 0
        private var retrying = false
        private var installed: [String] = []
        private var wanted: [String] = []
        private var directory: URL?
        private var needsRefresh = false
        private var jump: HeadingJump?
        private var completed: HeadingJump?
        private var restore: PreviewRestore?
        private var appliedRestore: PreviewRestore?
        private var savedLocation: PreviewLocation?
        private var restoring = false
        private var onLocation: ((PreviewLocation) -> Void)?

        func loadShell(_ view: WKWebView) {
            guard let html = try? PreviewPage.shell() else { return }
            loadGeneration += 1
            ready = false
            patching = false
            restoring = false
            appliedRestore = nil
            installed = []
            completed = nil
            view.loadHTMLString(html, baseURL: nil)
        }

        func update(_ view: WKWebView, blocks: [String], directory: URL?, jump: HeadingJump?,
                    restore: PreviewRestore, recordLocation: @escaping (PreviewLocation) -> Void) {
            if self.restore != restore {
                self.restore = restore
                savedLocation = restore.location
            }
            onLocation = recordLocation
            if wanted != blocks || self.directory != directory { retrying = false }
            wanted = blocks
            if self.directory != directory {
                self.directory = directory
                files.root = directory
                needsRefresh = true
            }
            self.jump = jump
            flush(view)
        }

        func webView(_ view: WKWebView, didFinish navigation: WKNavigation!) {
            ready = true
            flush(view)
        }

        func webViewWebContentProcessDidTerminate(_ view: WKWebView) {
            loadShell(view)
        }

        private func flush(_ view: WKWebView) {
            guard ready, !patching, !restoring else { return }
            if wanted == installed && !needsRefresh {
                if !installed.isEmpty, let restore, appliedRestore != restore {
                    appliedRestore = restore
                    if let savedLocation {
                        restoreScroll(view, location: savedLocation)
                        return
                    }
                }
                if let jump, jump != completed {
                    completed = jump
                    navigate(view, to: jump.id)
                }
                return
            }
            let target = wanted
            var head = 0
            var tail = 0
            if !needsRefresh {
                while head < min(installed.count, target.count) && installed[head] == target[head] { head += 1 }
                while tail < min(installed.count, target.count) - head &&
                    installed[installed.count - 1 - tail] == target[target.count - 1 - tail] { tail += 1 }
            }
            needsRefresh = false
            let markup = PreviewPage.markup(target[head..<(target.count - tail)])
            let generation = loadGeneration
            patching = true
            view.callAsyncJavaScript(
                """
                const page = document.getElementById('document');
                const template = document.createElement('template');
                template.innerHTML = markup;
                const y = window.scrollY;
                for (let i = 0; i < removeCount; ++i) page.children[start].remove();
                page.insertBefore(template.content, page.children[start] || null);
                document.body.classList.toggle('empty', page.childElementCount === 0);
                window.scrollTo(0, y);
                """,
                arguments: ["start": head, "removeCount": installed.count - head - tail, "markup": markup],
                in: nil, in: .defaultClient
            ) { [weak self, weak view] result in
                guard let self, let view, self.loadGeneration == generation else { return }
                self.patching = false
                if case .success = result {
                    self.installed = target
                    self.retrying = false
                    self.flush(view)
                } else if !self.retrying {
                    self.retrying = true
                    self.loadShell(view)
                }
            }
        }

        private func restoreScroll(_ view: WKWebView, location: PreviewLocation) {
            guard location.scrollY.isFinite, location.offset.isFinite else { return }
            let generation = loadGeneration
            restoring = true
            view.callAsyncJavaScript(
                """
                const children = document.getElementById('document').children;
                const named = heading ? document.getElementById(heading)?.closest('.markdown-block') : null;
                const block = named || children[index];
                window.scrollTo(0, block ? window.scrollY + block.getBoundingClientRect().top + offset : y);
                """,
                arguments: ["heading": location.headingID ?? "", "index": max(0, location.blockIndex),
                            "offset": location.offset, "y": max(0, location.scrollY)],
                in: nil, in: .defaultClient
            ) { [weak self, weak view] _ in
                guard let self, let view, self.loadGeneration == generation else { return }
                self.restoring = false
                self.flush(view)
            }
        }

        func userContentController(_ controller: WKUserContentController, didReceive message: WKScriptMessage) {
            guard ready, !restoring, !installed.isEmpty, appliedRestore == restore,
                  let body = message.body as? [String: Any], let index = body["blockIndex"] as? Int,
                  let y = body["scrollY"] as? Double, y.isFinite,
                  let offset = body["offset"] as? Double, offset.isFinite else { return }
            let heading = body["heading"] as? String ?? ""
            let location = PreviewLocation(blockIndex: index, headingID: heading.isEmpty ? nil : heading,
                                           offset: offset, scrollY: y)
            if savedLocation != location {
                savedLocation = location
                onLocation?(location)
            }
        }

        private func navigate(_ view: WKWebView, to id: String) {
            view.callAsyncJavaScript("document.getElementById(id)?.scrollIntoView({block: 'start'});",
                                     arguments: ["id": id], in: nil, in: .defaultClient, completionHandler: nil)
        }

        func webView(_ view: WKWebView, decidePolicyFor action: WKNavigationAction,
                     decisionHandler: @escaping (WKNavigationActionPolicy) -> Void) {
            guard action.navigationType == .linkActivated, let url = action.request.url else {
                decisionHandler(action.request.url?.absoluteString == "about:blank" ? .allow : .cancel)
                return
            }
            decisionHandler(.cancel)

            switch url.scheme {
            case "https", "http", "mailto":
                NSWorkspace.shared.open(url)
            case "md-local":
                if url.path == "/", let fragment = url.fragment {
                    navigate(view, to: fragment.removingPercentEncoding ?? fragment)
                } else if let file = files.localURL(for: url) {
                    let markdown = ["md", "markdown", "mdown", "mkd", "mkdn", "txt"]
                    if markdown.contains(file.pathExtension.lowercased()) {
                        NSDocumentController.shared.openDocument(withContentsOf: file, display: true) { _, _, error in
                            if let error { NSApp.presentError(error) }
                        }
                    } else {
                        NSWorkspace.shared.activateFileViewerSelecting([file])
                    }
                }
            default:
                break
            }
        }
    }
}

final class LocalImages: NSObject, WKURLSchemeHandler {
    var root: URL?
    private var active: Set<ObjectIdentifier> = []
    private let readQueue = DispatchQueue(label: "org.swiftmarkdown.local-images", qos: .userInitiated)

    func localURL(for url: URL) -> URL? {
        guard url.host == "document", let root else { return nil }
        let base = root.resolvingSymlinksInPath().standardizedFileURL
        let relative = url.path.split(separator: "/").joined(separator: "/")
        guard !relative.isEmpty else { return nil }
        let candidate = base.appendingPathComponent(relative).resolvingSymlinksInPath().standardizedFileURL
        guard candidate.path.hasPrefix(base.path == "/" ? "/" : base.path + "/") else { return nil }
        return candidate
    }

    func webView(_ view: WKWebView, start task: WKURLSchemeTask) {
        guard let request = task.request.url, let file = localURL(for: request),
              let type = UTType(filenameExtension: file.pathExtension), type.conforms(to: .image),
              let mime = type.preferredMIMEType else {
            task.didFailWithError(URLError(.fileDoesNotExist))
            return
        }
        let key = ObjectIdentifier(task)
        active.insert(key)
        readQueue.async {
            let limit = 32 * 1024 * 1024
            let data: Data? = try? {
                let attributes = try file.resourceValues(forKeys: [.isRegularFileKey, .fileSizeKey])
                guard attributes.isRegularFile == true, let size = attributes.fileSize, size > 0, size <= limit else {
                    throw URLError(.dataLengthExceedsMaximum)
                }
                let handle = try FileHandle(forReadingFrom: file)
                defer { try? handle.close() }
                guard let contents = try handle.read(upToCount: limit + 1), contents.count <= limit else {
                    throw URLError(.dataLengthExceedsMaximum)
                }
                return contents
            }()
            DispatchQueue.main.async {
                guard self.active.remove(key) != nil else { return }
                guard let data else {
                    task.didFailWithError(URLError(.cannotOpenFile))
                    return
                }
                task.didReceive(URLResponse(url: request, mimeType: mime, expectedContentLength: data.count,
                                            textEncodingName: nil))
                task.didReceive(data)
                task.didFinish()
            }
        }
    }

    func webView(_ view: WKWebView, stop task: WKURLSchemeTask) {
        active.remove(ObjectIdentifier(task))
    }
}
