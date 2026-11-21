#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QVector>

// Ctrl+K results. Rows come from the live editor, workspace, addon and theme objects, which
// are held as plain QObject so this class does not link any of them, plus the static
// search-index.json for settings and inspector controls.
class SearchIndex : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        KindRole = Qt::UserRole + 1,
        LabelRole,
        SubRole,
        HintRole,
        ShortcutRole,
        EnabledRole,
        TargetRole,
    };

    explicit SearchIndex(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setSources(QObject *editor, QObject *workspaces, QObject *addons, QObject *themes);
    void loadStaticJson(const QByteArray &json);

    // Both arguments are lowercased by the caller.
    static int score(const QString &haystackLower, const QString &queryLower);

    Q_INVOKABLE void rebuild();
    Q_INVOKABLE void setQuery(const QString &query);
    // Ask mode: an "Ask" row for the typed text, then the suggested commands that contain it.
    Q_INVOKABLE void setAskQuery(const QString &text);
    Q_INVOKABLE bool activate(int row);

signals:
    void countChanged();
    void navigateRequested(const QString &target);

private:
    struct Row
    {
        QString kind;
        QString label;
        QString sub;
        QString hint;
        QString shortcut;
        QString target;
        bool enabled = false;
    };

    struct Entry
    {
        Row row;
        QString lowerLabel;
        QString lowerKeywords;
        QString lowerSub;
        QString lowerAll;
    };

    void addRow(Row row, const QString &keywords);
    void addActions();
    void addCommands();
    void addWorkspaces();
    void addPanels();
    void addEditorCatalogs();
    void addAddons();
    void addStatic();
    bool actionEnabled(const QString &id) const;
    void runApply(const QString &payload);
    // m_visible holds -1 - k for m_extra[k].
    const Row &rowAt(int visibleIndex) const
    {
        const int i = m_visible.at(visibleIndex);
        return i < 0 ? m_extra.at(-1 - i) : m_entries.at(i).row;
    }
    void showOnly(const QVector<Row> &rows);
    bool commandBarEnabled() const;
    Row askRow(const QString &text) const;
    QString panelLabel(const QString &type) const;

    QObject *m_editor = nullptr;
    QObject *m_workspaces = nullptr;
    QObject *m_addons = nullptr;
    QObject *m_themes = nullptr;

    QJsonObject m_static;
    bool m_staticLoaded = false;
    QHash<QString, QString> m_panelLabels;

    QVector<Entry> m_entries;
    QVector<int> m_visible; // index into m_entries, or -1 - k for m_extra[k]
    QVector<Row> m_extra;   // ask and suggestion rows
    QString m_query;
};
