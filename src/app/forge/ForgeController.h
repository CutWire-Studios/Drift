#pragma once

#include "app/forge/ForgeGraphMapping.h"
#include "app/forge/ForgePreview.h"
#include "app/forge/compiler/Compiler.h"
#include "app/forge/compiler/PackageFiles.h"
#include "app/forge/doc/ForgeDoc.h"
#include "ui/nodes/NodeGraphController.h"

#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QUrl>
#include <QPointF>
#include <QTimer>
#include <QUndoStack>

class AppController;
class WorkspaceStore;

// Backend of the Forge workspace, reached from QML as Forge. It owns the document being authored
// and its own undo history, which never touches the project's. While the Forge workspace is
// current it compiles the graph after every change, stages it as the draft package and patches
// the selected clip (or a built-in sample) so the preview shows the result live.
class ForgeController : public NodeGraphController
{
    Q_OBJECT

    Q_PROPERTY(QString kind READ kind WRITE setKind NOTIFY kindChanged)
    Q_PROPERTY(QString docName READ docName NOTIFY metaChanged)
    Q_PROPERTY(QString docId READ docId NOTIFY metaChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
    Q_PROPERTY(QVariantMap meta READ meta NOTIFY metaChanged)
    Q_PROPERTY(QStringList categories READ categories NOTIFY kindChanged)
    Q_PROPERTY(QVariantList params READ params NOTIFY paramsChanged)
    Q_PROPERTY(QVariantMap previewValues READ previewValues NOTIFY previewValuesChanged)
    Q_PROPERTY(QStringList presetNames READ presetNames NOTIFY paramsChanged)
    Q_PROPERTY(QVariantList passes READ passes NOTIFY compiled)
    Q_PROPERTY(QVariantList compileErrors READ compileErrors NOTIFY compiled)
    Q_PROPERTY(bool compileOk READ compileOk NOTIFY compiled)
    Q_PROPERTY(QVariantMap previewTarget READ previewTarget NOTIFY previewTargetChanged)
    Q_PROPERTY(QString targetDescription READ targetDescription NOTIFY targetChanged)
    Q_PROPERTY(bool hasTarget READ hasTarget NOTIFY targetChanged)
    Q_PROPERTY(bool followSelection READ followSelection WRITE setFollowSelection NOTIFY targetChanged)
    Q_PROPERTY(QString animDirection READ animDirection WRITE setAnimDirection NOTIFY animChanged)
    Q_PROPERTY(int animDurationMs READ animDurationMs WRITE setAnimDurationMs NOTIFY animChanged)
    Q_PROPERTY(bool looping READ looping NOTIFY loopingChanged)
    Q_PROPERTY(QVariantList recentProjects READ recentProjects NOTIFY recentChanged)

public:
    ForgeController(AppController *app, WorkspaceStore *workspaces, QObject *parent = nullptr);
    ~ForgeController() override;

    QString kind() const;
    void setKind(const QString &kind);
    QString docName() const { return m_doc.meta.displayName; }
    QString docId() const { return m_doc.meta.id; }
    bool dirty() const { return m_dirty; }
    QVariantMap meta() const;
    QStringList categories() const;
    QVariantList params() const;
    QVariantMap previewValues() const;
    QStringList presetNames() const;
    QVariantList passes() const { return m_passes; }
    QVariantList compileErrors() const;
    bool compileOk() const { return m_result.ok && m_stageErrors.isEmpty(); }
    QVariantMap previewTarget() const;
    QString targetDescription() const { return m_target.description; }
    bool hasTarget() const { return m_target.valid(); }
    bool followSelection() const { return m_follow; }
    void setFollowSelection(bool on);
    QString animDirection() const { return m_animOut ? QStringLiteral("out") : QStringLiteral("in"); }
    void setAnimDirection(const QString &dir);
    int animDurationMs() const { return m_animDurationMs; }
    void setAnimDurationMs(int ms);
    bool looping() const { return m_looping; }
    QVariantList recentProjects() const { return m_recent; }

    bool canUndo() const override { return m_undo.canUndo(); }
    bool canRedo() const override { return m_undo.canRedo(); }
    void moveNodes(const QStringList &ids, qreal dx, qreal dy) override;
    void endMove() override;
    bool canConnect(const QString &fromNode, const QString &fromPort, const QString &toNode,
                    const QString &toPort) const override;
    bool connectPorts(const QString &fromNode, const QString &fromPort, const QString &toNode,
                      const QString &toPort) override;
    void disconnectInput(const QString &node, const QString &port) override;
    void deleteSelection() override;
    void duplicateSelection() override;
    void copySelection() override;
    void paste(const QPointF &at) override;
    void undo() override;
    void redo() override;
    QString addNode(const QString &type, const QPointF &at) override;
    QString addNodeConnected(const QString &type, const QPointF &at, const QString &fromNode,
                             const QString &fromPort, bool fromIsOutput) override;
    void setInputValue(const QString &node, const QString &input, const QVariant &value, bool coalesce) override;
    QVariantList nodeContextActions(const QString &id) const override;
    void triggerNodeAction(const QString &id, const QString &action) override;

    // Palette: the graph panel places the node at the centre of its view.
    Q_INVOKABLE void requestAddNode(const QString &type) { emit addNodeRequested(type); }

    Q_INVOKABLE QVariantMap nodeInfo(const QString &id) const;
    Q_INVOKABLE QVariantList nodeOptions(const QString &id) const;
    Q_INVOKABLE void setNodeOption(const QString &node, const QString &option, const QVariant &value, bool coalesce);
    Q_INVOKABLE void exposeInput(const QString &node, const QString &input);
    Q_INVOKABLE void unexposeInput(const QString &node, const QString &input);
    Q_INVOKABLE void exposeOption(const QString &node, const QString &option);
    Q_INVOKABLE void unexposeOption(const QString &node, const QString &option);

    Q_INVOKABLE void updateParam(const QString &id, const QVariantMap &patch, bool coalesce = false);
    Q_INVOKABLE void moveParam(const QString &id, int delta);
    Q_INVOKABLE void removeParam(const QString &id);
    Q_INVOKABLE void setPreviewValue(const QString &name, const QVariant &value);
    Q_INVOKABLE void resetPreviewValues();
    Q_INVOKABLE void addPreset(const QString &name);
    Q_INVOKABLE void applyPreset(const QString &name);
    Q_INVOKABLE void deletePreset(const QString &name);

    // Empty on success, otherwise why the value was refused.
    Q_INVOKABLE QString setMeta(const QString &key, const QVariant &value);

    Q_INVOKABLE void newDoc(const QString &kind);
    // Starts a document of `kind` and switches to the Forge workspace.
    Q_INVOKABLE void createNew(const QString &kind);
    // Compiles, thumbnails and installs the document as a user package. Replies
    // {ok, id, error, code}; code "exists" asks the caller to confirm and call again with overwrite.
    Q_INVOKABLE QVariantMap install(bool overwrite);
    Q_INVOKABLE QVariantMap exportDriftfx(const QUrl &path);
    Q_INVOKABLE QVariantMap exportFolder(const QUrl &dir);
    // Opens an installed user package that carries its forge.json as a new project, keeping its id.
    Q_INVOKABLE bool openPackage(const QString &kind, const QString &id);
    Q_INVOKABLE bool canEdit(const QString &kind, const QString &id);
    Q_INVOKABLE bool open(const QString &pathOrId);
    Q_INVOKABLE bool save();
    Q_INVOKABLE void refreshRecent();

    Q_INVOKABLE void previewNode(const QString &node, const QString &output);
    Q_INVOKABLE void previewOutput();
    Q_INVOKABLE void useSelection();
    Q_INVOKABLE void playLoop();
    Q_INVOKABLE void stopLoop();

signals:
    void kindChanged();
    void metaChanged();
    void dirtyChanged();
    void paramsChanged();
    void previewValuesChanged();
    void compiled();
    void previewTargetChanged();
    void targetChanged();
    void animChanged();
    void loopingChanged();
    void recentChanged();
    void addNodeRequested(const QString &type);
    void message(const QString &text);

protected:
    QList<CatalogEntry> catalog() const override;
    bool socketsCompatible(const QString &, const QString &) const override { return true; }

private:
    struct Target
    {
        QString clipId;
        QString fromId;
        QString toId;
        QString description;
        // A cut's transition length; for a clip the span it occupies.
        drift::TimeUs durationUs = 0;
        drift::TimeUs clipStart = 0;
        drift::TimeUs clipEnd = 0;
        // The span a transition covers.
        drift::TimeUs windowStart = 0;
        drift::TimeUs windowEnd = 0;
        bool valid() const { return !clipId.isEmpty() || !fromId.isEmpty(); }
    };

    void commit(const drift::forge::ForgeDoc &next, const QString &text, const QString &key = {}, bool structural = true);
    void applyDoc(const drift::forge::ForgeDoc &doc);
    QString burstKey(const QString &base);
    void replaceDoc(const drift::forge::ForgeDoc &doc);
    void refresh(bool recompile);
    void scheduleSave();
    QString projectDir(const QString &id) const;
    void deleteNodes(const QStringList &ids);
    bool buildPackage(drift::forge::PackageContents *out, QString *error);
    QByteArray thumbnailPng(const drift::forge::PackageContents &contents, const drift::forge::CompileResult &compiled);
    QImage frameAt(drift::TimeUs timeUs, int side) const;
    QString stageForExport(const drift::forge::PackageContents &contents, const QString &dir) const;
    void activateWorkspace();

    void setActive(bool active);
    void schedulePreview();
    void stageNow();
    void applyPatch();
    void clearPreview();
    drift::forge::PreviewPatch targetPatch(const Target &t, const QMap<QString, QVariant> &params) const;
    drift::forge::PreviewPatch samplePatch(const QMap<QString, QVariant> &params);
    Target sampleTarget() const;
    QString samplePath(int which);
    void setTarget(const Target &t);
    bool loopRange(const Target &t, drift::TimeUs *start, drift::TimeUs *end) const;

    AppController *m_app;
    WorkspaceStore *m_workspaces;
    drift::forge::ForgeDoc m_doc;
    QUndoStack m_undo;
    bool m_dirty = false;
    bool m_recompile = true;
    int m_burst = 0;
    int m_moveSerial = 0;
    QString m_lastBurstBase;
    QElapsedTimer m_burstClock;
    QStringList m_clipboard;

    drift::forge::CompileResult m_result;
    QVariantList m_passes;
    QStringList m_stageErrors;
    QMap<QString, QJsonValue> m_values;
    std::optional<std::pair<QString, QString>> m_previewTarget;

    bool m_active = false;
    QString m_stagedKey;
    QString m_pendingKey;
    QString m_stagedKind;
    QTimer m_stageTimer;
    QTimer m_saveTimer;
    QTimer m_selectionTimer;
    QTimer m_sampleTimer;
    bool m_follow = true;
    Target m_target;
    bool m_animOut = false;
    int m_animDurationMs = 800;
    bool m_looping = false;
    drift::TimeUs m_sampleCutUs = 0;
    QVariantList m_recent;
    QString m_savedId;
    QHash<QString, bool> m_canEdit;
};
