#include "TextPresetStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

namespace drift {

namespace {

const QString kIdPrefix = QStringLiteral("user:");
constexpr int kFormatVersion = 1;

QJsonObject presetToJson(const TextPreset &preset)
{
    return QJsonObject{
        {QStringLiteral("id"), preset.id},
        {QStringLiteral("label"), preset.label},
        {QStringLiteral("sampleText"), preset.sampleText},
        {QStringLiteral("style"), textStyleToJson(preset.style)},
    };
}

// `id` is taken from the object only when the caller trusts it (the library file); imports pass
// false so a shared file can never overwrite an existing preset by claiming its id.
TextPreset presetFromJson(const QJsonObject &o, bool keepId)
{
    TextPreset preset;
    if (keepId)
        preset.id = o.value(QStringLiteral("id")).toString();
    preset.label = o.value(QStringLiteral("label")).toString();
    preset.sampleText = o.value(QStringLiteral("sampleText")).toString();
    preset.style = textStyleFromJson(o.value(QStringLiteral("style")).toObject());
    // A saved pack is the style itself; a stale packId pointing at whatever clip it came from
    // would make the picker claim the wrong parent pack.
    preset.style.packId.clear();
    preset.style.keyframes.clear();
    return preset;
}

QString mintId()
{
    return kIdPrefix + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

} // namespace

bool isUserTextPresetId(const QString &id)
{
    return id.startsWith(kIdPrefix);
}

TextPresetStore &TextPresetStore::instance()
{
    static TextPresetStore store;
    return store;
}

QString TextPresetStore::storePath()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        return {};
    return QDir(base).filePath(QStringLiteral("text-presets.json"));
}

void TextPresetStore::ensureLoaded() const
{
    if (m_loaded)
        return;
    m_loaded = true;
    m_presets.clear();

    const QString path = storePath();
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return; // no library yet

    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonArray presets = root.value(QStringLiteral("presets")).toArray();
    for (const QJsonValue &value : presets) {
        const TextPreset preset = presetFromJson(value.toObject(), true);
        if (preset.id.isEmpty() || preset.label.isEmpty())
            continue;
        m_presets.append(preset);
    }
}

bool TextPresetStore::save() const
{
    const QString path = storePath();
    if (path.isEmpty())
        return false;

    QDir().mkpath(QFileInfo(path).absolutePath());

    QJsonArray presets;
    for (const TextPreset &preset : m_presets)
        presets.append(presetToJson(preset));

    const QJsonObject root{
        {QStringLiteral("version"), kFormatVersion},
        {QStringLiteral("presets"), presets},
    };

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return file.commit();
}

QList<TextPreset> TextPresetStore::presets() const
{
    QMutexLocker locker(&m_mutex);
    ensureLoaded();
    return m_presets;
}

std::optional<TextPreset> TextPresetStore::presetForId(const QString &id) const
{
    QMutexLocker locker(&m_mutex);
    ensureLoaded();
    for (const TextPreset &preset : m_presets) {
        if (preset.id == id)
            return preset;
    }
    return std::nullopt;
}

QString TextPresetStore::add(const QString &label, const TextStyle &style,
                             const QString &sampleText)
{
    const QString trimmed = label.trimmed();
    if (trimmed.isEmpty())
        return {};

    QMutexLocker locker(&m_mutex);
    ensureLoaded();

    TextPreset preset;
    preset.id = mintId();
    preset.label = trimmed;
    preset.style = style;
    preset.style.packId.clear();
    // A pack is a look, not a motion: keys belong to the clip they were authored on.
    preset.style.keyframes.clear();
    preset.sampleText = sampleText.trimmed().isEmpty() ? trimmed : sampleText.trimmed();

    m_presets.prepend(preset);
    if (!save()) {
        m_presets.removeFirst();
        return {};
    }
    return preset.id;
}

bool TextPresetStore::rename(const QString &id, const QString &label)
{
    const QString trimmed = label.trimmed();
    if (trimmed.isEmpty())
        return false;

    QMutexLocker locker(&m_mutex);
    ensureLoaded();
    for (TextPreset &preset : m_presets) {
        if (preset.id != id)
            continue;
        const QString previous = preset.label;
        preset.label = trimmed;
        if (!save()) {
            preset.label = previous;
            return false;
        }
        return true;
    }
    return false;
}

bool TextPresetStore::remove(const QString &id)
{
    QMutexLocker locker(&m_mutex);
    ensureLoaded();
    for (int i = 0; i < m_presets.size(); ++i) {
        if (m_presets.at(i).id != id)
            continue;
        const TextPreset removed = m_presets.takeAt(i);
        if (!save()) {
            m_presets.insert(i, removed);
            return false;
        }
        return true;
    }
    return false;
}

bool TextPresetStore::exportToFile(const QString &id, const QString &path) const
{
    const std::optional<TextPreset> preset = presetForId(id);
    if (!preset)
        return false;

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(presetToJson(*preset)).toJson(QJsonDocument::Indented));
    return file.commit();
}

QString TextPresetStore::importFromFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};

    const QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
    if (!o.contains(QStringLiteral("style")))
        return {};

    const TextPreset incoming = presetFromJson(o, false);
    const QString label = incoming.label.isEmpty() ? QFileInfo(path).completeBaseName()
                                                   : incoming.label;
    return add(label, incoming.style, incoming.sampleText);
}

void TextPresetStore::reload()
{
    QMutexLocker locker(&m_mutex);
    m_loaded = false;
    m_presets.clear();
}

} // namespace drift
