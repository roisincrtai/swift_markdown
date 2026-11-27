#include "markdown.hpp"

#include <iostream>

bool has(const std::string& value, std::string_view part) {
    return value.find(part) != value.npos;
}

int main() {
    markdown::Renderer renderer;
    const std::string source = R"(# Notebook
## Section *one*
$$
\begin{pmatrix}a & b \\ c & d\end{pmatrix}
$$

The result is $\frac{1}{n}$ and \(x^2\).

~~~tex
$x$ and <a>
~~~

[Guide][ref]

[ref]: https://example.com
)";
    auto first = renderer.render(source);
    auto edit = source;
    edit[edit.find("{1}{n}") + 1] = '2';
    auto second = renderer.render(edit);
    auto third = renderer.render("[Guide][ref]\n\n[ref]: notes.md\n");
    const auto more = renderer.render(R"(# Math

\[\sqrt[3]{x}\]

\begin{align*}
\sum_{n=1}^\infty \frac{1}{n^2}&=\frac{\pi^2}{6}
\end{align*}

| Formula | Unit |
| --- | --- |
| $\left|x\right|$ | none |

Inline $\text{if $x>0$}$ stays together. The price is $5 and $10.

Malformed $\frac{1}$ is still visible.
)");
    const bool success =
        first.outline.size() == 2 && first.outline[1].id == "section-one" &&
        first.outline[1].title == "Section one" && first.outline[1].line == 2 &&
        first.blocks.size() == 6 && second.blocks.size() == 6 &&
        first.blocks[2] == second.blocks[2] && has(first.blocks[2], "<mtable") &&
        first.blocks[3] != second.blocks[3] && has(second.blocks[3], "<mn>2</mn>") &&
        has(second.blocks[3], "<msup>") && !has(second.blocks[3], "display=\"block\"") &&
        has(first.blocks[4], "$x$ and &lt;a&gt;") && has(first.blocks[5], "https://example.com") &&
        third.blocks.size() == 1 && has(third.blocks[0], "notes.md") &&
        more.blocks.size() == 6 && has(more.blocks[1], "<mroot>") && has(more.blocks[2], "<mtable") &&
        has(more.blocks[3], "fence=\"true\"") && has(more.blocks[4], "<mtext") &&
        has(more.blocks[4], "$5 and $10") && has(more.blocks[5], "tex-error");
    if (!success) {
        std::cerr << "Markdown headings, formula refresh, code, or reference resolution failed\n";
        for (const auto& block : more.blocks) std::cerr << block << '\n';
    }
    return success ? 0 : 1;
}
