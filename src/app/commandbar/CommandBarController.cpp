#include "app/commandbar/CommandBarController.h"

#include "app/editor/AppController.h"
#include "app/mcp/McpController.h"
#include "app/mcp/tools/McpCatalog.h"
#include "app/mcp/tools/McpDispatcher.h"
#include "engine/addons/PackageSearchPaths.h"
#include "engine/ml/llm/LlamaServer.h"

#include <QDir>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLocale>

using namespace drift::commandbar;

namespace {

constexpr int kRequestTimeoutMs = 120000;
constexpr int kMaxNewTokens = 512;
const char *const kLogSource = "Command bar";

QString humanise(QString name)
{
    name.replace(QLatin1Char('_'), QLatin1Char(' '));
    return name.isEmpty() ? name : name.at(0).toUpper() + name.mid(1);
}

QString elided(const QString &text, int limit)
{
    return text.size() <= limit ? text : text.left(limit - 1) + QChar(0x2026);
}

bool looksLikeId(const QString &s)
{
    return s.size() >= 20 && !s.contains(QLatin1Char(' '));
}

bool isUndoStep(const QString &tool)
{
    return !drift::mcp::isReadOnlyOp(tool) && !drift::mcp::undoExemptOps().contains(tool);
}

QString problemText(const QJsonObject &reply)
{
    QJsonObject first = reply.value(QStringLiteral("problems")).toArray().first().toObject();
    if (first.isEmpty())
        first = reply.contains(QStringLiteral("failed")) ? reply.value(QStringLiteral("failed")).toObject() : reply;
    const QString detail = first.value(QStringLiteral("detail")).toString();
    const QString code = first.value(QStringLiteral("error")).toString();
    const QString tool = first.value(QStringLiteral("tool")).toString(reply.value(QStringLiteral("tool")).toString());
    const QString what = detail.isEmpty() ? code : detail;
    return elided(tool.isEmpty() ? what : humanise(tool) + QStringLiteral(": ") + what, 200);
}

} // namespace

CommandBarController::CommandBarController(AppController *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
    , m_dispatcher(std::make_unique<drift::mcp::McpDispatcher>(app))
{
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(kRequestTimeoutMs);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        if (!m_busy)
            return;
        ++m_generation;
        setBusy(false);
        setLastError(tr("The command model took too long to answer."));
        toast(QStringLiteral("error"), m_lastError);
    });
}

CommandBarController::~CommandBarController() = default;

QString CommandBarController::modelPath()
{
    const QStringList roots = PackageSearchPaths::defaultSearchPaths(
        QStringLiteral("DRIFT_COMMAND_MODEL"), QStringLiteral("command-model"), QStringLiteral("command-model"));
    for (const QString &root : roots) {
        const QStringList files = QDir(root).entryList({QStringLiteral("*.gguf")}, QDir::Files, QDir::Name);
        if (!files.isEmpty())
            return QDir(root).filePath(files.first());
    }
    return {};
}

bool CommandBarController::available() const
{
    const bool now = drift::llm::LlamaServer::available() && !modelPath().isEmpty();
    // The addons install while the app runs; whoever reads this next (the search popup opens, a
    // command is asked) sees the change.
    auto *self = const_cast<CommandBarController *>(this);
    if (now != m_lastAvailable) {
        self->m_lastAvailable = now;
        emit self->availableChanged();
    }
    return now;
}

void CommandBarController::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    if (!busy)
        m_timeout.stop();
    emit busyChanged();
}

void CommandBarController::setLastError(const QString &error)
{
    if (m_lastError == error)
        return;
    m_lastError = error;
    emit lastErrorChanged();
}

void CommandBarController::toast(const QString &severity, const QString &message, const QString &action)
{
    emit toastRequested(severity, message, action);
}

void CommandBarController::prewarm()
{
    if (!available())
        return;
    if (!m_server) {
        m_server = new drift::llm::LlamaServer(this);
        connect(m_server, &drift::llm::LlamaServer::failed, this,
                [this](const QString &reason) { setLastError(reason.section(QLatin1Char('\n'), 0, 0)); });
    }
    if (!m_server->isRunning() && !m_server->isStarting())
        m_server->start(modelPath());
}

void CommandBarController::cancel()
{
    ++m_generation;
    setBusy(false);
}

void CommandBarController::ask(const QString &textIn)
{
    const QString text = textIn.trimmed();
    if (text.isEmpty())
        return;
    // Whatever was waiting on an earlier question is moot.
    ++m_generation;
    m_pending = {};
    if (!available()) {
        setLastError(tr("The Command Bar Model add-on is not installed."));
        setBusy(false);
        toast(QStringLiteral("error"), m_lastError);
        return;
    }
    setLastError({});

    if (!m_retriever) {
        m_catalog = CommandCatalog::live();
        m_retriever = std::make_unique<CommandRetriever>(m_catalog.ops());
    }
    CommandContext context;
    context.playhead = m_app->playheadSeconds();
    const int track = m_app->selectedTrack();
    const int clip = m_app->selectedClip();
    if (track >= 0 && clip >= 0) {
        const QString id = m_app->mcp()->clipId(track, clip);
        if (!id.isEmpty()) {
            context.hasSelection = true;
            context.clip = id;
            context.track = track;
            context.index = clip;
        }
    }
    const QString prompt = commandPrompt(text, context, m_catalog.pick(m_retriever->top(text, 10)));

    prewarm();
    setBusy(true);
    m_timeout.start();
    const int generation = m_generation;
    m_server->complete(prompt, kMaxNewTokens, [this, generation, text](const drift::llm::Completion &c) {
        if (generation != m_generation)
            return;
        finish(text, c.content, c.ok ? QString() : c.error);
    });
}

void CommandBarController::finish(const QString &query, const QString &content, const QString &error)
{
    setBusy(false);
    if (!error.isEmpty()) {
        setLastError(error.section(QLatin1Char('\n'), 0, 0));
        toast(QStringLiteral("error"), tr("The command model failed: %1").arg(m_lastError));
        return;
    }
    const ParsedCalls parsed = parseCommandCalls(content);
    QList<ToolCall> calls;
    for (const ToolCall &call : parsed.calls) {
        if (drift::mcp::isKnownOp(call.name))
            calls.append(call);
    }
    if (parsed.refusal || calls.isEmpty()) {
        toast(QStringLiteral("info"), tr("No matching command for “%1”").arg(elided(query, 80)));
        return;
    }
    execute(query, calls);
}

QString CommandBarController::describeCalls(const QList<ToolCall> &calls)
{
    QStringList sentences;
    for (const ToolCall &call : calls) {
        QStringList parts;
        for (auto it = call.arguments.constBegin(); it != call.arguments.constEnd() && parts.size() < 3; ++it) {
            const QString key = humanise(it.key()).toLower();
            const QJsonValue v = it.value();
            if (v.isDouble())
                parts.append(key + QLatin1Char(' ') + QLocale::c().toString(v.toDouble(), 'g', 6));
            else if (v.isBool())
                parts.append(v.toBool() ? key : QStringLiteral("no ") + key);
            else if (v.isString() && !v.toString().isEmpty() && !looksLikeId(v.toString()))
                parts.append(key + QStringLiteral(" ") + elided(v.toString(), 30));
        }
        QString sentence = humanise(call.name);
        if (!parts.isEmpty())
            sentence += QStringLiteral(" (") + parts.join(QStringLiteral(", ")) + QLatin1Char(')');
        sentences.append(sentence);
    }
    return elided(sentences.join(QStringLiteral("; ")), 160);
}

void CommandBarController::logCalls(const QJsonArray &ops, int okCount, const QString &error, qint64 ms)
{
    for (int i = 0; i < ops.size(); ++i) {
        const QJsonObject op = ops.at(i).toObject();
        const bool ok = i < okCount;
        m_app->mcp()->logLocalCall(QString::fromLatin1(kLogSource), op.value(QStringLiteral("tool")).toString(),
                                   op.value(QStringLiteral("args")).toObject(), ok, ok ? QString() : error, ms);
        if (!ok)
            break;
    }
}

void CommandBarController::execute(const QString &query, const QList<ToolCall> &calls)
{
    QJsonArray ops;
    bool allReadOnly = true;
    bool anyUnundoable = false;
    for (const ToolCall &call : calls) {
        ops.append(QJsonObject{{QStringLiteral("tool"), call.name}, {QStringLiteral("args"), call.arguments}});
        allReadOnly = allReadOnly && drift::mcp::isReadOnlyOp(call.name);
        anyUnundoable = anyUnundoable || !isUndoStep(call.name);
    }
    const QString summary = describeCalls(calls);

    // The dispatcher's own checks (schema, clips, tracks, assets), with nothing run.
    const QJsonObject dry = m_dispatcher->apply({{QStringLiteral("ops"), ops}, {QStringLiteral("dry_run"), true}});
    if (!dry.value(QStringLiteral("ok")).toBool()) {
        const QString why = problemText(dry);
        logCalls(ops, 0, why, 0);
        toast(QStringLiteral("error"), tr("Couldn't run “%1”: %2").arg(elided(query, 60), why));
        return;
    }

    if (allReadOnly) {
        QStringList lines;
        for (int i = 0; i < ops.size(); ++i) {
            const QJsonObject op = ops.at(i).toObject();
            QElapsedTimer clock;
            clock.start();
            QJsonObject result = m_dispatcher->applyOne(op.value(QStringLiteral("tool")).toString(),
                                                        op.value(QStringLiteral("args")).toObject());
            const bool ok = result.value(QStringLiteral("ok")).toBool(true);
            m_app->mcp()->logLocalCall(QString::fromLatin1(kLogSource), op.value(QStringLiteral("tool")).toString(),
                                       op.value(QStringLiteral("args")).toObject(), ok,
                                       ok ? QString() : problemText(result), clock.elapsed());
            result.remove(QStringLiteral("ok"));
            lines.append(humanise(op.value(QStringLiteral("tool")).toString()) + QStringLiteral(": ")
                         + QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact)));
        }
        toast(QStringLiteral("info"), elided(lines.join(QLatin1Char('\n')), 300));
        return;
    }

    if (anyUnundoable) {
        m_pending = ops;
        m_pendingSummary = summary;
        toast(QStringLiteral("warning"), tr("%1 can't be undone, so it was not run.").arg(summary),
              QStringLiteral("run"));
        return;
    }

    QString error;
    if (!applyBatch(ops, true, &error)) {
        toast(QStringLiteral("error"), tr("Couldn't run “%1”: %2").arg(elided(query, 60), error));
        return;
    }
    toast(QStringLiteral("success"), tr("Done: %1").arg(summary), QStringLiteral("undo"));
}

// One undo step: the batch is the dispatcher's own, atomic when every op may roll back with the
// project, otherwise the same batch without the rollback.
bool CommandBarController::applyBatch(const QJsonArray &ops, bool atomic, QString *error)
{
    QElapsedTimer clock;
    clock.start();
    QJsonObject reply = m_dispatcher->apply({{QStringLiteral("ops"), ops}, {QStringLiteral("atomic"), atomic}});
    if (atomic && !reply.value(QStringLiteral("ok")).toBool()
        && reply.value(QStringLiteral("detail")).toString().startsWith(QLatin1String("atomic:")))
        reply = m_dispatcher->apply({{QStringLiteral("ops"), ops}, {QStringLiteral("atomic"), false}});
    if (reply.value(QStringLiteral("ok")).toBool()) {
        logCalls(ops, ops.size(), {}, clock.elapsed());
        return true;
    }
    *error = problemText(reply);
    const bool rolledBack = reply.value(QStringLiteral("rolled_back")).toBool();
    logCalls(ops, rolledBack ? 0 : reply.value(QStringLiteral("stopped")).toInt(0), *error, clock.elapsed());
    return false;
}

void CommandBarController::runPending()
{
    if (m_pending.isEmpty())
        return;
    const QJsonArray ops = std::exchange(m_pending, {});
    const QString summary = m_pendingSummary;
    QString error;
    if (!applyBatch(ops, false, &error)) {
        toast(QStringLiteral("error"), tr("Couldn't run: %1").arg(error));
        return;
    }
    const bool anyUndo = std::any_of(ops.cbegin(), ops.cend(), [](const QJsonValue &v) {
        return isUndoStep(v.toObject().value(QStringLiteral("tool")).toString());
    });
    toast(QStringLiteral("success"), tr("Done: %1").arg(summary), anyUndo ? QStringLiteral("undo") : QString());
}

#include "moc_CommandBarController.cpp"
