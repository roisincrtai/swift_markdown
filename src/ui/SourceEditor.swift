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
        scroll.contentView.postsBoundsChangedNotifications = true
        context.coordinator.restore(view, scroll: scroll, location: document?.viewState.editor)
        context.coordinator.observe(scroll)
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

    static func dismantleNSView(_ scroll: NSScrollView, coordinator: Coordinator) {
        coordinator.stopObserving()
    }

    final class Coordinator: NSObject, NSTextViewDelegate {
        var owner: SourceEditor
        private var observation: NSObjectProtocol?
        private var restoring = false

        init(_ owner: SourceEditor) { self.owner = owner }

        func textDidChange(_ notification: Notification) {
            guard let view = notification.object as? NSTextView else { return }
            owner.source = view.string
        }

        func textViewDidChangeSelection(_ notification: Notification) {
            if let view = notification.object as? NSTextView { record(view) }
        }

        func restore(_ view: NSTextView, scroll: NSScrollView, location: EditorLocation?) {
            guard let location else { return }
            restoring = true
            let length = (view.string as NSString).length
            let start = min(length, max(0, location.location))
            view.setSelectedRange(NSRange(location: start, length: min(length - start, max(0, location.length))))
            DispatchQueue.main.async { [weak self, weak view, weak scroll] in
                guard let self else { return }
                defer { self.restoring = false }
                guard let view, let scroll else { return }
                view.scrollRangeToVisible(view.selectedRange())
                if location.scrollY.isFinite {
                    let height = max(0, view.frame.height - scroll.contentView.bounds.height)
                    let origin = NSPoint(x: 0, y: min(height, max(0, location.scrollY)))
                    scroll.contentView.scroll(to: origin)
                    scroll.reflectScrolledClipView(scroll.contentView)
                }
            }
        }

        func observe(_ scroll: NSScrollView) {
            observation = NotificationCenter.default.addObserver(
                forName: NSView.boundsDidChangeNotification, object: scroll.contentView, queue: .main
            ) { [weak self, weak scroll] _ in
                if let view = scroll?.documentView as? NSTextView { self?.record(view) }
            }
        }

        func stopObserving() {
            if let observation { NotificationCenter.default.removeObserver(observation) }
            observation = nil
        }

        private func record(_ view: NSTextView) {
            guard !restoring, let scroll = view.enclosingScrollView else { return }
            let selection = view.selectedRange()
            owner.document?.recordEditor(EditorLocation(location: selection.location, length: selection.length,
                                                        scrollY: scroll.contentView.bounds.minY))
        }

        func undoManager(for view: NSTextView) -> UndoManager? { owner.document?.undoManager }
    }
}
