import AppKit

@main
struct SwiftMarkdownApp {
    static func main() {
        let application = NSApplication.shared
        let documents = MarkdownDocumentController()
        let delegate = ApplicationDelegate(documents: documents)
        application.delegate = delegate
        application.setActivationPolicy(.regular)
        ApplicationMenus.install(on: application)
        withExtendedLifetime(delegate) { application.run() }
    }
}

final class ApplicationDelegate: NSObject, NSApplicationDelegate {
    private let documents: MarkdownDocumentController
    private var attemptedRestore = false

    init(documents: MarkdownDocumentController) {
        self.documents = documents
        super.init()
    }
    func applicationDidFinishLaunching(_ notification: Notification) {
        let application = NSApplication.shared
        if let logo = Bundle.main.url(forResource: "logo", withExtension: "svg"),
           let icon = NSImage(contentsOf: logo) {
            application.applicationIconImage = icon
        }
        application.activate(ignoringOtherApps: true)
    }

    func applicationOpenUntitledFile(_ sender: NSApplication) -> Bool {
        if !attemptedRestore {
            attemptedRestore = true
            if let previous = SessionStore.shared.state.lastOpened {
                documents.openRecent(previous) { [weak self] document, _, _ in
                    if document == nil { self?.showWelcomeDocument() }
                }
                return true
            }
        }
        return showWelcomeDocument()
    }

    func applicationWillTerminate(_ notification: Notification) {
        SessionStore.shared.flush()
    }

    @discardableResult
    private func showWelcomeDocument() -> Bool {
        let type = documents.defaultType ?? "Markdown"
        do {
            let document = try documents.makeUntitledDocument(ofType: type)
            (document as? MarkdownDocument)?.loadExample()
            documents.addDocument(document)
            document.makeWindowControllers()
            document.showWindows()
            return true
        } catch {
            return false
        }
    }
}

private enum ApplicationMenus {
    static func install(on application: NSApplication) {
        let bar = NSMenu()
        application.mainMenu = bar

        func submenu(_ title: String) -> NSMenu {
            let item = NSMenuItem()
            let menu = NSMenu(title: title)
            item.submenu = menu
            bar.addItem(item)
            return menu
        }

        func add(_ menu: NSMenu, _ title: String, _ selector: String, _ key: String = "",
                 modifiers: NSEvent.ModifierFlags = .command) {
            let item = NSMenuItem(title: title, action: NSSelectorFromString(selector), keyEquivalent: key)
            item.keyEquivalentModifierMask = modifiers
            menu.addItem(item)
        }

        let app = submenu("Swift Markdown")
        add(app, "About Swift Markdown", "orderFrontStandardAboutPanel:")
        app.addItem(.separator())
        add(app, "Hide Swift Markdown", "hide:", "h")
        add(app, "Hide Others", "hideOtherApplications:", "h", modifiers: [.command, .option])
        add(app, "Show All", "unhideAllApplications:")
        app.addItem(.separator())
        add(app, "Quit Swift Markdown", "terminate:", "q")

        let file = submenu("File")
        add(file, "New", "newDocument:", "n")
        add(file, "Open…", "openDocument:", "o")
        file.addItem(.separator())
        add(file, "Close", "performClose:", "w")
        add(file, "Save", "saveDocument:", "s")
        add(file, "Save As…", "saveDocumentAs:", "s", modifiers: [.command, .shift])
        add(file, "Revert to Saved", "revertDocumentToSaved:")
        file.addItem(.separator())
        add(file, "Export as PDF…", "exportMarkdownPDF:", "e", modifiers: [.command, .shift])

        let edit = submenu("Edit")
        add(edit, "Undo", "undo:", "z")
        add(edit, "Redo", "redo:", "z", modifiers: [.command, .shift])
        edit.addItem(.separator())
        add(edit, "Cut", "cut:", "x")
        add(edit, "Copy", "copy:", "c")
        add(edit, "Paste", "paste:", "v")
        add(edit, "Select All", "selectAll:", "a")

        let view = submenu("View")
        add(view, "Show Markdown Source", "toggleMarkdownSource:", "e", modifiers: [.command, .option])
        add(view, "Enter Full Screen", "toggleFullScreen:", "f", modifiers: [.command, .control])

        let window = submenu("Window")
        add(window, "Minimize", "performMiniaturize:", "m")
        add(window, "Zoom", "performZoom:")
        window.addItem(.separator())
        add(window, "Bring All to Front", "arrangeInFront:")
        application.windowsMenu = window
    }
}
