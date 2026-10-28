#pragma once

#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QString>

// Gives a QQuickItem a drawn cursor (crosshair plus a small badge for the tool) in place of a
// Qt::CursorShape ("busy" is the wait cursor). An empty shape restores the item's default cursor. Shapes: rect, ellipse,
// polygon, polygonClose, brush, aiInclude, aiExclude, trackPoint, trackPlanar.
class ToolCursor : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QQuickItem *target READ target WRITE setTarget NOTIFY targetChanged)
    Q_PROPERTY(QString shape READ shape WRITE setShape NOTIFY shapeChanged)

public:
    explicit ToolCursor(QObject *parent = nullptr);
    ~ToolCursor() override;

    QQuickItem *target() const { return m_target; }
    void setTarget(QQuickItem *target);
    QString shape() const { return m_shape; }
    void setShape(const QString &shape);

signals:
    void targetChanged();
    void shapeChanged();

private:
    void apply();

    QPointer<QQuickItem> m_target;
    QString m_shape;
    bool m_applied = false;
};
