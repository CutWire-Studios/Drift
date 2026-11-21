#pragma once

#include "app/commandbar/CommandCatalog.h"
#include "app/commandbar/CommandPrompt.h"
#include "app/commandbar/CommandRetriever.h"

#include <QJsonArray>
#include <QObject>
#include <QString>
#include <QTimer>

#include <memory>

class AppController;

namespace drift::llm {
class LlamaServer;
}

namespace drift::mcp {
class McpDispatcher;
}

// Natural-language commands from the Ctrl+K search, reached from QML as EditorState.commandBar.
// A sentence goes through the retriever and prompt builder to a local llama-server; the tool calls
// it answers with run through the MCP dispatcher, so they are the same ops an agent would call.
//
//   all calls undoable        -> one atomic batch, one undo step, "Done" toast with Undo
//   any call read-only only   -> run, result shown in a toast
//   any call not undoable     -> nothing runs until the user presses Run on the toast
class CommandBarController : public QObject
{
    Q_OBJECT

    // Both the llama-runtime and a command-model add-on are installed.
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit CommandBarController(AppController *app, QObject *parent = nullptr);
    ~CommandBarController() override;

    bool available() const;
    bool busy() const { return m_busy; }
    QString lastError() const { return m_lastError; }

    // Replaces any command still being worked out: its answer is dropped when it arrives.
    Q_INVOKABLE void ask(const QString &text);
    // Starts the model server ahead of the first command; the idle timeout stops it again.
    Q_INVOKABLE void prewarm();
    Q_INVOKABLE void cancel();
    // The "Run" action of a confirmation toast.
    Q_INVOKABLE void runPending();

    // "Split clip (time 12.5)". Public for tests.
    static QString describeCalls(const QList<drift::commandbar::ToolCall> &calls);

signals:
    void availableChanged();
    void busyChanged();
    void lastErrorChanged();
    // severity: info | success | warning | error. action: "" | "undo" | "run".
    void toastRequested(const QString &severity, const QString &message, const QString &action);

private:
    static QString modelPath();
    void setBusy(bool busy);
    void setLastError(const QString &error);
    void toast(const QString &severity, const QString &message, const QString &action = {});
    void finish(const QString &query, const QString &content, const QString &error);
    void execute(const QString &query, const QList<drift::commandbar::ToolCall> &calls);
    bool applyBatch(const QJsonArray &ops, bool atomic, QString *error);
    void logCalls(const QJsonArray &ops, int okCount, const QString &error, qint64 ms);

    AppController *m_app = nullptr;
    drift::llm::LlamaServer *m_server = nullptr;
    std::unique_ptr<drift::mcp::McpDispatcher> m_dispatcher;
    drift::commandbar::CommandCatalog m_catalog;
    std::unique_ptr<drift::commandbar::CommandRetriever> m_retriever;
    QTimer m_timeout;
    QJsonArray m_pending;
    QString m_pendingSummary;
    QString m_lastError;
    int m_generation = 0;
    bool m_busy = false;
    bool m_lastAvailable = false;
};
