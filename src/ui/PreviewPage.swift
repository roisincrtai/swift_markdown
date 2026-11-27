import Foundation
import WebKit

enum PreviewPage {
    static func configuration(files: LocalImages) -> WKWebViewConfiguration {
        let config = WKWebViewConfiguration()
        config.defaultWebpagePreferences.allowsContentJavaScript = false
        config.websiteDataStore = .nonPersistent()
        config.setURLSchemeHandler(files, forURLScheme: "md-local")
        return config
    }

    static func shell() throws -> String {
        guard let page = Bundle.main.url(forResource: "Preview", withExtension: "html") else {
            throw CocoaError(.fileNoSuchFile)
        }
        return try String(contentsOf: page, encoding: .utf8)
    }

    static func markup(_ blocks: ArraySlice<String>) -> String {
        blocks.map { "<div class=\"markdown-block\">\($0)</div>" }.joined()
    }

    static func printPage(blocks: [String]) throws -> String {
        guard let styleURL = Bundle.main.url(forResource: "Print", withExtension: "css") else {
            throw CocoaError(.fileNoSuchFile)
        }
        let css = try String(contentsOf: styleURL, encoding: .utf8)
        return try PDFPage.html(shell: shell(), style: css, article: markup(blocks[...]))
    }
}
