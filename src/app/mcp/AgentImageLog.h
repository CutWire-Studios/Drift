#pragma once

#include <QAbstractListModel>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QUrl>
#include <QVariantList>

// Every image an MCP tool handed back this session, newest first, so the user can see what the
// agent was shown. Inline images are written to a per-session cache folder; a tool that wrote
// its own file ("return":"path") is referenced where it is. Never saved in the project.
class AgentImageLog : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        EntryIdRole = Qt::UserRole + 1,
        CallIdRole,
        ToolRole,
        ArgsRole,
        TimeRole,
        PathRole,
        UrlRole,
        WidthRole,
        HeightRole,
        TimesRole,
        RangeStartRole,
        RangeEndRole,
    };

    explicit AgentImageLog(QObject *parent = nullptr);
    ~AgentImageLog() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Scans an MCP tool result for image content or a returned image path.
    void recordResult(int callId, const QString &tool, const QJsonObject &args, const QJsonObject &result);
    QString sessionDir() const;

    Q_INVOKABLE int entryForCall(int callId) const;
    Q_INVOKABLE QVariantMap entry(int entryId) const;
    Q_INVOKABLE void copyPath(const QString &path) const;
    Q_INVOKABLE bool saveCopy(const QString &path, const QUrl &destination) const;
    Q_INVOKABLE QUrl folderUrl(const QString &path) const;

signals:
    void countChanged();

private:
    struct Entry
    {
        int id = 0;
        int callId = 0;
        QString tool;
        QString args;
        qint64 time = 0;
        QString path;
        int width = 0;
        int height = 0;
        QList<double> times;
        double rangeStart = -1.0;
        double rangeEnd = -1.0;
        bool owned = false;
    };

    void add(Entry entry);
    QVariantMap toMap(const Entry &entry) const;

    QList<Entry> m_entries;
    int m_nextId = 1;
    mutable QString m_dir;
};
