import AppKit

@MainActor
final class MarkdownDocumentController: NSDocumentController {
    override func openDocument(withContentsOf url: URL, display displayDocument: Bool,
                               completionHandler: @escaping (NSDocument?, Bool, (any Error)?) -> Void) {
        open(FileAccess(url), previousPath: nil, display: displayDocument, completion: completionHandler)
    }

    func openRecent(_ reference: FileReference, completion: ((NSDocument?, Bool, (any Error)?) -> Void)? = nil) {
        open(FileAccess(reference), previousPath: reference.path, display: true, completion: completion)
    }

    private func open(_ access: FileAccess, previousPath: String?, display: Bool,
                      completion: ((NSDocument?, Bool, (any Error)?) -> Void)?) {
        let session = SessionStore.shared
        let browser = session.directoryAccess
        let inherited = browser.contains(access.url) ? browser : nil
        super.openDocument(withContentsOf: access.url, display: display) { document, alreadyOpen, error in
            if let document = document as? MarkdownDocument {
                let reference = access.reference()
                document.keepAccess(access, inherited: inherited)
                if let previousPath, previousPath != reference.path {
                    document.restoreMovedFile(from: previousPath)
                }
                session.noteOpened(reference, previousPath: previousPath)
            }
            if let completion { completion(document, alreadyOpen, error) }
            else if let error { NSApp.presentError(error) }
        }
    }
}
