#pragma once

#include "app/color/ScopesController.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

class AppController;
class LookThumbnailService;

// Colour grading and looks, reached from QML as EditorState.color. The grade is one color.grade
// effect on the adjustment that hosts the selected clip's effects; finishing effects sit beside it.
class ColorController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap target READ target NOTIFY targetChanged)
    Q_PROPERTY(bool bypass READ bypass WRITE setBypass NOTIFY bypassChanged)
    Q_PROPERTY(QVariantList looks READ looks NOTIFY looksChanged)
    Q_PROPERTY(int thumbnailSerial READ thumbnailSerial NOTIFY thumbnailSerialChanged)
    Q_PROPERTY(bool thumbnailsActive READ thumbnailsActive WRITE setThumbnailsActive NOTIFY thumbnailsActiveChanged)
    Q_PROPERTY(ScopesController *scopes READ scopes CONSTANT)

public:
    explicit ColorController(AppController *app, QObject *parent = nullptr);

    QVariantMap target() const { return m_target; }
    bool bypass() const { return m_bypass; }
    void setBypass(bool on);
    QVariantList looks() const { return m_looks; }
    int thumbnailSerial() const { return m_thumbnailSerial; }
    bool thumbnailsActive() const { return m_thumbnailsActive; }
    void setThumbnailsActive(bool active);
    ScopesController *scopes() const { return m_scopes; }

    // A drag of grade params is one undo step: beginEdit .. commitEdit. The setters outside an
    // edit wrap themselves in one.
    Q_INVOKABLE void beginEdit(const QString &label);
    Q_INVOKABLE void setParam(const QString &key, double v);
    Q_INVOKABLE void setVec2(const QString &key, double x, double y);
    Q_INVOKABLE void setText(const QString &key, const QString &s);
    Q_INVOKABLE void commitEdit();
    Q_INVOKABLE void cancelEdit();

    Q_INVOKABLE void resetGroup(const QString &group);
    Q_INVOKABLE void resetGroups(const QStringList &groups);
    Q_INVOKABLE void resetHslBand(const QString &band, const QString &label);
    Q_INVOKABLE void resetAll();
    Q_INVOKABLE void setLook(const QString &id);
    Q_INVOKABLE void importLooks(const QList<QUrl> &urls);
    Q_INVOKABLE int clipsUsingLook(const QString &id) const;
    Q_INVOKABLE void removeLook(const QString &id);
    Q_INVOKABLE void renameLook(const QString &id, const QString &name);

    Q_INVOKABLE QVariantList finishingCatalog() const;
    Q_INVOKABLE QVariantList finishingParamSpecs(const QString &id) const;
    Q_INVOKABLE void toggleFinishing(const QString &id, bool on);
    Q_INVOKABLE void setFinishingEnabled(const QString &id, bool enabled);
    Q_INVOKABLE void beginFinishingEdit(const QString &id, const QString &label);
    Q_INVOKABLE void setFinishingParam(const QString &id, const QString &key, double v);
    Q_INVOKABLE void setFinishingColor(const QString &id, const QString &key, const QString &hex);

    Q_INVOKABLE int applyToAll(bool includeFinishing);
    Q_INVOKABLE void gradeWholeTimeline();
    Q_INVOKABLE void enableGrade();
    Q_INVOKABLE void selectTimelineGrade();
    Q_INVOKABLE void requestThumbnails();

    // AddonManager::kindChanged. Looks are the only kind this controller shows.
    void refreshForKind(const QString &kind);

signals:
    void targetChanged();
    void bypassChanged();
    void looksChanged();
    void thumbnailSerialChanged();
    void thumbnailsActiveChanged();

private:
    void refreshTarget();
    void publishTarget();
    void rebuildLooks();
    static void warmLook(const QString &id);
    void applyBypass();
    void resetParams(const QStringList &groups, const QString &undoText);

    bool locate(const QString &clipId, int *trackIndex, int *clipIndex) const;
    QString hostIdFor(const QString &clipId) const;
    QVariantMap buildTarget() const;

    bool editTarget(int *trackIndex, int *clipIndex) const;
    bool editKeyed(const QString &trackKey) const;
    void writeParam(const QString &key, double v);
    void writeVec2(const QString &key, double x, double y);
    void writeText(const QString &key, const QString &value);

    AppController *m_app = nullptr;
    LookThumbnailService *m_thumbs = nullptr;
    ScopesController *m_scopes = nullptr;
    QString m_sourceClipId;
    QVariantMap m_target;
    bool m_targetAnimated = false;
    QVariantList m_looks;
    bool m_bypass = false;
    QString m_bypassedHostId;
    bool m_thumbnailsActive = false;
    int m_thumbnailSerial = 0;
    // The edit in progress: the host adjustment and the effect within it that params write to.
    bool m_editing = false;
    QString m_editHostId;
    QString m_editCatalogId;
    int m_editEffectIndex = -1;
};
