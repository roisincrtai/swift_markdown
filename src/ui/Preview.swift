import AppKit
import SwiftUI
import UniformTypeIdentifiers
import WebKit

struct WebPreview: NSViewRepresentable {
    let blocks: [String]
    let rootDirectory: URL?
    let jump: HeadingJump?

    func makeCoordinator() -> Coordinator { Coordinator() }

    func makeNSView(context: Context) -> WKWebView {
        let config = WKWebViewConfiguration()
        config.defaultWebpagePreferences.allowsContentJavaScript = false
        config.websiteDataStore = .nonPersistent()
        config.setURLSchemeHandler(context.coordinator.files, forURLScheme: "md-local")

        let view = WKWebView(frame: .zero, configuration: config)
        view.navigationDelegate = context.coordinator
        context.coordinator.loadShell(view)
        return view
    }

    func updateNSView(_ view: WKWebView, context: Context) {
        context.coordinator.update(view, blocks: blocks, directory: rootDirectory, jump: jump)
    }

    final class Coordinator: NSObject, WKNavigationDelegate {
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

        func loadShell(_ view: WKWebView) {
            guard let page = Bundle.main.url(forResource: "Preview", withExtension: "html"),
                  let html = try? String(contentsOf: page, encoding: .utf8) else { return }
            loadGeneration += 1
            ready = false
            patching = false
            installed = []
            completed = nil
            view.loadHTMLString(html, baseURL: nil)
        }

        func update(_ view: WKWebView, blocks: [String], directory: URL?, jump: HeadingJump?) {
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
            guard ready, !patching else { return }
            if wanted == installed && !needsRefresh {
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
            let markup = target[head..<(target.count - tail)].map { "<div class=\"markdown-block\">\($0)</div>" }.joined()
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
                    let markdown = ["md", "markdown", "mdown", "mkd", "mkdn"]
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

private final class LocalImages: NSObject, WKURLSchemeHandler {
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
