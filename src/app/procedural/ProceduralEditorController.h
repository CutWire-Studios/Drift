#pragma once

#include "app/procedural/ProceduralAuthoringService.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <optional>

class AppController;

// Backend of the Procedural workspace (code editor, console), reached from QML as ProceduralEditor.
// One package is open at a time; the drafts live here so they survive panels being destroyed and
// recreated. Everything goes through ProceduralAuthoringService, so what Apply does is what the
// MCP tools do.
class ProceduralEditorController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString packageId READ packageId NOTIFY packageChanged)
    Q_PROPERTY(QString packageName READ packageName NOTIFY packageChanged)
    // Bundled and addon packages open for reading only; Duplicate makes an editable copy.
    Q_PROPERTY(bool readOnly READ readOnly NOTIFY packageChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
    // procedural.json, main.lua, then the shaders by name.
    Q_PROPERTY(QStringList fileNames READ fileNames NOTIFY packageChanged)
    // Output of the last Apply / Check: [{level: "error"|"warning"|"log"|"shader"|"info", text}].
    Q_PROPERTY(QVariantList consoleLines READ consoleLines NOTIFY consoleChanged)

public:
    explicit ProceduralEditorController(AppController *app, QObject *parent = nullptr);

    QString packageId() const { return m_id; }
    QString packageName() const { return m_name; }
    bool readOnly() const { return !m_id.isEmpty() && !m_user; }
    bool dirty() const;
    QStringList fileNames() const;
    QVariantList consoleLines() const { return m_console; }

    // Loads a package's source into the drafts; false (and a console line) when it cannot be read.
    Q_INVOKABLE bool open(const QString &id);
    Q_INVOKABLE void close();
    Q_INVOKABLE QString text(const QString &file) const;
    Q_INVOKABLE void setText(const QString &file, const QString &text);
    Q_INVOKABLE QString languageFor(const QString &file) const;
    // Drops the edits and reloads the package from disk.
    Q_INVOKABLE void revert();
    // Writes the drafts through ProceduralAuthoringService::update, reloads the catalog and refreshes the preview.
    Q_INVOKABLE bool apply();
    // The same validation without installing anything.
    Q_INVOKABLE bool check();
    // Copies the open package to a new user.<slug> and opens the copy; returns its id, or empty.
    Q_INVOKABLE QString duplicate();
    // A minimal valid package (one sphere, one shader pair); returns its id, or empty.
    Q_INVOKABLE QString createNew(const QString &name);
    // Adds <name>.vert and <name>.frag drafts; they are written on Apply.
    Q_INVOKABLE bool addShaderPair(const QString &name);
    Q_INVOKABLE void clearConsole();
    // The last evaluation of a clip: {known, packageId, error, errors[], log[], serial, shaders[{name, ok, log}]}.
    Q_INVOKABLE QVariantMap live(const QString &clipId) const;

signals:
    void packageChanged();
    void dirtyChanged();
    void consoleChanged();
    // The catalog was rewritten; package lists should be fetched again.
    void catalogChanged();
    // Apply or Create succeeded.
    void applied(const QString &id);

private:
    void report(const QString &title, const drift::ProceduralAuthoringService::Result &result);
    bool loadSource(const QString &id);
    std::optional<QJsonObject> draftManifest(bool *ok);
    void log(const QString &level, const QString &text);
    void afterCatalogChange();
    QSize canvas() const;

    AppController *m_app = nullptr;
    QString m_id;
    QString m_name;
    bool m_user = false;
    QMap<QString, QString> m_original;
    QMap<QString, QString> m_drafts;
    QVariantList m_console;
};
