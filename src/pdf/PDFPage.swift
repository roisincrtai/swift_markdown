import Foundation

enum PDFPage {
    private static let main = #"<main id="document" aria-label="Markdown preview"></main>"#

    static func html(shell: String, style: String, article: String) throws -> String {
        guard shell.contains(main), shell.contains("</head>"), shell.contains(#"<body class="empty">"#) else {
            throw CocoaError(.fileReadCorruptFile)
        }
        let eagerArticle = article.replacingOccurrences(of: #" loading="lazy" src="#, with: #" src="#)
        return shell
            .replacingOccurrences(of: #"<body class="empty">"#, with: #"<body class="print-preview">"#)
            .replacingOccurrences(of: "</head>", with: "<style>\(style)</style></head>")
            .replacingOccurrences(of: main, with: #"<main id="document">\#(eagerArticle)</main>"#)
    }
}
