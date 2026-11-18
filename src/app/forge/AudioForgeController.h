#pragma once

#include "app/forge/ForgePreview.h"
#include "app/forge/audio/Rack.h"
#include "app/forge/compiler/PackageFiles.h"
#include "app/forge/doc/ForgeDoc.h"
#include "ui/nodes/NodeGraphController.h"

#include <QElapsedTimer>
#include <QJsonObject>
#include <QMap>
#include <QTimer>
#include <QUndoStack>
#include <QUrl>

class AppController;
class WorkspaceStore;

// Backend of the Audio Forge workspace, reached from QML as AudioForge. It owns the pedalboard
// document being authored and its own undo history. While the workspace is current, every change is
// staged as the draft audio effect and put on the selected clip: knob drags only change the clip's
// effect parameters, so the sound follows without rebuilding the graph.
class AudioForgeController : public NodeGraphController
{
    Q_OBJECT

    Q_PROPERTY(QString kind READ kind CONSTANT)
    Q_PROPERTY(QString docName READ docName NOTIFY metaChanged)
    Q_PROPERTY(QString docId READ docId NOTIFY metaChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
    Q_PROPERTY(QVariantMap meta READ meta NOTIFY metaChanged)
    Q_PROPERTY(QStringList categories READ categories CONSTANT)
    Q_PROPERTY(QVariantList params READ params NOTIFY paramsChanged)
    Q_PROPERTY(QVariantMap previewValues READ previewValues NOTIFY previewValuesChanged)
    Q_PROPERTY(QStringList presetNames READ presetNames NOTIFY paramsChanged)
    Q_PROPERTY(QStringList validation READ validation NOTIFY validationChanged)
    Q_PROPERTY(bool valid READ valid NOTIFY validationChanged)
    Q_PROPERTY(QString targetDescription READ targetDescription NOTIFY targetChanged)
    Q_PROPERTY(bool hasTarget READ hasTarget NOTIFY targetChanged)
    Q_PROPERTY(bool followSelection READ followSelection WRITE setFollowSelection NOTIFY targetChanged)
    Q_PROPERTY(bool looping READ looping NOTIFY loopingChanged)
    Q_PROPERTY(QVariantList recentProjects READ recentProjects NOTIFY recentChanged)

public:
    AudioForgeController(AppController *app, WorkspaceStore *workspaces, QObject *parent = nullptr);
    ~AudioForgeController() override;

    QString kind() const { return QStringLiteral("audio"); }
    QString docName() const { return m_doc.meta.displayName; }
    QString docId() const { return m_doc.meta.id; }
    bool dirty() const { return m_dirty; }
    QVariantMap meta() const;
    QStringList categories() const;
    QVariantList params() const;
    QVariantMap previewValues() const;
    QStringList presetNames() const;
    QStringList validation() const { return m_problems + m_stageErrors; }
    bool valid() const { return m_problems.isEmpty() && m_stageErrors.isEmpty(); }
    QString targetDescription() const { return m_targetDescription; }
    bool hasTarget() const { return !m_targetClip.isEmpty(); }
    bool followSelection() const { return m_follow; }
    void setFollowSelection(bool on);
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

    // Everything the Node tab shows about one node: {id, kind, title, deletable, inputs, bypass, ir,
    // irOptions, lanes, mode, steps, source, routes, ...}. Empty for an unknown node.
    Q_INVOKABLE QVariantMap nodeInfo(const QString &id) const;
    Q_INVOKABLE void setBypass(const QString &node, bool on);
    Q_INVOKABLE void exposeInput(const QString &node, const QString &input);
    Q_INVOKABLE void unexposeInput(const QString &node, const QString &input);
    Q_INVOKABLE void addLane(const QString &split);
    Q_INVOKABLE void removeLane(const QString &split, const QString &lane);
    Q_INVOKABLE void setLaneGain(const QString &split, const QString &lane, double gain, bool coalesce);
    Q_INVOKABLE void setSteps(const QString &modulator, const QVariantList &steps, bool coalesce);
    Q_INVOKABLE void setRouteDepth(const QString &route, double depth, bool coalesce);
    Q_INVOKABLE void removeRoute(const QString &route);
    // Adds the recording as the pedal's impulse response. Empty on success, otherwise why it was refused.
    Q_INVOKABLE QString importIr(const QString &node, const QUrl &file);

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

    Q_INVOKABLE void newDoc();
    // Starts a fresh board and switches to the Audio Forge workspace.
    Q_INVOKABLE void createNew();
    // Replies {ok, id, error, code}; code "exists" asks the caller to confirm and call again with overwrite.
    Q_INVOKABLE QVariantMap install(bool overwrite);
    Q_INVOKABLE QVariantMap exportDriftfx(const QUrl &path);
    Q_INVOKABLE QVariantMap exportFolder(const QUrl &dir);
    // Opens an installed user audio effect that carries its forge.json as a new project, keeping its id.
    Q_INVOKABLE bool openPackage(const QString &kind, const QString &id);
    Q_INVOKABLE bool canEdit(const QString &kind, const QString &id);
    Q_INVOKABLE bool open(const QString &pathOrId);
    Q_INVOKABLE bool save();
    Q_INVOKABLE void refreshRecent();

    Q_INVOKABLE void useSelection();
    Q_INVOKABLE void playLoop();
    Q_INVOKABLE void stopLoop();

signals:
    void metaChanged();
    void dirtyChanged();
    void paramsChanged();
    void previewValuesChanged();
    void validationChanged();
    void targetChanged();
    void loopingChanged();
    void recentChanged();
    void addNodeRequested(const QString &type);
    void message(const QString &text);

protected:
    QList<CatalogEntry> catalog() const override;

private:
    void commit(const drift::forge::ForgeDoc &next, const QString &text, const QString &key = {}, bool structural = true);
    void applyDoc(const drift::forge::ForgeDoc &doc);
    QString burstKey(const QString &base);
    void replaceDoc(const drift::forge::ForgeDoc &doc);
    void refresh(bool republish);
    void scheduleSave();
    QString projectDir(const QString &id) const;
    bool buildPackage(drift::forge::PackageContents *out, QString *error);
    QString stageForExport(const drift::forge::PackageContents &contents, const QString &dir) const;
    void activateWorkspace();

    void setActive(bool active);
    QString structureKey() const;
    void schedulePreview();
    void stageNow();
    void applyPatch();
    void clearPreview();
    QMap<QString, QVariant> patchValues() const;
    void setTarget(const QString &clipId, drift::TimeUs start, drift::TimeUs end, const QString &description);

    AppController *m_app;
    WorkspaceStore *m_workspaces;
    drift::forge::ForgeDoc m_doc;
    QUndoStack m_undo;
    bool m_dirty = false;
    bool m_republish = true;
    int m_burst = 0;
    int m_moveSerial = 0;
    QString m_lastBurstBase;
    QElapsedTimer m_burstClock;
    QStringList m_clipboard;

    QStringList m_problems;
    QStringList m_stageErrors;
    QJsonObject m_hoisted;
    QJsonObject m_manifest;
    QMap<QString, QJsonValue> m_values;

    bool m_active = false;
    QString m_stagedKey;
    QString m_pendingKey;
    QTimer m_stageTimer;
    QTimer m_saveTimer;
    QTimer m_selectionTimer;
    bool m_follow = true;
    QString m_targetClip;
    QString m_targetDescription;
    drift::TimeUs m_targetStart = 0;
    drift::TimeUs m_targetEnd = 0;
    bool m_looping = false;
    QVariantList m_recent;
    QString m_savedId;
    QHash<QString, bool> m_canEdit;
};
