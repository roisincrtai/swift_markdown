import CoreGraphics
import Foundation
import YAMLConfigBridge

struct FileReference: Codable, Equatable, Identifiable {
    var path: String
    var bookmark: Data?
    var id: String { path }
    var url: URL { URL(fileURLWithPath: path) }

    init(_ url: URL, bookmark: Data? = nil) {
        path = url.standardizedFileURL.path
        self.bookmark = bookmark
    }
}

struct WindowPlacement: Codable, Equatable {
    var x: Double
    var y: Double
    var width: Double
    var height: Double

    init(_ frame: CGRect) {
        x = frame.minX
        y = frame.minY
        width = frame.width
        height = frame.height
    }

    var frame: CGRect? {
        guard [x, y, width, height].allSatisfy(\.isFinite),
              width > 0, height > 0, width <= 30_000, height <= 30_000 else { return nil }
        return CGRect(x: x, y: y, width: width, height: height)
    }
}

struct EditorLocation: Codable, Equatable {
    var location: Int
    var length: Int
    var scrollY: Double
}

struct PreviewLocation: Codable, Equatable {
    var blockIndex: Int
    var headingID: String?
    var offset: Double
    var scrollY: Double

    private enum CodingKeys: String, CodingKey {
        case blockIndex, offset, scrollY
        case headingID = "headingId"
    }
}

struct DocumentViewState: Codable, Equatable {
    var window: WindowPlacement?
    var editor: EditorLocation?
    var preview: PreviewLocation?
}

struct UserSessionState: Codable, Equatable {
    var window: WindowPlacement?
    var directory: FileReference?
    var lastOpened: FileReference?
    var recentFiles = [FileReference]()
    var views = [String: DocumentViewState]()

    private enum CodingKeys: String, CodingKey {
        case window, directory, lastOpened, recentFiles, views
    }

    init() {}

    init(from decoder: Decoder) throws {
        let values = try decoder.container(keyedBy: CodingKeys.self)
        window = try values.decodeIfPresent(WindowPlacement.self, forKey: .window)
        directory = try values.decodeIfPresent(FileReference.self, forKey: .directory)
        lastOpened = try values.decodeIfPresent(FileReference.self, forKey: .lastOpened)
        recentFiles = try values.decodeIfPresent([FileReference].self, forKey: .recentFiles) ?? []
        views = try values.decodeIfPresent([String: DocumentViewState].self, forKey: .views) ?? [:]
    }
}

enum SessionYAML {
    static func decode(_ data: Data) throws -> UserSessionState {
        guard !data.isEmpty, let json = data.withUnsafeBytes({ bytes in
            md_yaml_to_json(bytes.bindMemory(to: CChar.self).baseAddress, bytes.count)
        }) else { throw CocoaError(.fileReadCorruptFile) }
        defer { md_yaml_free(json) }
        let decoder = JSONDecoder()
        decoder.keyDecodingStrategy = .convertFromSnakeCase
        return try decoder.decode(UserSessionState.self, from: Data(bytes: json, count: strlen(json)))
    }

    static func encode(_ state: UserSessionState) throws -> Data {
        let encoder = JSONEncoder()
        encoder.keyEncodingStrategy = .convertToSnakeCase
        let object = try JSONSerialization.jsonObject(with: encoder.encode(state))
        var lines = ["# Swift Markdown session", "---"]
        try yaml(object, indent: "", into: &lines)
        return Data((lines.joined(separator: "\n") + "\n").utf8)
    }

    private static func collection(_ value: Any) -> Bool {
        if let map = value as? [String: Any] { return !map.isEmpty }
        if let array = value as? [Any] { return !array.isEmpty }
        return false
    }

    private static func yaml(_ value: Any, indent: String, into lines: inout [String]) throws {
        if let map = value as? [String: Any], !map.isEmpty {
            for (key, value) in map.sorted(by: { $0.key < $1.key }) {
                try append(value, prefix: indent + (try scalar(key)) + ":", indent: indent, into: &lines)
            }
        } else if let array = value as? [Any], !array.isEmpty {
            for item in array {
                try append(item, prefix: indent + "-", indent: indent, into: &lines)
            }
        } else {
            lines.append(indent + (try scalar(value)))
        }
    }

    private static func append(_ value: Any, prefix: String, indent: String, into lines: inout [String]) throws {
        if collection(value) {
            lines.append(prefix)
            try yaml(value, indent: indent + "  ", into: &lines)
        } else {
            lines.append(prefix + " " + (try scalar(value)))
        }
    }

    private static func scalar(_ value: Any) throws -> String {
        let data = try JSONSerialization.data(withJSONObject: value, options: [.fragmentsAllowed, .withoutEscapingSlashes])
        return String(decoding: data, as: UTF8.self)
    }
}
