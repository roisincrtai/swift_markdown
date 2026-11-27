import CoreGraphics
import Foundation

@main
struct PDFPageTests {
    static func main() throws {
        let shell = #"<html><head></head><body class="empty"><main id="document" aria-label="Markdown preview"></main></body></html>"#
        let article = #"<p>Inline <math><mi>x</mi></math></p><div><math display="block"><mfrac><mn>1</mn><mn>2</mn></mfrac></math></div><img loading="lazy" src="figure.png">"#
        let html = try PDFPage.html(shell: shell, style: "math { break-inside: avoid; }", article: article)
        precondition(html.contains(#"class="print-preview""#))
        precondition(html.contains(#"<math display="block"><mfrac>"#))
        precondition(html.contains(#"<img src="figure.png">"#))
        precondition(html.contains("<style>math { break-inside: avoid; }</style>"))
        precondition((try? PDFPage.html(shell: "", style: "", article: "")) == nil)

        let slices = try PDFPagination.plan(height: 1_940, pageHeight: 753, breakpoints: [620, 680, 1_410, 1_890])
        precondition(slices == [
            PDFSlice(start: 0, end: 680),
            PDFSlice(start: 680, end: 1_410),
            PDFSlice(start: 1_410, end: 1_940),
        ])
        let long = try PDFPagination.plan(height: 1_650, pageHeight: 753, breakpoints: [0, .nan, 3000])
        precondition(long.map(\.height) == [753, 753, 144])
        let empty = try PDFPagination.plan(height: 0, pageHeight: 753, breakpoints: [])
        precondition(empty == [PDFSlice(start: 0, end: 1)])
        precondition((try? PDFPagination.plan(height: .infinity, pageHeight: 753, breakpoints: [])) == nil)

        let destination = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString + ".pdf")
        defer { try? FileManager.default.removeItem(at: destination) }
        let source = try makeVectorPage()
        let writer = try PDFDocumentWriter(title: "Formula Notes")
        for slice in slices { try writer.append(source, usedHeight: slice.height) }
        try writer.save(to: destination, pageCount: slices.count)
        guard let pdf = CGPDFDocument(destination as CFURL), let page = pdf.page(at: 1) else {
            throw CocoaError(.fileReadCorruptFile)
        }
        let bounds = page.getBoxRect(.mediaBox)
        precondition(pdf.numberOfPages == slices.count)
        precondition(abs(bounds.width - PDFDocumentWriter.paper.width) < 0.02)
        precondition(abs(bounds.height - PDFDocumentWriter.paper.height) < 0.02)
    }

    private static func makeVectorPage() throws -> Data {
        let data = NSMutableData()
        var bounds = CGRect(x: 0, y: 0, width: 507, height: 753)
        guard let consumer = CGDataConsumer(data: data), let context = CGContext(consumer: consumer, mediaBox: &bounds, nil) else {
            throw CocoaError(.fileWriteUnknown)
        }
        context.beginPDFPage(nil)
        context.setFillColor(red: 0.2, green: 0.3, blue: 0.6, alpha: 1)
        context.fill(CGRect(x: 12, y: 600, width: 90, height: 90))
        context.endPDFPage()
        context.closePDF()
        return data as Data
    }
}
