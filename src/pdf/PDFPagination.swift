import Foundation

struct PDFSlice: Equatable {
    let start: Double
    let end: Double
    var height: Double { end - start }
}

enum PDFPagination {
    static func plan(height: Double, pageHeight: Double, breakpoints: [Double]) throws -> [PDFSlice] {
        guard height.isFinite, pageHeight.isFinite, height >= 0, pageHeight >= 1 else {
            throw CocoaError(.fileWriteUnknown)
        }
        let total = max(1, ceil(height))
        let cuts = breakpoints.filter { $0.isFinite && $0 > 0 && $0 < total }.sorted()
        var pages: [PDFSlice] = []
        var cursor = 0.0
        var nextCut = 0
        while cursor < total {
            guard pages.count < 2_000 else { throw CocoaError(.fileWriteOutOfSpace) }
            let limit = min(cursor + pageHeight, total)
            while nextCut < cuts.count && cuts[nextCut] <= limit { nextCut += 1 }
            let candidate = nextCut > 0 ? cuts[nextCut - 1] : 0
            let end = limit < total && candidate > cursor + 1 ? candidate : limit
            pages.append(PDFSlice(start: cursor, end: end))
            cursor = end
        }
        return pages
    }
}
