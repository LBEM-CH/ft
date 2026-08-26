// ---------------------------------------------------------------------------
//  Rendering AI Help answers
//
//  The language model behind the Help dialog's AI mode writes Markdown, and -
//  being trained on scientific text - LaTeX mathematics, however firmly the
//  prompt asks for plain text. Escaping that verbatim into the results pane
//  (the first implementation) is honest but hard to read: **bold** stays
//  starred, $\int f \cdot h$ stays backslashed.
//
//  The pane is a QTextBrowser, which renders a useful HTML subset but neither
//  Markdown nor mathematics. So this header converts what the model actually
//  produces into that subset:
//
//    * the Markdown the model uses: **bold**, *italic*, `code`, # headings,
//      bullet and numbered lists, --- rules;
//    * LaTeX math spans ($...$, $$...$$, \(...\), \[...\]), degraded to
//      Unicode: \int -> ∫, \alpha -> α, ^{2} -> superscript, \frac{a}{b} ->
//      (a)/(b), and so on. Degraded, not typeset: a command with no
//      plain-text equivalent stays visible as written rather than vanishing,
//      so the reader can tell something was there.
//    * [tag] citations, rewritten to numbered links into the manual exactly
//      as before (see linkCitationsInHtml below).
//
//  Header-only and Qt-only on purpose: the same functions serve the
//  application and a standalone test harness, with nothing to moc or link.
//  Kept in step with the port of this AI Help mode in the 4d application
//  (apps/src/widgets/AnswerFormatting.h there), so fixes travel both ways.
// ---------------------------------------------------------------------------
#ifndef ANSWERFORMATTING_H
#define ANSWERFORMATTING_H

#include <QChar>
#include <QHash>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVector>

namespace AnswerFormatting {

// One LaTeX math span -> HTML-safe Unicode. The input is the raw span content
// (no delimiters, not yet HTML-escaped); the result is escaped HTML.
inline QString latexSpanToHtml(QString m)
{
    // Commands with a Unicode equivalent. Longer names first, so \leftarrow
    // is not eaten by \le and \sum not left half-replaced by a shorter match.
    static const struct { const char* cmd; const char* uni; } kSubs[] = {
        {"\\leftrightarrow", "↔"}, {"\\Leftrightarrow", "⇔"},
        {"\\longrightarrow", "→"}, {"\\rightarrow", "→"}, {"\\leftarrow", "←"},
        {"\\Rightarrow", "⇒"}, {"\\Leftarrow", "⇐"}, {"\\mapsto", "↦"},
        {"\\varepsilon", "ε"}, {"\\epsilon", "ε"}, {"\\vartheta", "θ"},
        {"\\varphi", "φ"}, {"\\lambda", "λ"}, {"\\Lambda", "Λ"},
        {"\\alpha", "α"}, {"\\beta", "β"}, {"\\gamma", "γ"}, {"\\Gamma", "Γ"},
        {"\\delta", "δ"}, {"\\Delta", "Δ"}, {"\\theta", "θ"}, {"\\Theta", "Θ"},
        {"\\kappa", "κ"}, {"\\sigma", "σ"}, {"\\Sigma", "Σ"}, {"\\omega", "ω"},
        {"\\Omega", "Ω"}, {"\\mu", "μ"}, {"\\nu", "ν"}, {"\\xi", "ξ"},
        {"\\pi", "π"}, {"\\Pi", "Π"}, {"\\rho", "ρ"}, {"\\tau", "τ"},
        {"\\phi", "φ"}, {"\\Phi", "Φ"}, {"\\chi", "χ"}, {"\\psi", "ψ"},
        {"\\Psi", "Ψ"}, {"\\eta", "η"}, {"\\zeta", "ζ"},
        {"\\infty", "∞"}, {"\\partial", "∂"}, {"\\nabla", "∇"},
        {"\\propto", "∝"}, {"\\approx", "≈"}, {"\\simeq", "≃"}, {"\\sim", "∼"},
        {"\\neq", "≠"}, {"\\ne", "≠"}, {"\\leq", "≤"}, {"\\le", "≤"},
        {"\\geq", "≥"}, {"\\ge", "≥"}, {"\\ll", "≪"}, {"\\gg", "≫"},
        {"\\pm", "±"}, {"\\mp", "∓"}, {"\\times", "×"}, {"\\div", "÷"},
        {"\\cdot", "·"}, {"\\ast", "∗"}, {"\\star", "⋆"}, {"\\circ", "∘"},
        {"\\otimes", "⊗"}, {"\\oplus", "⊕"}, {"\\iint", "∬"}, {"\\int", "∫"},
        {"\\oint", "∮"}, {"\\sum", "Σ"}, {"\\prod", "Π"}, {"\\sqrt", "√"},
        {"\\langle", "⟨"}, {"\\rangle", "⟩"}, {"\\hbar", "ℏ"},
        {"\\ldots", "…"}, {"\\cdots", "⋯"}, {"\\dots", "…"}, {"\\prime", "′"},
        {"\\in", "∈"}, {"\\forall", "∀"}, {"\\exists", "∃"},
        {"\\quad", "  "}, {"\\qquad", "   "},
    };
    for (const auto& s : kSubs)
        m.replace(QString::fromLatin1(s.cmd), QString::fromUtf8(s.uni));

    // Thin-space and glue commands, and the sizing/style noise that carries no
    // content of its own.
    static const QRegularExpression kSpacing(QStringLiteral("\\\\[,;:! ]"));
    m.replace(kSpacing, QStringLiteral(" "));
    static const QRegularExpression kNoise(QStringLiteral(
        "\\\\(?:left|right|[bB]igg?[lr]?|displaystyle|limits|nolimits)\\b"));
    m.remove(kNoise);

    // Wrappers whose argument is the content: \mathbf{x} -> x. Repeated, for
    // one level of nesting per pass.
    static const QRegularExpression kWrap(QStringLiteral(
        "\\\\(?:mathrm|mathbf|mathit|mathcal|mathbb|boldsymbol|text|textrm|"
        "operatorname)\\s*\\{([^{}]*)\\}"));
    for (int i = 0; i < 3 && m.contains(kWrap); ++i)
        m.replace(kWrap, QStringLiteral("\\1"));

    // \frac{a}{b} -> (a)/(b), parenthesised only when the operand needs it.
    static const QRegularExpression kFrac(QStringLiteral(
        "\\\\[dt]?frac\\s*\\{([^{}]*)\\}\\s*\\{([^{}]*)\\}"));
    QRegularExpressionMatch f;
    for (int i = 0; i < 3 && (f = kFrac.match(m)).hasMatch(); ++i) {
        auto paren = [](const QString& s) {
            static const QRegularExpression kAtom(QStringLiteral("^[^\\s+−/-]*$"));
            return kAtom.match(s).hasMatch() ? s : QStringLiteral("(") + s + QStringLiteral(")");
        };
        m.replace(f.capturedStart(), f.capturedLength(),
                  paren(f.captured(1)) + QStringLiteral("/") + paren(f.captured(2)));
    }

    m = m.toHtmlEscaped();

    // Super- and subscripts, as tags -- Unicode has too few of them. Applied
    // after escaping: ^ and _ are untouched by it, and the argument is safe.
    static const QRegularExpression kSupB(QStringLiteral("\\^\\{([^{}]+)\\}")),
                                    kSupC(QStringLiteral("\\^([^\\s{}])")),
                                    kSubB(QStringLiteral("_\\{([^{}]+)\\}")),
                                    kSubC(QStringLiteral("_([^\\s{}])"));
    m.replace(kSupB, QStringLiteral("<sup>\\1</sup>"));
    m.replace(kSupC, QStringLiteral("<sup>\\1</sup>"));
    m.replace(kSubB, QStringLiteral("<sub>\\1</sub>"));
    m.replace(kSubC, QStringLiteral("<sub>\\1</sub>"));

    // Leftover grouping braces have done their job; leftover \commands stay,
    // visibly, because hiding them would misquote the model.
    m.remove(QLatin1Char('{'));
    m.remove(QLatin1Char('}'));
    return m.simplified();
}

// The model cites sections by tag, as [p2-ctf-fit] -- meaningful to it, but
// the reader would have to match that string against the source list by eye.
// So tags are rewritten into numbered links, [3], pointing straight at the
// manual section, and the source list below is numbered to match. A tag that
// is not among the sources is left exactly as written rather than silently
// dropped: that only happens when the model invented a citation, which is
// worth seeing. Operates on already-escaped HTML; sources rows are
// [score, tag, title, url].
inline QString linkCitationsInHtml(const QString& escaped,
                                   const QVector<QStringList>& sources)
{
    QHash<QString, int> number;
    for (int i = 0; i < sources.size(); ++i)
        number.insert(sources.at(i).value(1), i + 1);

    static const QRegularExpression re(QStringLiteral("\\[([A-Za-z0-9._#-]+)\\]"));

    QString out;
    qsizetype last = 0;
    auto it = re.globalMatch(escaped);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += escaped.mid(last, m.capturedStart() - last);
        const int n = number.value(m.captured(1), 0);
        if (n > 0)
            out += QStringLiteral("<a href=\"%1\">[%2]</a>")
                       .arg(sources.at(n - 1).value(3).toHtmlEscaped()).arg(n);
        else
            out += m.captured(0);
        last = m.capturedEnd();
    }
    out += escaped.mid(last);
    return out;
}

// The whole pipeline: model output in, QTextBrowser HTML out. Inherits the
// pane's text colour; the caller wraps it in a coloured <div>.
inline QString answerToHtml(const QString& plain,
                            const QVector<QStringList>& sources)
{
    // -- 1. Stash math spans behind placeholders. They must not reach the
    // Markdown pass: *, _ and [ mean something else inside a formula. The
    // placeholder character cannot occur in model output, and toHtmlEscaped()
    // leaves it alone.
    const QChar kMark(0x01);
    QStringList math;
    QString text = plain;
    auto stashAll = [&](const QRegularExpression& re) {
        QString out;
        qsizetype last = 0;
        auto it = re.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            out += text.mid(last, m.capturedStart() - last);
            out += kMark + QString::number(math.size()) + kMark;
            math << latexSpanToHtml(m.captured(1));
            last = m.capturedEnd();
        }
        out += text.mid(last);
        text = out;
    };
    static const QRegularExpression
        kDisplayDollar(QStringLiteral("\\$\\$(.+?)\\$\\$"),
                       QRegularExpression::DotMatchesEverythingOption),
        kDisplayBracket(QStringLiteral("\\\\\\[(.+?)\\\\\\]"),
                        QRegularExpression::DotMatchesEverythingOption),
        kInlineParen(QStringLiteral("\\\\\\((.+?)\\\\\\)"),
                     QRegularExpression::DotMatchesEverythingOption),
        kInlineDollar(QStringLiteral("\\$([^$\\n]+)\\$"));
    stashAll(kDisplayDollar);
    stashAll(kDisplayBracket);
    stashAll(kInlineParen);
    stashAll(kInlineDollar);

    // -- 2. Escape, then link the citations.
    text = linkCitationsInHtml(text.toHtmlEscaped(), sources);

    // -- 3. Inline Markdown. Backticks and stars are untouched by escaping.
    // Bold before italic, so ** is not read as two italics.
    static const QRegularExpression
        kCode(QStringLiteral("`([^`\\n]+)`")),
        kBold(QStringLiteral("\\*\\*([^*\\n]+)\\*\\*")),
        kItalic(QStringLiteral("(?<!\\*)\\*([^*\\s][^*\\n]*)\\*(?!\\*)"));
    text.replace(kCode, QStringLiteral("<code>\\1</code>"));
    text.replace(kBold, QStringLiteral("<b>\\1</b>"));
    text.replace(kItalic, QStringLiteral("<i>\\1</i>"));

    // -- 4. Math back in, in italic the way print sets it.
    for (int i = 0; i < math.size(); ++i)
        text.replace(kMark + QString::number(i) + kMark,
                     QStringLiteral("<i>") + math.at(i) + QStringLiteral("</i>"));

    // -- 5. Block structure, line by line: headings, list items, rules,
    // paragraphs. Lists are indented paragraphs rather than <ul>/<ol>, which
    // keeps the model's own numbering and QTextBrowser's list margins out of
    // the picture.
    static const QRegularExpression
        kHeading(QStringLiteral("^\\s*#{1,6}\\s+(.*)$")),
        kRule(QStringLiteral("^\\s*-{3,}\\s*$")),
        kBullet(QStringLiteral("^\\s*[-*•]\\s+(.*)$")),
        kNumbered(QStringLiteral("^\\s*(\\d{1,3})[.)]\\s+(.*)$"));

    QString html;
    QStringList para;
    auto flush = [&]() {
        if (!para.isEmpty())
            html += QStringLiteral("<p style=\"margin:4px 0;\">")
                    + para.join(QStringLiteral("<br>")) + QStringLiteral("</p>");
        para.clear();
    };
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        const QString trimmed = line.trimmed();
        QRegularExpressionMatch m;
        if (trimmed.isEmpty()) {
            flush();
        } else if ((m = kHeading.match(line)).hasMatch()) {
            flush();
            html += QStringLiteral("<p style=\"margin:8px 0 2px 0;\"><b>")
                    + m.captured(1) + QStringLiteral("</b></p>");
        } else if (kRule.match(line).hasMatch()) {
            flush();
            html += QStringLiteral("<hr>");
        } else if ((m = kBullet.match(line)).hasMatch()) {
            flush();
            html += QStringLiteral("<p style=\"margin:2px 0 2px 14px;\">•&nbsp;")
                    + m.captured(1) + QStringLiteral("</p>");
        } else if ((m = kNumbered.match(line)).hasMatch()) {
            flush();
            html += QStringLiteral("<p style=\"margin:2px 0 2px 14px;\">%1.&nbsp;")
                        .arg(m.captured(1))
                    + m.captured(2) + QStringLiteral("</p>");
        } else {
            para << trimmed;
        }
    }
    flush();
    return html;
}

} // namespace AnswerFormatting

#endif // ANSWERFORMATTING_H
