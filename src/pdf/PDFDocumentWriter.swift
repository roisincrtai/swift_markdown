import CoreGraphics
import Foundation

final class PDFDocumentWriter {
    static let paper = CGRect(x: 0, y: 0, width: 595.28, height: 841.89)
    static let margin = 44.0
    static let contentWidth = floor(paper.width - 2 * margin)
    static let contentHeight = floor(paper.height - 2 * margin)

    private let folder: URL
    private let file: URL
    private var context: CGContext?

    init(title: String) throws {
        folder = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString, isDirectory: true)
        file = folder.appendingPathComponent("document.pdf")
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: false)
        var paper = Self.paper
        guard let context = CGContext(file as CFURL, mediaBox: &paper, [kCGPDFContextTitle: title] as CFDictionary) else {
            try? FileManager.default.removeItem(at: folder)
            throw CocoaError(.fileWriteUnknown)
        }
        self.context = context
    }

    deinit {
        context?.closePDF()
        try? FileManager.default.removeItem(at: folder)
    }

    func append(_ data: Data, usedHeight: Double) throws {
        guard let context, let provider = CGDataProvider(data: data as CFData),
              let document = CGPDFDocument(provider), let page = document.page(at: 1) else {
            throw CocoaError(.fileReadCorruptFile)
        }

        let target = CGRect(x: Self.margin, y: Self.paper.height - Self.margin - usedHeight,
                            width: Self.contentWidth, height: usedHeight)
        context.beginPDFPage(nil)
        context.saveGState()
        context.clip(to: target)
        context.concatenate(page.getDrawingTransform(.mediaBox, rect: target, rotate: 0, preserveAspectRatio: true))
        context.drawPDFPage(page)
        context.restoreGState()
        context.endPDFPage()
    }

    func save(to destination: URL, pageCount: Int) throws {
        context?.closePDF()
        context = nil
        guard let document = CGPDFDocument(file as CFURL), document.numberOfPages == pageCount else {
            throw CocoaError(.fileWriteUnknown)
        }
        try Data(contentsOf: file, options: .mappedIfSafe).write(to: destination, options: .atomic)
    }
}
