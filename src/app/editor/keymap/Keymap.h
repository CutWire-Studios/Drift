#pragma once

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVariantList>

namespace drift {

// Preset bindings plus the user's overrides on top, per action and per scope.
//
// A chord may be bound to one action per scope. "global" and each panel scope are separate, so a
// panel can reuse a global chord (the panel wins while it is engaged).
class Keymap : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString presetId READ presetId NOTIFY changed)
    Q_PROPERTY(QVariantList presets READ presets CONSTANT)
    Q_PROPERTY(QVariantList categories READ categories CONSTANT)
    Q_PROPERTY(QVariantList actions READ actions NOTIFY changed)
    // Distinct non-arrow chords across every scope: one Shortcut item each in QML.
    Q_PROPERTY(QStringList chords READ chords NOTIFY changed)
    Q_PROPERTY(bool hasOverrides READ hasOverrides NOTIFY changed)

public:
    explicit Keymap(QObject *parent = nullptr);

    QString presetId() const { return m_presetId; }
    QVariantList presets() const;
    QVariantList categories() const;
    QVariantList actions() const;
    QStringList chords() const { return m_chords; }
    bool hasOverrides() const { return !m_overrides.isEmpty(); }

    Q_INVOKABLE QStringList bindings(const QString &actionId) const;
    Q_INVOKABLE QString scopeOf(const QString &actionId) const;
    Q_INVOKABLE QString labelFor(const QString &actionId) const;
    Q_INVOKABLE bool isModified(const QString &actionId) const;
    Q_INVOKABLE QStringList chordsFor(const QString &scope) const;
    Q_INVOKABLE QString actionFor(const QString &scope, const QString &chord) const;

    // The editing calls return the label of the action that already owns a chord in the same
    // scope when they refuse, and an empty string on success.
    Q_INVOKABLE QString addBinding(const QString &actionId, const QString &chord);
    Q_INVOKABLE QString setBindings(const QString &actionId, const QStringList &chords);
    Q_INVOKABLE void removeBinding(const QString &actionId, const QString &chord);
    Q_INVOKABLE void resetAction(const QString &actionId);
    Q_INVOKABLE void resetAll();
    Q_INVOKABLE void setPreset(const QString &presetId, bool keepOverrides);

    // nativeKey is QKeyEvent::nativeScanCode(), which is all QML's KeyEvent exposes. It is only
    // read on macOS, where it is the virtual key code.
    Q_INVOKABLE QString chordFromKey(int key, int modifiers, int nativeKey = 0,
                                     const QString &text = QString()) const;
    Q_INVOKABLE QString displayText(const QString &chord) const;
    Q_INVOKABLE static QString normalizeChord(const QString &chord);

    static QString chordFromKey(int key, int modifiers, int nativeKey, const QString &text, bool mac);
    static QString displayText(const QString &chord, bool mac);
    static QString macKeyName(int virtualKey);

signals:
    void changed();

private:
    void load();
    void migrateLegacy();
    void rebuild();
    void pruneOverrides();
    void storeOverride(const QString &actionId, const QStringList &chords);
    QStringList presetBindings(const QString &actionId) const;
    void persistPreset() const;

    QString m_presetId;
    QHash<QString, QStringList> m_overrides;
    QHash<QString, QStringList> m_effective;
    QHash<QString, QHash<QString, QString>> m_owners;
    QStringList m_chords;
};

} // namespace drift
