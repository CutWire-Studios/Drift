#include "FontCatalog.h"

#include "GpuPackageParse.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>

#include <algorithm>
#include <cstdlib>
#include <limits>

QList<int> FontFamilyEntry::weights() const
{
    QList<int> out;
    for (const FontFace &face : faces) {
        if (!face.italic && !out.contains(face.weight))
            out.append(face.weight);
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool FontFamilyEntry::hasItalic() const
{
    for (const FontFace &face : faces) {
        if (face.italic)
            return true;
    }
    return false;
}

namespace {

QList<FontFamilyEntry> g_catalog;
QHash<QString, int> g_familyIndex; // lowercased family name -> index
QMutex g_mutex;
bool g_initialized = false;

FontFamilyEntry loadPackage(const QString &packageDir)
{
    FontFamilyEntry entry;

    QFile file(QDir(packageDir).filePath(QStringLiteral("family.json")));
    if (!file.open(QIODevice::ReadOnly))
        return entry;

    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    if (root.isEmpty())
        return entry;

    entry.id = root.value(QStringLiteral("id")).toString();
    entry.family = root.value(QStringLiteral("family")).toString();
    entry.category = root.value(QStringLiteral("category")).toString();
    entry.license = root.value(QStringLiteral("license")).toString();
    entry.order = root.value(QStringLiteral("order")).toInt();
    entry.packageDir = packageDir;
    if (entry.family.isEmpty())
        return {};

    const QJsonArray faces = root.value(QStringLiteral("faces")).toArray();
    for (const QJsonValue &value : faces) {
        const QJsonObject object = value.toObject();
        const QString relative = object.value(QStringLiteral("file")).toString();
        const QString absolute = QDir(packageDir).filePath(relative);
        if (relative.isEmpty() || !QFile::exists(absolute))
            continue;

        const int id = QFontDatabase::addApplicationFont(absolute);
        if (id < 0) {
            qWarning("FontCatalog: Qt rejected %s", qPrintable(absolute));
            continue;
        }
        if (entry.qtFamily.isEmpty())
            entry.qtFamily = QFontDatabase::applicationFontFamilies(id).value(0);

        FontFace face;
        face.weight = qBound(100, object.value(QStringLiteral("weight")).toInt(400), 900);
        face.italic = object.value(QStringLiteral("italic")).toBool();
        face.styleName = object.value(QStringLiteral("styleName")).toString();
        face.file = absolute;
        entry.faces.append(face);
    }

    if (entry.faces.isEmpty() || entry.qtFamily.isEmpty())
        return {};

    std::sort(entry.faces.begin(), entry.faces.end(), [](const FontFace &a, const FontFace &b) {
        if (a.weight != b.weight)
            return a.weight < b.weight;
        return !a.italic && b.italic;
    });
    return entry;
}

void rebuildLocked(const QStringList &packageRoots)
{
    g_catalog.clear();
    g_familyIndex.clear();

    const QStringList roots = packageRoots.isEmpty()
        ? GpuPackageParse::defaultSearchPaths(QStringLiteral("DRIFT_FONTS_DIR"),
                                              QStringLiteral("fonts"), QStringLiteral("fonts"))
        : packageRoots;

    for (const QString &root : roots) {
        QDir dir(root);
        if (!dir.exists())
            continue;
        const QStringList packages = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &package : packages) {
            FontFamilyEntry entry = loadPackage(dir.filePath(package));
            if (entry.family.isEmpty())
                continue;
            // First root wins, so DRIFT_FONTS_DIR can shadow the shipped bundle.
            if (g_familyIndex.contains(entry.family.toLower()))
                continue;
            g_familyIndex.insert(entry.family.toLower(), g_catalog.size());
            g_catalog.append(entry);
        }
    }

    std::sort(g_catalog.begin(), g_catalog.end(), [](const FontFamilyEntry &a, const FontFamilyEntry &b) {
        if (a.category != b.category)
            return a.category < b.category;
        if (a.order != b.order)
            return a.order < b.order;
        return a.family < b.family;
    });

    g_familyIndex.clear();
    for (int i = 0; i < g_catalog.size(); ++i)
        g_familyIndex.insert(g_catalog.at(i).family.toLower(), i);

    g_initialized = true;
}

void ensureLocked()
{
    if (!g_initialized)
        rebuildLocked({});
}

// CSS-style nearest weight: at or above 400 prefer the next heavier face, below 400 the next
// lighter one, and fall back to whatever is closest when the family runs out in that direction.
const FontFace *nearestFace(const FontFamilyEntry &entry, int weight, bool italic)
{
    const FontFace *best = nullptr;
    int bestScore = std::numeric_limits<int>::max();

    for (const FontFace &face : entry.faces) {
        if (face.italic != italic)
            continue;
        const int distance = std::abs(face.weight - weight);
        const bool wrongSide = weight >= 400 ? face.weight < weight : face.weight > weight;
        const int score = distance + (wrongSide ? 1000 : 0);
        if (score < bestScore) {
            bestScore = score;
            best = &face;
        }
    }
    return best;
}

} // namespace

void reloadFontCatalog(const QStringList &packageRoots)
{
    QMutexLocker lock(&g_mutex);
    rebuildLocked(packageRoots);
}

const QList<FontFamilyEntry> &fontCatalog()
{
    QMutexLocker lock(&g_mutex);
    ensureLocked();
    return g_catalog;
}

const FontFamilyEntry *fontFamilyForName(const QString &family)
{
    QMutexLocker lock(&g_mutex);
    ensureLocked();
    const auto it = g_familyIndex.constFind(family.toLower());
    if (it == g_familyIndex.constEnd())
        return nullptr;
    return &g_catalog.at(it.value());
}

QList<QPair<QString, QString>> fontCategories()
{
    return {
        {QStringLiteral("impact"), QCoreApplication::translate("FontCatalog", "High-Impact & Bold")},
        {QStringLiteral("clean"), QCoreApplication::translate("FontCatalog", "Clean & Minimal")},
        {QStringLiteral("editorial"), QCoreApplication::translate("FontCatalog", "Classy & Editorial")},
        {QStringLiteral("playful"), QCoreApplication::translate("FontCatalog", "Creative & Playful")},
    };
}

QFont fontForStyle(const drift::TextStyle &style, int pixelSizePx)
{
    const int px = qMax(4, pixelSizePx);
    const FontFamilyEntry *entry = fontFamilyForName(style.fontFamily);

    if (!entry) { // not in the bundle — let the system font database resolve it
        QFont font(style.fontFamily);
        font.setPixelSize(px);
        font.setWeight(QFont::Weight(qBound(100, style.fontWeight, 900)));
        font.setItalic(style.italic);
        return font;
    }

    const FontFace *face = nearestFace(*entry, style.fontWeight, style.italic);
    if (!face && style.italic) // family ships no italic; use the upright face
        face = nearestFace(*entry, style.fontWeight, false);
    if (!face)
        face = &entry->faces.first();

    QFont font(entry->qtFamily);
    // usWeightClass is unreliable in these files (Montserrat/Inter Thin both report 250), so the
    // style name is what actually pins the face; the weight is a hint for backends that ignore it.
    font.setStyleName(face->styleName);
    font.setWeight(QFont::Weight(face->weight));
    font.setItalic(face->italic);
    font.setPixelSize(px);
    return font;
}
