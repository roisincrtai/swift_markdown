import AppKit
import SwiftUI

struct SourceEditor: NSViewRepresentable {
    @Binding var source: String
    weak var document: MarkdownDocument?

    func makeCoordinator() -> Coordinator { Coordinator(self) }

    func makeNSView(context: Context) -> NSScrollView {
        let scroll = NSTextView.scrollableTextView()
        guard let view = scroll.documentView as? NSTextView else { return scroll }
        view.delegate = context.coordinator
        view.isRichText = false
        view.importsGraphics = false
        view.allowsUndo = true
        view.isAutomaticQuoteSubstitutionEnabled = false
        view.isAutomaticDashSubstitutionEnabled = false
        view.isAutomaticTextReplacementEnabled = false
        view.isAutomaticSpellingCorrectionEnabled = false
        view.isContinuousSpellCheckingEnabled = false
        view.textContainerInset = NSSize(width: 14, height: 14)
        view.font = .monospacedSystemFont(ofSize: 13, weight: .regular)
        view.string = source
        view.setAccessibilityLabel("Markdown Source")
        scroll.borderType = .noBorder
        return scroll
    }

    func updateNSView(_ scroll: NSScrollView, context: Context) {
        context.coordinator.owner = self
        guard let view = scroll.documentView as? NSTextView, view.string != source else { return }
        let selection = view.selectedRange()
        view.string = source
        let length = (source as NSString).length
        view.setSelectedRange(NSRange(location: min(length, selection.location),
                                      length: selection.location <= length ? min(length - selection.location, selection.length) : 0))
    }

    final class Coordinator: NSObject, NSTextViewDelegate {
        var owner: SourceEditor

        init(_ owner: SourceEditor) { self.owner = owner }

        func textDidChange(_ notification: Notification) {
            guard let view = notification.object as? NSTextView else { return }
            owner.source = view.string
        }

        func undoManager(for view: NSTextView) -> UndoManager? { owner.document?.undoManager }
    }
}
