import Foundation

guard CommandLine.arguments.count == 3 else { exit(2) }
let directory = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
let output = URL(fileURLWithPath: CommandLine.arguments[2])
let slots = [
    ("icp4", "icon_16x16.png"), ("icp5", "icon_32x32.png"), ("icp6", "icon_32x32@2x.png"),
    ("ic07", "icon_128x128.png"), ("ic08", "icon_256x256.png"), ("ic09", "icon_512x512.png"),
    ("ic10", "icon_512x512@2x.png"), ("ic11", "icon_16x16@2x.png"), ("ic12", "icon_32x32@2x.png"),
    ("ic13", "icon_128x128@2x.png"), ("ic14", "icon_256x256@2x.png")
]

func header(_ type: String, _ size: Int) -> Data {
    var data = Data(type.utf8)
    var length = UInt32(size + 8).bigEndian
    data.append(withUnsafeBytes(of: &length) { Data($0) })
    return data
}

var entries = Data()
for (type, name) in slots {
    let png = try Data(contentsOf: directory.appendingPathComponent(name))
    guard png.starts(with: [137, 80, 78, 71, 13, 10, 26, 10]) else {
        throw CocoaError(.fileReadCorruptFile)
    }
    entries.append(header(type, png.count))
    entries.append(png)
}
try (header("icns", entries.count) + entries).write(to: output, options: .atomic)
