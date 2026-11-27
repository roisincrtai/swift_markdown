#include "tex.hpp"

#include <chrono>
#include <initializer_list>
#include <iostream>
#include <random>
#include <string>
#include <string_view>

namespace {

int failures = 0;
int cases = 0;

void check(bool condition, std::string_view input, std::string_view detail) {
    if (condition) return;
    ++failures;
    std::cerr << "FAIL " << input << " — " << detail << '\n';
}

void accepts(std::string_view source, std::initializer_list<std::string_view> expected,
             bool display = false) {
    ++cases;
    auto result = tex::render(source, display);
    check(result.error.empty(), source, result.error);
    check(result.mathml.starts_with("<math ") && result.mathml.ends_with("</math>"),
          source, "expected a complete MathML tree");
    auto start = result.mathml.substr(0, result.mathml.find('>'));
    check((start.find("display=\"block\"") != start.npos) == display, source, "wrong math mode");
    auto visible = std::string_view(result.mathml).substr(0, result.mathml.find("<annotation"));
    for (auto part : expected) check(visible.find(part) != visible.npos, source, part);
}

void rejects(std::string_view source, std::string_view diagnostic) {
    ++cases;
    auto result = tex::render(source);
    check(result.mathml.empty(), source, "invalid formula should not emit incomplete markup");
    check(result.error.find(diagnostic) != std::string::npos, source, result.error);
}

void arithmetic() {
    accepts("", {"<mrow></mrow>"});
    accepts("E = mc^2", {"<mi>E</mi>", "<msup><mi>c</mi><mn>2</mn></msup>"});
    accepts("x_2^3", {"<msubsup><mi>x</mi><mn>2</mn><mn>3</mn></msubsup>"});
    accepts("x^2_3", {"<msubsup><mi>x</mi><mn>3</mn><mn>2</mn></msubsup>"});
    accepts("^4_9 X", {"<msubsup><mrow></mrow><mn>9</mn><mn>4</mn></msubsup>"});
    accepts("f''_i(x)", {"′′", "<msubsup>"});
    accepts("3.14159-12", {"<mn>3.14159</mn>", "−", "<mn>12</mn>"});
    accepts("e^23", {"<msup><mi>e</mi><mn>2</mn></msup><mn>3</mn>"});
    accepts(R"(\frac{1+\sqrt{x^2}}{\sqrt[3]{y}})", {"<mfrac>", "<msqrt>", "<mroot>", "<msup>"});
    accepts(R"({a\over b}+{n\choose k})", {"<mfrac>", "linethickness=\"0\""});
    accepts(R"(\frac12)", {"<mfrac><mn>1</mn><mn>2</mn></mfrac>"});
    accepts(R"(\cfrac[l]{1}{1+\cfrac{1}{x}})", {"<mstyle displaystyle=\"true\"", "<mfrac>"});
    accepts(R"(\binom{n}{k}\tfrac{1}{2}\dfrac{1}{x})", {"linethickness=\"0\"", "displaystyle=\"false\"", "displaystyle=\"true\""});
    accepts(R"(\genfrac{[}{]}{0pt}{2}{n}{k})", {"linethickness=\"0pt\"", "scriptlevel=\"1\""});
}

void symbolsAndLayout() {
    accepts(R"(\alpha+\Gamma\leq\infty\land\neg\emptyset)", {"α", "Γ", "≤", "∞", "∧", "¬", "∅"});
    accepts(R"(\lim_{x\to0}\frac{\sin x}{x}=1)", {"<munder>", "→", ">sin</mo>"});
    accepts(R"(\sum_{k=0}^{n}k^2)", {"<munderover>", "∑", "movablelimits=\"true\""}, true);
    accepts(R"(\sum\limits_i^n i)", {"<munderover>", "movablelimits=\"false\""}, false);
    accepts(R"(\int_0^\infty e^{-x}\,\mathrm{d}x)", {"<msubsup>", "∫", "width=\"0.1667em\"", "mathvariant=\"normal\">d"});
    accepts(R"(\int\limits_0^1 x\,dx+\sum\nolimits_i)", {"<munderover>", "<msub>"});
    accepts(R"(\left\langle \frac xy \middle| \vec v \right\rangle)", {"stretchy=\"true\"", "⟨", "form=\"infix\"", "⟩"});
    accepts(R"(\left.\frac{dy}{dx}\right|_{x=0})", {"<msub>", "form=\"postfix\"", "|</mo>"});
    accepts(R"(\bigl( x \Biggr])", {"minsize=\"1.4em\"", "minsize=\"2.6em\""});
    accepts(R"(\widehat{xyz}+\overbrace{a+b}^{2}+\underline{x})", {"accent=\"true\"", "⏞", "accentunder=\"true\""});
    accepts(R"(A\xrightarrow[\text{below}]{\text{above}}B)", {"<munderover>", "⟶", ">below</mtext>", ">above</mtext>"});
    accepts(R"(\overset{!}{=}\underset{0}{\to})", {"<mover>", "<munder>"});
    accepts(R"(\boxed{\cancel{x}+\phantom{y}})", {"notation=\"box\"", "updiagonalstrike", "<mphantom>"});
    accepts(R"(\mathbb{CHNPQRZ}\quad\mathcal{B}\mathbf{x}\mathfrak{R})", {"ℂ", "ℍ", "ℕ", "ℙ", "ℚ", "ℝ", "ℤ", "ℬ", "𝐱", "ℜ"});
    accepts(R"(\operatorname*{argmax}_{x} f(x)\pmod{p})", {"<munder>", "argmax", ">mod</mtext>"});
    accepts(R"(\textcolor{#456ABC}{x}\hspace{0.5em}\mkern-3mu y)", {"mathcolor=\"#456ABC\"", "width=\"0.5em\"", "width=\"-0.166667em\""});
    accepts(R"(\text{when $x\ge0$ and \textbf{defined}})", {">when </mtext>", "≥", "font-weight:bold", ">defined</mtext>"});
    accepts(R"(\text{50\% \& \# \_ \{a\} \$5})", {"50% &amp; # _ {a} $5"});
    accepts("1 % comment\n+ 2", {"<mn>1</mn>", "<mn>2</mn>"});
    accepts("α+β → ∞", {"α", "β", "→", "∞"});
    accepts(R"(\boldsymbol{\alpha}+\mathbf{\Gamma})", {"𝜶", "𝚪"});
}

void environments() {
    accepts(R"(\begin{pmatrix}1&0\\0&1\end{pmatrix})", {"<mtable ", "form=\"prefix\">(", "</mtr><mtr>"});
    accepts(R"(\begin{bmatrix*}[r]1&2\\3&4\end{bmatrix*})", {"text-align:right", "form=\"prefix\">["});
    accepts(R"(\begin{Vmatrix}a&b\\c&d\end{Vmatrix})", {"‖"});
    accepts(R"(\begin{cases}x^2&x\ge0\\-x&x<0\end{cases})", {"columnalign=\"left\"", "form=\"prefix\">{", "&lt;"});
    accepts(R"(\begin{aligned}[t]x&=1\\y&=2\end{aligned})", {"columnalign=\"right left\"", "text-align:right", "text-align:left"});
    accepts(R"(\begin{alignat}{2}a&=b & c&=d\end{alignat})", {"right left"}, true);
    accepts(R"(\begin{array}{r|cl}x&y&z\\[2pt]\hline 1&2&3\end{array})",
            {"columnalign=\"right center left\"", "columnlines=\"solid none\"", "rowlines=\"solid\""});
    accepts(R"(\begin{smallmatrix}1&2\end{smallmatrix})", {"scriptlevel=\"1\""});
    accepts(R"(\sum_{\substack{i>0\\j>0}}a_{ij})", {"<munder>", "<mtable", "</mtr><mtr>"});
    accepts(R"(\begin{equation}a+b=c\tag{A}\label{eq:a}\end{equation})", {">(A)</mtext>"}, true);
}

void macrosAndErrors() {
    accepts(R"(\newcommand{\half}{\frac12}\half+\half)", {"</mfrac>", "<mn>2</mn>"});
    accepts(R"(\newcommand{\ratio}[2][n]{\frac{#1}{#2}}\ratio{x}+\ratio[m]{y})",
            {"<mi>n</mi>", "<mi>m</mi>", "<mi>y</mi>"});
    accepts(R"(\def\sq#1{#1^2}\sq{x})", {"<msup><mi>x</mi><mn>2</mn></msup>"});
    accepts(R"(\DeclareMathOperator*{\argmax}{arg max}\argmax_x)", {"<munder>", "arg max"});
    accepts(R"(\def\foo{a}\let\bar=\foo\def\foo{b}\bar+\foo)", {"<mi>a</mi>", "<mi>b</mi>"});
    accepts(R"(\begingroup\gdef\foo{a}\endgroup\foo)", {"<mrow></mrow><mi>a</mi>"});
    accepts(R"(\def\alias{\sqrt}\alias{x})", {"<msqrt>"});
    accepts(R"(\def\foo{a}{\def\foo{b}\foo}\foo)", {"<mrow><mi>b</mi></mrow><mi>a</mi>"});
    accepts(R"(\def\x{1}\providecommand{\x}{2}\x)", {"<mrow><mn>1</mn></mrow>"});
    accepts(R"(\renewcommand{\pi}{a}\pi)", {"<mrow><mi>a</mi></mrow>"});
    accepts(R"(\text{<img src="x" onerror="bad">})", {"&lt;img src=&quot;x&quot;", "&gt;"});
    rejects(R"(\frac{1})", "Missing math argument");
    rejects(R"(\sqrt[2)", "Missing ]");
    rejects(R"(\left(x)", "Missing \\right");
    rejects(R"(\begin{matrix}a\end{cases})", "\\end{matrix}");
    rejects(R"(\begin{unknown}x\end{unknown})", "Unsupported math environment");
    rejects(R"(\unknown{x})", "Unsupported TeX command");
    rejects("x_1_2", "Double subscript");
    rejects("x^1^2", "Double superscript");
    rejects(R"(\textcolor{red"><script>}{x})", "Invalid math color");
    rejects(R"(\def\x{\x}\x)", "Macro expansion limit");
    rejects(R"(\newcommand{\m}[1]{#2}\m{x})", "parameter is out of range");
    rejects(R"(\text{an unclosed $x})", "Unclosed math inside text");
    rejects(std::string(65537, 'x'), "64 KiB");
    rejects(std::string(150, '{') + "x" + std::string(150, '}'), "nesting limit");
    rejects(std::string("\xc2"), "Invalid UTF-8");
    rejects(std::string("\xed\xa0\x80"), "Invalid UTF-8");
    rejects(std::string("\xf4\x90\x80\x80"), "Invalid UTF-8");
}

void malformedInputSweep() {
    std::mt19937 rng(2911);
    constexpr std::string_view alphabet = "\\{}^_[]&$%@#+-123abc<>~,|'";
    for (int i = 0; i < 2500; ++i) {
        ++cases;
        std::string input;
        for (auto size = rng() % 96; size > 0; --size) input += alphabet[rng() % alphabet.size()];
        auto result = tex::render(input);
        check(result.error.empty() != result.mathml.empty(), input, "expected either MathML or a diagnostic");
    }
}

}

int main() {
    auto start = std::chrono::steady_clock::now();
    arithmetic();
    symbolsAndLayout();
    environments();
    macrosAndErrors();
    malformedInputSweep();
    const auto elapsed = std::chrono::steady_clock::now() - start;
    std::cout << cases << " formula cases in "
              << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() << " ms\n";
    return failures == 0 ? 0 : 1;
}
