#pragma once

#include "app/surfaces/SurfaceTargets.h"

#include <QHash>
#include <QObject>
#include <QTimer>

#include <functional>

class AppController;

namespace drift {

// The editor's side of ControlSurfaceManager: mixer strips, transport, the selected clip's
// properties and effect parameters, and the colour grade.
class EditorSurfaceTargets : public QObject, public SurfaceTargets {
    Q_OBJECT

public:
    static constexpr int kBankSize = 8;

    explicit EditorSurfaceTargets(AppController *app, QObject *parent = nullptr);

    QList<SurfaceTarget> targets() const override;
    SurfaceTarget describe(const QString &id) const override;
    bool trigger(const QString &id) override;
    void setAbsolute(const QString &id, double normalized) override;
    void nudge(const QString &id, double ticks) override;
    double current(const QString &id) const override;
    QStringList stripLabels() const override;
    QString idForProperty(const QString &key, double min, double max) const override;

private:
    struct Strip {
        int track = -1;
        QString name;
    };
    struct PropSpec {
        QStringList keys;
        double min;
        double max;
    };

    void refreshStrips() const;
    int stripTrack(int slot) const;
    bool beginGesture(const QString &owner, std::function<void()> commit);
    void commitGesture();
    bool selectionValid() const;
    bool resolveEffectKey(const QString &catalogId, const QString &param, QString *propKey) const;
    void applyProp(const QStringList &keys, double value);
    void shuttleTick();

    AppController *m_app;
    mutable quint32 m_stripRevision = 0;
    mutable bool m_stripsBuilt = false;
    mutable QList<Strip> m_strips;
    int m_bank = 0;
    QTimer m_commitTimer;
    QString m_gestureOwner;
    std::function<void()> m_gestureCommit;
    QHash<QString, double> m_gestureLast;
    double m_jogRemainder = 0.0;
    QTimer m_shuttleTimer;
    double m_shuttleRate = 0.0;
    double m_shuttleRemainder = 0.0;
};

} // namespace drift
