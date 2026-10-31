#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

// The Keyframes panel's selected keys, addressed by {prop, seconds}. Two refs are the same key
// when they share a property and round to the same microsecond.
class KeyframeSelection : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int count READ count NOTIFY revisionChanged)
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

public:
    explicit KeyframeSelection(QObject *parent = nullptr);

    int count() const { return m_refs.size(); }
    int revision() const { return m_revision; }

    Q_INVOKABLE static QString keyId(const QString &prop, double seconds);

    Q_INVOKABLE void replace(const QVariantList &refs);
    Q_INVOKABLE void set(const QVariantList &refs) { replace(refs); }
    Q_INVOKABLE void add(const QVariantList &refs);
    Q_INVOKABLE void toggle(const QVariantMap &ref);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void remove(const QVariantList &refs);
    // True when every ref is selected, and there is at least one.
    Q_INVOKABLE bool hasAll(const QVariantList &refs) const;
    // Applies the selection rules of pressing a key. Returns 0 when the press only deselected
    // (no drag follows), 1 to start a drag, 2 to drag but collapse to the pressed key if no drag
    // happens.
    Q_INVOKABLE int press(int modifiers, const QVariantList &hitRefs);
    Q_INVOKABLE QVariantList refs() const;
    Q_INVOKABLE bool has(const QString &prop, double seconds) const;
    Q_INVOKABLE QVariant span() const;
    Q_INVOKABLE QStringList props() const;

signals:
    void revisionChanged();

private:
    struct Ref
    {
        QString prop;
        double seconds = 0;
    };

    void assign(const QList<Ref> &next);
    static QList<Ref> parse(const QVariantList &refs);

    QList<Ref> m_refs;
    QHash<QString, int> m_index;
    int m_revision = 0;
};
