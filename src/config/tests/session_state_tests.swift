import CoreGraphics
import Foundation

@main
struct SessionStateTests {
    static func main() throws {
        let sample = URL(fileURLWithPath: "/tmp/Notebook: α #1.md")
        let reference = FileReference(sample, bookmark: Data([0, 27, 194, 60]))
        var expected = UserSessionState()
        expected.lastOpened = reference
        expected.recentFiles = [reference, FileReference(URL(fileURLWithPath: "/tmp/Readme.txt"))]
        expected.directory = FileReference(URL(fileURLWithPath: "/tmp/Research Notes", isDirectory: true))
        expected.window = WindowPlacement(CGRect(x: -507, y: 180, width: 1080, height: 730))
        expected.views[reference.path] = DocumentViewState(
            window: expected.window,
            editor: EditorLocation(location: 190, length: 4, scrollY: 87),
            preview: PreviewLocation(blockIndex: 27, headingID: "formulas-and-notes", offset: -14, scrollY: 1985))
        let encoded = try SessionYAML.encode(expected)
        let decoded = try SessionYAML.decode(encoded)
        guard decoded == expected else {
            fatalError("The session settings did not round-trip.")
        }
        let defaults = try SessionYAML.decode(Data("{}".utf8))
        precondition(defaults == UserSessionState())
    }
}
