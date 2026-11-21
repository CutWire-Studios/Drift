#pragma once

#include "core/shapes/ShapeStyle.h"

#include <QList>
#include <QMutex>
#include <QString>

#include <optional>

namespace drift {

struct UserShape
{
    QString id; // "user:<file stem>"
    QString name;
    QVector<ShapeContour> contours; // normalized to the shape's box
    Qt::FillRule fillRule = Qt::WindingFill;
    double aspect = 1.0; // box width / height
    // Changes whenever the geometry under this id could have, so a thumbnail cached against it
    // can be told from a deleted-and-recreated shape of the same name.
    int revision = 0;
};

// The "My shapes" library: one .svg per shape under AppDataLocation/shapes. A file is a plain
// SVG with a viewBox the size of the box and one <path>, so it opens elsewhere; the name rides in
// a data-drift-name attribute and open contours are the sub-paths that end without a Z.
class UserShapeStore
{
public:
    static UserShapeStore &instance();
    static QString dir();
    static bool isUserId(const QString &id) { return id.startsWith(QLatin1String("user:")); }

    // By value: the QML list and the thumbnail provider's thread both read while the GUI thread
    // saves and deletes.
    QList<UserShape> all() const;
    std::optional<UserShape> find(const QString &id) const;

    // Returns the new id, or an empty string when nothing could be written.
    QString add(const QString &name, const QVector<ShapeContour> &contours, Qt::FillRule rule, double aspect);
    // Normalizes an .svg through importSvgShape and saves it, named after the file.
    QString addFromSvgFile(const QString &path, QString *error);
    bool rename(const QString &id, const QString &name);
    bool remove(const QString &id);

    // Test seam: drops the in-memory copy so the next read re-reads the folder.
    void reload();

private:
    UserShapeStore() = default;

    void ensureLoaded() const; // call with m_mutex held
    bool writeFile(const UserShape &shape) const;

    mutable QMutex m_mutex;
    mutable QList<UserShape> m_shapes;
    mutable bool m_loaded = false;
    mutable int m_nextRevision = 1;
};

} // namespace drift
