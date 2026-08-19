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
static QString htmlPara(const QString &s)
{
    return s.toHtmlEscaped().replace("\n", "<br>");
}

void FtWindow::aiEnsureStarted()
{
    if (m_aiProc && m_aiProc->state() != QProcess::NotRunning)
        return;

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
    m_aiStatus = "starting";
    m_aiBuf.clear();

    m_aiProc = new QProcess(this);
    m_aiProc->setWorkingDirectory(dir);   // so "rag/..." paths inside resolve

    connect(m_aiProc, &QProcess::readyReadStandardOutput, this, [this]() {
        m_aiBuf += m_aiProc->readAllStandardOutput();
        int nl;
        while ((nl = m_aiBuf.indexOf('\n')) >= 0) {
            const QByteArray line = m_aiBuf.left(nl);
            m_aiBuf.remove(0, nl + 1);
            if (!line.trimmed().isEmpty())
                aiHandleLine(line);
        }
    });
    // The worker's diagnostics (model download progress, tracebacks) are not
    // protocol, so they go to the console rather than the results pane.
    connect(m_aiProc, &QProcess::readyReadStandardError, this, [this]() {
        qDebug().noquote() << "[rag]" << m_aiProc->readAllStandardError().trimmed();
    });
    connect(m_aiProc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        m_aiReady = false;
        m_aiBusy = false;
        m_aiStatus = "could not start " + m_aiProc->program() +
                     " - " + m_aiProc->errorString();
        aiRender();
    });
    connect(m_aiProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) {
        m_aiReady = false;
        m_aiBusy = false;
        if (code != 0)
            m_aiStatus = QStringLiteral("the helper exited (code %1) - see the "
                                        "console for its output").arg(code);
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

void FtWindow::aiAsk(const QString &question)
{
    aiEnsureStarted();
    if (!m_aiProc || m_aiProc->state() == QProcess::NotRunning)
        return;

    m_aiThink.clear();
    m_aiAnswer.clear();
    m_aiStream.clear();
    m_aiSources.clear();
    m_aiShowThink = false;
    m_aiBusy = true;
    aiRender();

    // Safe to send before "ready" arrives: the worker reads stdin only after
    // its models are loaded, and the pipe holds the line until then.
    QJsonObject req;
    req["question"] = question;
    m_aiProc->write(QJsonDocument(req).toJson(QJsonDocument::Compact) + "\n");
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
            m_aiSources.append(QStringList{
                QString::number(s.value("score").toDouble(), 'f', 3),
                s.value("anchor").toString(),
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
    }
    aiRender();
}

void FtWindow::aiRender()
{
    if (!m_aiOut)
        return;

    QString html;

    // Still loading, or something went wrong.
    if (!m_aiReady && !m_aiBusy && m_aiAnswer.isEmpty()) {
        html = "<p style=\"color:#bbb;\">" +
               (m_aiStatus.isEmpty() ? QStringLiteral("Starting the local model...")
                                     : htmlPara(m_aiStatus)) + "</p>";
        m_aiOut->setHtml(html);
        if (m_aiThinkBtn) m_aiThinkBtn->hide();
        return;
    }

    if (m_aiBusy) {
        html += "<p style=\"color:#bbb;\">" +
                (m_aiReady ? QStringLiteral("Reading the manual...")
                           : QStringLiteral("Loading the model - ") + htmlPara(m_aiStatus)) +
                "</p>";
        if (!m_aiStream.isEmpty())
            html += "<p style=\"color:#888;\">" + htmlPara(m_aiStream.right(1200)) + "</p>";
    }

    // The reasoning, folded away unless asked for. QTextBrowser's HTML subset
    // has no <details>, so the fold is the button beside the pane, not markup.
    if (!m_aiThink.isEmpty() && m_aiShowThink)
        html += "<div style=\"color:#999; border-left:2px solid #555; "
                "padding-left:8px; margin:6px 0;\"><i>" +
                htmlPara(m_aiThink) + "</i></div>";

    if (!m_aiAnswer.isEmpty())
        html += "<p style=\"color:#eee;\">" + htmlPara(m_aiAnswer) + "</p>";

    if (!m_aiSources.isEmpty() && !m_aiBusy) {
        html += "<p style=\"color:#999; margin:10px 0 2px 0;\">Sources</p>";
        for (const QStringList &s : m_aiSources)
            html += QStringLiteral("<p style=\"margin:0 0 4px 14px;\">"
                                   "<span style=\"color:#777;\">%1</span> "
                                   "<a href=\"%2\">%3</a></p>")
                        .arg(s.value(0), s.value(3),
                             (s.value(2).isEmpty() ? s.value(1) : s.value(2)).toHtmlEscaped());
    }

    m_aiOut->setHtml(html);

    if (m_aiThinkBtn) {
        m_aiThinkBtn->setVisible(!m_aiThink.isEmpty());
        m_aiThinkBtn->setText(m_aiShowThink ? "Hide reasoning" : "Show reasoning");
    }
}

#endif // !__EMSCRIPTEN__
