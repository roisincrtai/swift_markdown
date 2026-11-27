# Swift Markdown

Swift Markdown is a native macOS viewer and editor. The outline and preview open together. Use **⌥⌘E** or the toolbar button to show the editable Markdown source. Select a heading to jump to it, open documents with **⌘O**, and save changes with **⌘S**.

## Build and run

The default preset targets macOS 14 and later. It uses CMake 3.25 or later and the Xcode toolchain. The app uses the C++ standard library and Apple frameworks and runs offline.

    cmake --preset macos
    cmake --build --preset macos
    ctest --preset macos
    open "build/unsigned/Swift Markdown.app"

The macos-ninja preset uses the same Apple toolchain with the Ninja generator. Its intermediates live in build/native. The Xcode preset uses build/artifacts. The app bundle is in build/unsigned/ in either case.

The C++ core and formula suite can also be built on their own.

    cmake --preset core
    cmake --build --preset core
    ctest --preset core

## Sign and package

    cmake --build --preset macos --target sign
    cmake --build --preset macos --target deploy

Both targets verify the signed app and place it in build/dist/. Deploy also creates a DMG alongside it. The script selects a local signing identity or uses an ad-hoc signature. Set -DCODE_SIGN_IDENTITY=- to force ad-hoc signing.

## App icon

Sigma Sheet is the selected logo at [src/logo/logo.svg](src/logo/logo.svg). CMake bundles the original SVG and preserves the vector representation in the macOS icon catalog. The [candidate sheet](src/logo/logo-candidates.svg) shows the original 20 vector designs.

## Markdown and TeX

The C++ Markdown parser supports headings, lists, task lists, blockquotes, code, tables, links, images, and formulas. The preview loads local images beside the document or in its subdirectories. Clicked web links open in the system browser.

Use $...$ or \( ... \) for inline math and $$...$$ or \[ ... \] for display math. Display environments such as equation, align, gather, and multline also work as Markdown blocks. The C++ engine in src/tex tokenizes and expands TeX, parses its layout, and emits presentation MathML for native WebKit typesetting. It handles scripts, fractions, roots, Greek letters and symbols, delimiters, accents, matrices, arrays, cases, alignment, explicit equation tags, math fonts, and formula-local macros. Unsupported commands show a diagnostic in the preview. TeX document preambles, external packages, automatic numbering, and cross-document labels are outside the supported math syntax.

While you type, Swift schedules the newest edit off the main thread. The C++ renderer reuses cached inline markup and formula layouts. The preview replaces only the changed range of rendered blocks and keeps its scroll position.

The formula suite in src/tex/tests/ covers inline and display output, nested layout, environments, macro expansion, malformed input, and input limits. The Markdown suite in src/core/tests/ checks math delimiters and rendering before and after a source edit.
