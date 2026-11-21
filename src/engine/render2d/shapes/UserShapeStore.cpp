#include "engine/render2d/shapes/UserShapeStore.h"

#include "core/shapes/ShapePath.h"
#include "engine/render2d/shapes/SvgShapeImport.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <algorithm>

namespace drift {

namespace {

const QString kIdPrefix = QStringLiteral("user:");
constexpr double kBoxHeight = 100.0;

QString filePathFor(const QString &id)
{
    return QDir(UserShapeStore::dir()).filePath(id.mid(kIdPrefix.size()) + QStringLiteral(".svg"));
}

QString slug(const QString &name)
{
    QString out = name.trimmed().toLower();
    out.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    out = out.mid(0, 48);
    while (out.startsWith(u'-'))
        out.remove(0, 1);
    while (out.endsWith(u'-'))
        out.chop(1);
    return out.isEmpty() ? QStringLiteral("shape") : out;
}

// Our own files carry the name and the box; anything else is named after its file.
struct Header
{
    QString name;
    double aspect = 0.0;
};

Header readHeader(const QByteArray &svg)
{
    Header header;
    QXmlStreamReader xml(svg);
    while (!xml.atEnd()) {
        if (xml.readNext() != QXmlStreamReader::StartElement)
            continue;
        header.name = xml.attributes().value(QStringLiteral("data-drift-name")).toString();
        const QStringList box = xml.attributes().value(QStringLiteral("viewBox")).toString().split(u' ', Qt::SkipEmptyParts);
        if (!header.name.isEmpty() && box.size() == 4 && box.at(3).toDouble() > 0.0)
            header.aspect = box.at(2).toDouble() / box.at(3).toDouble();
        break;
    }
    return header;
}

} // namespace

UserShapeStore &UserShapeStore::instance()
{
    static UserShapeStore store;
    return store;
}

QString UserShapeStore::dir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base.isEmpty() ? QString() : QDir(base).filePath(QStringLiteral("shapes"));
}

void UserShapeStore::ensureLoaded() const
{
    if (m_loaded)
        return;
    m_loaded = true;
    m_shapes.clear();
    const QString folder = dir();
    if (folder.isEmpty())
        return;

    const QFileInfoList files = QDir(folder).entryInfoList({QStringLiteral("*.svg")}, QDir::Files);
    for (const QFileInfo &info : files) {
        QFile file(info.filePath());
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QByteArray svg = file.readAll();
        UserShape shape;
        if (!importSvgShapeData(svg, &shape.contours, &shape.fillRule, &shape.aspect, nullptr))
            continue;
        const Header header = readHeader(svg);
        shape.id = kIdPrefix + info.completeBaseName();
        shape.name = header.name.isEmpty() ? info.completeBaseName() : header.name;
        if (header.aspect > 0.0)
            shape.aspect = header.aspect;
        shape.revision = m_nextRevision++;
        m_shapes.append(shape);
    }
    std::sort(m_shapes.begin(), m_shapes.end(), [](const UserShape &a, const UserShape &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
}

bool UserShapeStore::writeFile(const UserShape &shape) const
{
    const QString folder = dir();
    if (folder.isEmpty() || !QDir().mkpath(folder))
        return false;

    const double w = kBoxHeight * shape.aspect;
    QSaveFile file(filePathFor(shape.id));
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QXmlStreamWriter xml(&file);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeStartElement(QStringLiteral("svg"));
    xml.writeAttribute(QStringLiteral("xmlns"), QStringLiteral("http://www.w3.org/2000/svg"));
    xml.writeAttribute(QStringLiteral("viewBox"),
                       QStringLiteral("0 0 %1 %2").arg(QString::number(w, 'g', 7), QString::number(kBoxHeight)));
    xml.writeAttribute(QStringLiteral("data-drift-name"), shape.name);
    xml.writeEmptyElement(QStringLiteral("path"));
    xml.writeAttribute(QStringLiteral("d"), contoursToSvgPath(shape.contours, QSizeF(w, kBoxHeight)));
    xml.writeAttribute(QStringLiteral("fill"), QStringLiteral("#000000"));
    xml.writeAttribute(QStringLiteral("fill-rule"),
                       shape.fillRule == Qt::OddEvenFill ? QStringLiteral("evenodd") : QStringLiteral("nonzero"));
    xml.writeEndElement();
    xml.writeEndDocument();
    return file.commit();
}

QList<UserShape> UserShapeStore::all() const
{
    QMutexLocker locker(&m_mutex);
    ensureLoaded();
    return m_shapes;
}

std::optional<UserShape> UserShapeStore::find(const QString &id) const
{
    QMutexLocker locker(&m_mutex);
    ensureLoaded();
    for (const UserShape &shape : m_shapes) {
        if (shape.id == id)
            return shape;
    }
    return std::nullopt;
}

QString UserShapeStore::add(const QString &name, const QVector<ShapeContour> &contours, Qt::FillRule rule,
                            double aspect)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || contours.isEmpty() || aspect <= 0.0)
        return {};

    QMutexLocker locker(&m_mutex);
    ensureLoaded();

    UserShape shape;
    const QString stem = slug(trimmed);
    QString candidate = stem;
    for (int n = 2; QFileInfo::exists(filePathFor(kIdPrefix + candidate)); ++n)
        candidate = QStringLiteral("%1-%2").arg(stem).arg(n);
    shape.id = kIdPrefix + candidate;
    shape.name = trimmed;
    shape.contours = contours;
    shape.fillRule = rule;
    shape.aspect = aspect;
    shape.revision = m_nextRevision++;
    if (!writeFile(shape))
        return {};
    m_shapes.append(shape);
    std::sort(m_shapes.begin(), m_shapes.end(), [](const UserShape &a, const UserShape &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    return shape.id;
}

QString UserShapeStore::addFromSvgFile(const QString &path, QString *error)
{
    UserShape shape;
    if (!importSvgShape(path, &shape.contours, &shape.fillRule, &shape.aspect, error))
        return {};
    const QString id = add(QFileInfo(path).completeBaseName(), shape.contours, shape.fillRule, shape.aspect);
    if (id.isEmpty() && error)
        *error = QStringLiteral("could not save to the shape library");
    return id;
}

bool UserShapeStore::rename(const QString &id, const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return false;

    QMutexLocker locker(&m_mutex);
    ensureLoaded();
    for (UserShape &shape : m_shapes) {
        if (shape.id != id)
            continue;
        const QString before = shape.name;
        shape.name = trimmed;
        if (!writeFile(shape)) {
            shape.name = before;
            return false;
        }
        std::sort(m_shapes.begin(), m_shapes.end(), [](const UserShape &a, const UserShape &b) {
            return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
        });
        return true;
    }
    return false;
}

bool UserShapeStore::remove(const QString &id)
{
    QMutexLocker locker(&m_mutex);
    ensureLoaded();
    for (int i = 0; i < m_shapes.size(); ++i) {
        if (m_shapes.at(i).id != id)
            continue;
        if (!QFile::remove(filePathFor(id)))
            return false;
        m_shapes.removeAt(i);
        return true;
    }
    return false;
}

void UserShapeStore::reload()
{
    QMutexLocker locker(&m_mutex);
    m_loaded = false;
}

} // namespace drift
