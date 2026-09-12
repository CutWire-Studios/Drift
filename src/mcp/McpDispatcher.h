#pragma once

#include <QJsonObject>
#include <QString>
#include <QVariantList>

class AppController;

namespace drift::mcp {

class McpDispatcher
{
public:
    explicit McpDispatcher(AppController *controller);

    QJsonObject inspect(const QJsonObject &args) const;
    QJsonObject apply(const QJsonObject &args);
    // Validates against the op schema, then runs; appends ignored:[unknown keys] on success.
    QJsonObject applyOne(const QString &tool, const QJsonObject &args);
    QJsonObject applyOneUnchecked(const QString &tool, const QJsonObject &args);
    QJsonObject applyOneExtended(const QString &tool, const QJsonObject &args);
    QJsonObject capture(const QJsonObject &args);
    QJsonObject frames(const QJsonObject &args);
    QJsonObject activity(const QJsonObject &args);

private:
    struct ClipRef {
        int track = -1;
        int clip = -1;
        QString id;
        bool valid() const { return track >= 0 && clip >= 0; }
    };

    ClipRef resolveClip(const QJsonObject &args) const;
    // "video", "audio", "image", "text", "subtitle", "shape", "adjustment", "vector".
    QString clipKind(const ClipRef &ref) const;
    // type_mismatch unless the clip's kind is one of `kinds`; empty when it is.
    QJsonObject requireKind(const ClipRef &ref, const QStringList &kinds, const char *what) const;
    int resolveAsset(const QJsonValue &value) const;
    // transitionKinds() rows carry their id under "kind"; the catalog helpers key on "id".
    QVariantList transitionCatalog() const;
    QJsonObject clipFeedback(const ClipRef &ref, const QJsonObject &extra = {}) const;
    QJsonObject effectHost(const ClipRef &ref, QJsonObject extra) const;
    QJsonObject waitImport(const QStringList &ids);
    bool isUndoable(const QString &tool) const;
    void moveClipToRequested(const ClipRef &ref, double at);

    QJsonObject opImportMedia(const QJsonObject &args);
    QJsonObject opListAssets() const;
    QJsonObject opRenameAsset(const QJsonObject &args);
    QJsonObject opAddTrack(const QJsonObject &args);
    QJsonObject opRemoveTrack(const QJsonObject &args);
    QJsonObject opSetTrack(const QJsonObject &args);
    QJsonObject opPlaceClip(const QJsonObject &args);
    QJsonObject opMoveClip(const QJsonObject &args);
    QJsonObject opSetDuration(const QJsonObject &args);
    QJsonObject opSetTrim(const QJsonObject &args);
    QJsonObject opMoveToTrack(const QJsonObject &args);
    QJsonObject opSplitClip(const QJsonObject &args);
    QJsonObject opDeleteClip(const QJsonObject &args);
    QJsonObject opDuplicateClip(const QJsonObject &args);
    QJsonObject opUndo();
    QJsonObject opRedo();
    QJsonObject opSetOverlap(const QJsonObject &args);
    QJsonObject opSetTransform(const QJsonObject &args);
    QJsonObject opResetTransform(const QJsonObject &args);
    QJsonObject opSeek(const QJsonObject &args);
    QJsonObject opPlay();
    QJsonObject opPause();
    QJsonObject opSetWorkArea(const QJsonObject &args);
    QJsonObject opClearWorkArea();
    QJsonObject opAddText(const QJsonObject &args);
    QJsonObject opSetText(const QJsonObject &args);
    QJsonObject opListEffects(const QJsonObject &args) const;
    QJsonObject opListAudioEffects(const QJsonObject &args) const;
    QJsonObject opListTransitions(const QJsonObject &args) const;
    QJsonObject opAddEffect(const QJsonObject &args);
    QJsonObject opRemoveEffect(const QJsonObject &args);
    QJsonObject opSetEffectParam(const QJsonObject &args);
    QJsonObject opAddAudioEffect(const QJsonObject &args);
    QJsonObject opRemoveAudioEffect(const QJsonObject &args);
    QJsonObject opSetAudioEffectParam(const QJsonObject &args);
    QJsonObject opAddTransition(const QJsonObject &args);
    QJsonObject opRemoveTransition(const QJsonObject &args);
    QJsonObject opSetProjectSetup(const QJsonObject &args);
    QJsonObject opSetBackground(const QJsonObject &args);
    QJsonObject opSetMetadata(const QJsonObject &args);
    QJsonObject opSaveProject(const QJsonObject &args);
    QJsonObject opListExportOptions() const;
    QJsonObject opExportVideo(const QJsonObject &args);
    QJsonObject opExportStatus() const;
    QJsonObject opExport(const QJsonObject &args);
    QJsonObject opListAnimatedProperties(const QJsonObject &args) const;
    QJsonObject opListKeyframes(const QJsonObject &args) const;
    QJsonObject opSetKeyframe(const QJsonObject &args);
    QJsonObject opRemoveKeyframe(const QJsonObject &args);
    QJsonObject opSetKeyframeInterpolation(const QJsonObject &args);
    QJsonObject opSetKeyframeTangents(const QJsonObject &args);
    QJsonObject opSetKeyframeHold(const QJsonObject &args);
    QJsonObject opSetPropertyKeyframesEnabled(const QJsonObject &args);
    QJsonObject opListSpeedCurve(const QJsonObject &args);
    QJsonObject opSetSpeedCurve(const QJsonObject &args);
    QJsonObject opClearSpeedCurve(const QJsonObject &args);
    QJsonObject opSplitOnBeats(const QJsonObject &args);
    QJsonObject opSnapClipsToBeats(const QJsonObject &args);
    QJsonObject opSplitOnScenes(const QJsonObject &args);
    QJsonObject opGetUiPreferences() const;
    QJsonObject opSetTheme(const QJsonObject &args);
    QJsonObject opListShortcuts(const QJsonObject &args) const;
    QJsonObject opSetShortcut(const QJsonObject &args);
    QJsonObject opResetShortcuts();

    static QJsonArray speedPointsToJson(const QVariantList &points);
    static QVariantList speedPointsFromJson(const QJsonArray &points);

    AppController *m_controller = nullptr;
};

} // namespace drift::mcp
