// ---------------------------------------------------------------------------
//  AI Help mode
//
//  The Help dialog's literal search answers "where does this word appear?".
//  This answers "what is the answer?" - a local language model reads the manual
//  sections that retrieval selected and replies directly, citing them.
//
//  The retrieval and generation live in rag/ (Python). This file is only the
//  plumbing: start that worker once, keep it warm, send questions, render
//  replies. Loading an 8B model costs 10-20 seconds, so the process outlives
//  the dialog and survives Help being closed and reopened.
//
//  Wire protocol - one JSON object per line each way, documented in
//  rag/serve.py. Messages arriving here:
//
//      {"type":"status","stage":"llm","detail":"..."}   loading progress
//      {"type":"ready"}                                 accepting questions
//      {"type":"retrieved","sources":[...]}             sections chosen
//      {"type":"token","text":"..."}                    streamed while writing
//      {"type":"answer","think":"...","answer":"...","sources":[...]}
//      {"type":"error","message":"...","fatal":bool}
//
//  Not built for WebAssembly: no QProcess, no local model. There the literal
//  "Find in manual" search remains the only route.
// ---------------------------------------------------------------------------
#include "ftwindow.h"

#ifndef __EMSCRIPTEN__

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QPushButton>
#include <QTextBrowser>

#include "answerformatting.h"

// Where rag/ lives. Empty means "work it out from the executable's location",
// which covers the normal case of building into <repo>/build.
static QString s_aiDir;

void FtWindow::setAiDir(const QString &dir) { s_aiDir = dir; }

QString FtWindow::aiDir()
{
    if (!s_aiDir.isEmpty())
        return s_aiDir;
    // <repo>/build/ft  ->  <repo>.  Also try the working directory, so running
    // from the source tree during development just works.
    const QDir appDir(QCoreApplication::applicationDirPath());
    for (const QString &cand : { appDir.absoluteFilePath(".."),
                                 appDir.absolutePath(),
                                 QDir::currentPath() })
        if (QFileInfo::exists(cand + "/rag/serve.py"))
            return QDir(cand).absolutePath();
    return appDir.absoluteFilePath("..");
}

// Escape for the results pane and keep the line breaks the model produced.
// For status lines and the live token stream only: finished replies go
// through AnswerFormatting::answerToHtml() (answerformatting.h), which
// renders the model's Markdown and mathematics properly, citations included.
static QString htmlPara(const QString &s)
{
    return s.toHtmlEscaped().replace("\n", "<br>");
}

void FtWindow::aiEnsureStarted()
{
    if (m_aiProc && m_aiProc->state() != QProcess::NotRunning)
        return;

    // A worker that has exited is replaced, not kept: deleting it here also
    // drops its signal connections, so a dead process can neither accumulate
    // per restart nor call back into the fresh one's state.
    if (m_aiProc) {
        m_aiProc->deleteLater();
        m_aiProc = nullptr;
    }

    const QString dir = aiDir();
    const QString script = dir + "/rag/serve.py";
    if (!QFileInfo::exists(script)) {
        m_aiStatus = "rag/serve.py not found under " + dir +
                     " - see rag/README.md, or call FtWindow::setAiDir().";
        aiRender();
        return;
    }
    // Prefer the project's own environment; fall back to whatever python3 is
    // on PATH so a system-wide install still works.
    QString py = dir + "/rag/.venv/bin/python3";
    if (!QFileInfo::exists(py))
        py = "python3";

    m_aiReady = false;
    m_aiFatal = false;
    m_aiStatus = "starting";
    m_aiBuf.clear();
    m_aiErr.clear();

    m_aiProc = new QProcess(this);
    m_aiProc->setWorkingDirectory(dir);   // so "rag/..." paths inside resolve

    connect(m_aiProc, &QProcess::readyReadStandardOutput, this,
            [this]() { aiReadStdout(false); });
    // The worker's diagnostics (model download progress, tracebacks) are not
    // protocol, so they go to the console rather than the results pane. A tail
    // is kept all the same: if the worker dies, that tail is the results pane's
    // explanation — the app may have been launched from Finder, where there is
    // no console to go and see.
    connect(m_aiProc, &QProcess::readyReadStandardError, this, [this]() {
        const QByteArray err = m_aiProc->readAllStandardError();
        qDebug().noquote() << "[rag]" << err.trimmed();
        m_aiErr += err;
        if (m_aiErr.size() > 2000)
            m_aiErr = m_aiErr.right(2000);
    });
    connect(m_aiProc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        m_aiReady = false;
        m_aiBusy = false;
        m_aiStatus = "could not start " + m_aiProc->program() +
                     " - " + m_aiProc->errorString();
        aiRender();
    });
    // A startup failure the worker could diagnose arrives as a fatal protocol
    // error, immediately followed by exit code 1 (see rag/serve.py) — so when
    // m_aiFatal is set, m_aiStatus already names the actual problem and must
    // not be overwritten with this generic line. Without it, the stderr tail
    // (a traceback the worker did not survive to report) is the best account
    // of what happened.
    connect(m_aiProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) {
        // Qt does not promise the readyRead signals have delivered the dying
        // process's last bytes yet, so both channels are drained here first —
        // a fatal error emitted with the worker's last breath must set
        // m_aiFatal (and the stderr tail be complete) before the exit is
        // judged below.
        aiReadStdout(true);
        const QByteArray err = m_aiProc->readAllStandardError();
        if (!err.trimmed().isEmpty()) {
            qDebug().noquote() << "[rag]" << err.trimmed();
            m_aiErr += err;
            // Same tail cap as the readyRead handler: a worker dying with a
            // long undrained traceback must not paste it all into the pane.
            if (m_aiErr.size() > 2000)
                m_aiErr = m_aiErr.right(2000);
        }
        m_aiReady = false;
        m_aiBusy = false;
        if (code != 0 && !m_aiFatal) {
            m_aiStatus = QStringLiteral("the helper exited (code %1)").arg(code);
            const QString err = QString::fromUtf8(m_aiErr).trimmed();
            if (!err.isEmpty())
                m_aiStatus += "\n\nIts last output:\n" + err;
        }
        aiRender();
    });
    // Do not leave the model resident after the application closes.
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() { aiStop(); });

    m_aiProc->start(py, { script });
}

void FtWindow::aiStop()
{
    if (!m_aiProc)
        return;
    if (m_aiProc->state() != QProcess::NotRunning) {
        m_aiProc->write("{\"command\":\"quit\"}\n");
        m_aiProc->closeWriteChannel();
        if (!m_aiProc->waitForFinished(3000))
            m_aiProc->kill();
    }
    m_aiReady = false;
}

// The reply fields live on the window, not on the dialog, so that the worker
// and its loaded model survive Help being closed and reopened. That means they
// must be cleared deliberately: otherwise reopening Help and switching to AI
// mode repaints the previous answer, which reads as the app answering a
// question nobody asked.
void FtWindow::aiResetReply()
{
    m_aiThink.clear();
    m_aiAnswer.clear();
    m_aiStream.clear();
    m_aiSources.clear();
    m_aiShowThink = false;
    if (m_aiThinkBtn)
        m_aiThinkBtn->hide();
}

void FtWindow::aiAsk(const QString &question)
{
    if (m_aiBusy)          // one question at a time; ignore double submits
        return;

    aiEnsureStarted();
    if (!m_aiProc || m_aiProc->state() == QProcess::NotRunning)
        return;

    aiResetReply();
    m_aiBusy = true;
    aiRender();

    // Safe to send before "ready" arrives: the worker reads stdin only after
    // its models are loaded, and the pipe holds the line until then.
    QJsonObject req;
    req["question"] = question;
    m_aiProc->write(QJsonDocument(req).toJson(QJsonDocument::Compact) + "\n");
}

// Deliver whatever worker stdout is buffered, one protocol line at a time.
// Called from readyReadStandardOutput as data arrives, and once more from the
// finished handler with flushPartial set: at that point no further read will
// ever complete a dangling line, so an unterminated remainder is handled now
// rather than silently dropped.
void FtWindow::aiReadStdout(bool flushPartial)
{
    m_aiBuf += m_aiProc->readAllStandardOutput();
    int nl;
    while ((nl = m_aiBuf.indexOf('\n')) >= 0) {
        const QByteArray line = m_aiBuf.left(nl);
        m_aiBuf.remove(0, nl + 1);
        if (!line.trimmed().isEmpty())
            aiHandleLine(line);
    }
    if (flushPartial && !m_aiBuf.trimmed().isEmpty()) {
        aiHandleLine(m_aiBuf);
        m_aiBuf.clear();
    }
}

void FtWindow::aiHandleLine(const QByteArray &line)
{
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qDebug().noquote() << "[rag] unparsable:" << line;
        return;
    }
    const QJsonObject o = doc.object();
    const QString type = o.value("type").toString();

    auto readSources = [this](const QJsonArray &arr) {
        m_aiSources.clear();
        for (const QJsonValue &v : arr) {
            const QJsonObject s = v.toObject();
            // [0] score, [1] citation tag, [2] title, [3] url. The tag is the
            // exact string the model was asked to cite, so answers can be
            // matched against it -- anchor where there is one, page otherwise.
            m_aiSources.append(QStringList{
                QString::number(s.value("score").toDouble(), 'f', 3),
                s.value("tag").toString(),
                s.value("title").toString(),
                s.value("url").toString() });
        }
    };

    if (type == "status") {
        m_aiStatus = o.value("stage").toString() + ": " + o.value("detail").toString();
    } else if (type == "ready") {
        m_aiReady = true;
        m_aiStatus.clear();
    } else if (type == "retrieved") {
        readSources(o.value("sources").toArray());
    } else if (type == "token") {
        m_aiStream += o.value("text").toString();
    } else if (type == "answer") {
        m_aiThink  = o.value("think").toString();
        m_aiAnswer = o.value("answer").toString();
        readSources(o.value("sources").toArray());
        m_aiStream.clear();
        m_aiBusy = false;
    } else if (type == "error") {
        m_aiBusy = false;
        m_aiStatus = o.value("message").toString();
        // A fatal error is the worker's exit note: it quits right after, and
        // the finished handler must leave this message standing.
        if (o.value("fatal").toBool())
            m_aiFatal = true;
    }
    aiRender();
}

void FtWindow::aiRender()
{
    if (!m_aiOut)
        return;

    // Inline colours matching the Help dialog's current look; the dialog
    // keeps m_aiDark in step with its Dark mode button (helptheme.h).
    const QString fg     = m_aiDark ? QStringLiteral("#eeeeee") : QStringLiteral("#1d1d20");
    const QString muted  = m_aiDark ? QStringLiteral("#bbbbbb") : QStringLiteral("#5a5a62");
    const QString dim    = m_aiDark ? QStringLiteral("#888888") : QStringLiteral("#8a8a92");
    const QString border = m_aiDark ? QStringLiteral("#555555") : QStringLiteral("#c9c9d0");

    QString html;

    // Nothing asked yet: report loading progress, or invite a question. Never
    // show a stale reply here -- aiResetReply() has cleared it, and reaching
    // this branch means the pane is between questions.
    if (!m_aiBusy && m_aiAnswer.isEmpty()) {
        if (m_aiReady && m_aiStatus.isEmpty())
            html = QStringLiteral("<p style=\"color:%1;\">Ready. Type a question "
                                  "above and press Ask.</p>").arg(muted);
        else
            html = QStringLiteral("<p style=\"color:%1;\">").arg(muted) +
                   (m_aiStatus.isEmpty() ? QStringLiteral("Starting the local model...")
                                         : htmlPara(m_aiStatus)) + "</p>";
        m_aiOut->setHtml(html);
        if (m_aiThinkBtn) m_aiThinkBtn->hide();
        return;
    }

    if (m_aiBusy) {
        html += QStringLiteral("<p style=\"color:%1;\">").arg(muted) +
                (m_aiReady ? QStringLiteral("Reading the manual...")
                           : QStringLiteral("Loading the model - ") + htmlPara(m_aiStatus)) +
                "</p>";
        if (!m_aiStream.isEmpty())
            html += QStringLiteral("<p style=\"color:%1;\">").arg(dim)
                    + htmlPara(m_aiStream.right(1200)) + "</p>";
    }

    // The reasoning, folded away unless asked for. QTextBrowser's HTML subset
    // has no <details>, so the fold is the button beside the pane, not markup.
    if (!m_aiThink.isEmpty() && m_aiShowThink)
        html += QStringLiteral("<div style=\"color:%1; border-left:2px solid %2; "
                               "padding-left:8px; margin:6px 0;\">").arg(dim, border) +
                AnswerFormatting::answerToHtml(m_aiThink, m_aiSources) + "</div>";

    if (!m_aiAnswer.isEmpty())
        html += QStringLiteral("<div style=\"color:%1;\">").arg(fg) +
                AnswerFormatting::answerToHtml(m_aiAnswer, m_aiSources) + "</div>";

    if (!m_aiSources.isEmpty() && !m_aiBusy) {
        html += QStringLiteral("<p style=\"color:%1; margin:10px 0 2px 0;\">Sources</p>").arg(dim);
        int n = 0;
        for (const QStringList &s : m_aiSources) {
            ++n;
            // Numbered to match the [N] links in the answer above, so a citation
            // can be followed either way: click it, or find it by number here.
            html += QStringLiteral("<p style=\"margin:0 0 4px 14px;\">"
                                   "<a href=\"%1\">[%2]</a> %3 "
                                   "<span style=\"color:%4;\">%5</span></p>")
                        .arg(s.value(3).toHtmlEscaped()).arg(n)
                        .arg((s.value(2).isEmpty() ? s.value(1) : s.value(2)).toHtmlEscaped(),
                             dim, s.value(0));
        }
    }

    m_aiOut->setHtml(html);

    if (m_aiThinkBtn) {
        m_aiThinkBtn->setVisible(!m_aiThink.isEmpty());
        m_aiThinkBtn->setText(m_aiShowThink ? "Hide reasoning" : "Show reasoning");
    }
}

#endif // !__EMSCRIPTEN__
