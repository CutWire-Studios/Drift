#include "app/editor/keymap/Keymap.h"
#include "app/editor/keymap/KeymapCatalog.h"
#include "app/editor/keymap/KeymapPresets.h"

#include <QCoreApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QSettings>
#include <QVariantMap>

#include <algorithm>

namespace drift {

namespace {

#ifdef Q_OS_MACOS
constexpr bool kMac = true;
#else
constexpr bool kMac = false;
#endif

constexpr int kSchemaVersion = 1;
const QString kNoShortcut = QStringLiteral("none");
const QString kGroup = QStringLiteral("keymap");
const QString kLegacyGroup = QStringLiteral("shortcuts");

// v0 -> v1 (arrow-key jog, #203): stepBack/stepForward moved off Shift+Left/Right onto the bare
// arrows, and jumpBack/jumpForward took the chords they vacated. Everyone who had ever used
// "Reset shortcuts" - which wrote every binding, not just changed ones - carried the old pair in
// QSettings, which left two actions claiming Shift+Left and nothing on Left/Right.
QString supersededLegacyDefault(int fromVersion, const QString &actionId)
{
    if (fromVersion >= 1)
        return {};
    if (actionId == QLatin1String("stepBack"))
        return QStringLiteral("Shift+Left");
    if (actionId == QLatin1String("stepForward"))
        return QStringLiteral("Shift+Right");
    return {};
}

// Actions that were split per panel. A stored binding for `from` is carried to every id in `to`
// (the old action did all of those jobs at once); `keep` leaves `from` in the catalog because it
// still exists with its global meaning.
struct IdMove {
    const char *from;
    QStringList to;
    bool keep;
};

const QList<IdMove> &idMoves()
{
    static const QList<IdMove> moves = {
        {"gizmoMove", {QStringLiteral("viewport3d.gizmoMove"), QStringLiteral("tracking.trackerTool")}, false},
        {"gizmoRotate", {QStringLiteral("viewport3d.gizmoRotate"), QStringLiteral("masking.rectTool")}, false},
        {"gizmoScale", {QStringLiteral("viewport3d.gizmoScale"), QStringLiteral("masking.ellipseTool")}, false},
        {"gizmoOrientation", {QStringLiteral("viewport3d.gizmoOrientation")}, false},
        {"bladeTool", {QStringLiteral("masking.brushTool")}, true},
        {"selectTool", {QStringLiteral("masking.selectTool")}, true},
        {"maskingPolygonTool", {QStringLiteral("masking.polygonTool")}, false},
        {"maskingAiTool", {QStringLiteral("masking.aiTool")}, false},
        {"maskingLassoTool", {QStringLiteral("masking.lassoTool")}, false},
        {"maskingScissorsTool", {QStringLiteral("masking.scissorsTool")}, false},
        {"maskingWandTool", {QStringLiteral("masking.wandTool")}, false},
        {"maskingBrushSmaller", {QStringLiteral("masking.brushSmaller")}, false},
        {"maskingBrushLarger", {QStringLiteral("masking.brushLarger")}, false},
        {"maskingBrushToggleErase", {QStringLiteral("masking.brushToggleErase")}, false},
        {"trackForward", {QStringLiteral("tracking.trackForward")}, false},
        {"trackBackward", {QStringLiteral("tracking.trackBackward")}, false},
        {"trackFrameForward", {QStringLiteral("tracking.trackFrameForward")}, false},
        {"trackFrameBackward", {QStringLiteral("tracking.trackFrameBackward")}, false},
        {"trackBoth", {QStringLiteral("tracking.trackBoth")}, false},
    };
    return moves;
}

bool isArrowChord(const QString &chord)
{
    QStringList modifiers;
    QString key;
    keymap::splitChord(chord, &modifiers, &key);
    return key == QLatin1String("Left") || key == QLatin1String("Right") || key == QLatin1String("Up")
        || key == QLatin1String("Down");
}

QString namedKey(int key)
{
    switch (key) {
    case Qt::Key_Escape:    return QStringLiteral("Escape");
    case Qt::Key_Backspace: return QStringLiteral("Backspace");
    case Qt::Key_Return:    return QStringLiteral("Return");
    case Qt::Key_Enter:     return QStringLiteral("Enter");
    case Qt::Key_Insert:    return QStringLiteral("Insert");
    case Qt::Key_Delete:    return QStringLiteral("Delete");
    case Qt::Key_Home:      return QStringLiteral("Home");
    case Qt::Key_End:       return QStringLiteral("End");
    case Qt::Key_Left:      return QStringLiteral("Left");
    case Qt::Key_Up:        return QStringLiteral("Up");
    case Qt::Key_Right:     return QStringLiteral("Right");
    case Qt::Key_Down:      return QStringLiteral("Down");
    case Qt::Key_PageUp:    return QStringLiteral("PgUp");
    case Qt::Key_PageDown:  return QStringLiteral("PgDown");
    case Qt::Key_Space:     return QStringLiteral("Space");
    case Qt::Key_Tab:
    case Qt::Key_Backtab:   return QStringLiteral("Tab");
    case Qt::Key_Menu:      return QStringLiteral("Menu");
    case Qt::Key_Print:     return QStringLiteral("Print");
    case Qt::Key_Pause:     return QStringLiteral("Pause");
    default:
        break;
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24)
        return QStringLiteral("F%1").arg(key - Qt::Key_F1 + 1);
    if (key > Qt::Key_Space && key <= 0x7e)
        return QString(QChar(char16_t(key)));
    return {};
}

// What Shift produces on a US layout. Qt reports the symbol, not the base key, so the chord stores
// the symbol alone and matches the way Qt's own shortcut matching does.
bool isShiftedSymbol(int key)
{
    return key > Qt::Key_Space && key <= 0x7e && QStringLiteral("~!@#$%^&*()_+{}|:\"<>?").contains(QChar(char16_t(key)));
}

} // namespace

QString Keymap::macKeyName(int virtualKey)
{
    // kVK_ANSI_* from HIToolbox/Events.h.
    static const QHash<int, char> table = {
        {0, 'A'}, {1, 'S'}, {2, 'D'}, {3, 'F'}, {4, 'H'}, {5, 'G'}, {6, 'Z'}, {7, 'X'}, {8, 'C'},
        {9, 'V'}, {11, 'B'}, {12, 'Q'}, {13, 'W'}, {14, 'E'}, {15, 'R'}, {16, 'Y'}, {17, 'T'},
        {18, '1'}, {19, '2'}, {20, '3'}, {21, '4'}, {22, '6'}, {23, '5'}, {24, '='}, {25, '9'},
        {26, '7'}, {27, '-'}, {28, '8'}, {29, '0'}, {30, ']'}, {31, 'O'}, {32, 'U'}, {33, '['},
        {34, 'I'}, {35, 'P'}, {37, 'L'}, {38, 'J'}, {39, '\''}, {40, 'K'}, {41, ';'}, {42, '\\'},
        {43, ','}, {44, '/'}, {45, 'N'}, {46, 'M'}, {47, '.'}, {50, '`'},
    };
    const auto it = table.constFind(virtualKey);
    return it == table.cend() ? QString() : QString(QLatin1Char(*it));
}

QString Keymap::normalizeChord(const QString &chord)
{
    if (chord.isEmpty())
        return {};
    if (keymap::isPadChord(chord))
        return keymap::normalizePadChord(chord);
    const QKeySequence sequence = QKeySequence::fromString(chord, QKeySequence::PortableText);
    if (sequence.count() != 1)
        return {};
    // Rendered here rather than with QKeySequence::toString, which abbreviates Delete to Del and
    // so on; the names stay the ones the presets and the keymap page already use.
    const QKeyCombination combination = sequence[0];
    const QString name = namedKey(combination.key());
    if (name.isEmpty())
        return {};
    const Qt::KeyboardModifiers mods = combination.keyboardModifiers();
    QStringList parts;
    if (mods.testFlag(Qt::MetaModifier))
        parts << QStringLiteral("Meta");
    if (mods.testFlag(Qt::ControlModifier))
        parts << QStringLiteral("Ctrl");
    if (mods.testFlag(Qt::AltModifier))
        parts << QStringLiteral("Alt");
    if (mods.testFlag(Qt::ShiftModifier))
        parts << QStringLiteral("Shift");
    if (mods.testFlag(Qt::KeypadModifier))
        parts << QStringLiteral("Num");
    parts << name;
    return parts.join(QLatin1Char('+'));
}

QString Keymap::chordFromKey(int key, int modifiers, int nativeKey, const QString &text, bool mac)
{
    switch (key) {
    case Qt::Key_Shift:
    case Qt::Key_Control:
    case Qt::Key_Alt:
    case Qt::Key_Meta:
    case Qt::Key_AltGr:
    case Qt::Key_CapsLock:
    case Qt::Key_NumLock:
    case Qt::Key_ScrollLock:
    case Qt::Key_Super_L:
    case Qt::Key_Super_R:
        return {};
    default:
        break;
    }

    auto mods = Qt::KeyboardModifiers(modifiers);
    // Qt flags every arrow press on a Mac as a keypad key, which no binding means.
    if (mac && (key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down))
        mods &= ~Qt::KeypadModifier;
    bool shift = mods.testFlag(Qt::ShiftModifier) || key == Qt::Key_Backtab;
    QString name = namedKey(key);
    if (name.isEmpty()) {
        // Option composes characters on a Mac (Option+S arrives as the sharp s), so the key Qt
        // reports is not one we recognise; the virtual key code still says which key it was.
        if (mac && mods.testFlag(Qt::AltModifier))
            name = macKeyName(nativeKey);
    } else if (isShiftedSymbol(key)) {
        shift = false;
    }
    if (name.isEmpty() && text.size() == 1 && text.at(0).unicode() > 0x20 && text.at(0).unicode() < 0x7f)
        name = text.toUpper();
    if (name.isEmpty())
        return {};

    QStringList parts;
    if (mods.testFlag(Qt::MetaModifier))
        parts << QStringLiteral("Meta");
    if (mods.testFlag(Qt::ControlModifier))
        parts << QStringLiteral("Ctrl");
    if (mods.testFlag(Qt::AltModifier))
        parts << QStringLiteral("Alt");
    if (shift)
        parts << QStringLiteral("Shift");
    if (mods.testFlag(Qt::KeypadModifier))
        parts << QStringLiteral("Num");
    parts << name;
    const QString chord = parts.join(QLatin1Char('+'));
    return normalizeChord(chord) == chord ? chord : QString();
}

QString Keymap::chordFromKey(int key, int modifiers, int nativeKey, const QString &text) const
{
    return chordFromKey(key, modifiers, kMac ? nativeKeyFor(key, nativeKey) : nativeKey, text, kMac);
}

int Keymap::nativeKeyFor(int key, int fallback) const
{
    return key == m_lastKey && m_lastVirtualKey >= 0 ? m_lastVirtualKey : fallback;
}

bool Keymap::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        const auto *press = static_cast<QKeyEvent *>(event);
        m_lastKey = press->key();
        m_lastVirtualKey = int(press->nativeVirtualKey());
    }
    return QObject::eventFilter(watched, event);
}

QString Keymap::displayText(const QString &chord, bool mac)
{
    if (chord.isEmpty() || !mac)
        return chord;

    QStringList modifiers;
    QString key;
    keymap::splitChord(chord, &modifiers, &key);

    static const QHash<QString, QString> symbols = {
        {QStringLiteral("Left"), QStringLiteral("←")},
        {QStringLiteral("Right"), QStringLiteral("→")},
        {QStringLiteral("Up"), QStringLiteral("↑")},
        {QStringLiteral("Down"), QStringLiteral("↓")},
        {QStringLiteral("Backspace"), QStringLiteral("⌫")},
        {QStringLiteral("Delete"), QStringLiteral("⌦")},
        {QStringLiteral("Escape"), QStringLiteral("⎋")},
        {QStringLiteral("Return"), QStringLiteral("↩")},
        {QStringLiteral("Enter"), QStringLiteral("⌅")},
    };
    key = symbols.value(key, key);
    if (modifiers.contains(QLatin1String("Num")))
        key = QStringLiteral("Num ") + key;

    // Qt's Apple mapping: Ctrl is Command, Meta is the physical Control. Apple's order.
    QString out;
    if (modifiers.contains(QLatin1String("Meta")))
        out += QStringLiteral("⌃");
    if (modifiers.contains(QLatin1String("Alt")))
        out += QStringLiteral("⌥");
    if (modifiers.contains(QLatin1String("Shift")))
        out += QStringLiteral("⇧");
    if (modifiers.contains(QLatin1String("Ctrl")))
        out += QStringLiteral("⌘");
    return out + key;
}

QString Keymap::displayText(const QString &chord) const
{
    return displayText(chord, kMac);
}

Keymap::Keymap(QObject *parent)
    : QObject(parent)
{
    if (QCoreApplication::instance())
        QCoreApplication::instance()->installEventFilter(this);
    load();
}

void Keymap::load()
{
    migrateLegacy();

    QSettings settings;
    settings.beginGroup(kGroup);
    m_presetId = settings.value(QStringLiteral("preset"), QLatin1String(keymap::kDefaultPresetId)).toString();
    if (!keymap::findPreset(m_presetId))
        m_presetId = QLatin1String(keymap::kDefaultPresetId);
    settings.beginGroup(QStringLiteral("overrides"));
    QHash<QString, QStringList> stored;
    for (const QString &id : settings.childKeys()) {
        QStringList list = settings.value(id).toStringList();
        if (list == QStringList{kNoShortcut}) {
            stored.insert(id, {});
            continue;
        }
        QStringList clean;
        for (const QString &chord : std::as_const(list)) {
            const QString normalized = normalizeChord(chord);
            if (!normalized.isEmpty() && !clean.contains(normalized))
                clean.append(normalized);
        }
        stored.insert(id, clean);
    }
    settings.endGroup();
    settings.endGroup();

    for (const IdMove &move : idMoves()) {
        const QString from = QLatin1String(move.from);
        if (!stored.contains(from))
            continue;
        const QStringList chords = stored.value(from);
        for (const QString &to : move.to) {
            if (stored.contains(to))
                continue;
            stored.insert(to, chords);
            settings.setValue(kGroup + QStringLiteral("/overrides/") + to, chords.isEmpty() ? QStringList{kNoShortcut} : chords);
        }
        if (!move.keep) {
            stored.remove(from);
            settings.remove(kGroup + QStringLiteral("/overrides/") + from);
        }
    }
    for (auto it = stored.cbegin(); it != stored.cend(); ++it) {
        if (keymap::findAction(it.key()))
            m_overrides.insert(it.key(), it.value());
    }

    pruneOverrides();
    rebuild();
}

void Keymap::migrateLegacy()
{
    QSettings settings;
    if (settings.contains(kGroup + QStringLiteral("/schemaVersion")))
        return;

    settings.beginGroup(kLegacyGroup);
    const int version = settings.value(QStringLiteral("schemaVersion"), 0).toInt();
    const keymap::Preset *drift = keymap::findPreset(QLatin1String(keymap::kDefaultPresetId));
    QHash<QString, QString> migrated;
    const auto consider = [&](const QString &id, const QString &defaultsFrom) {
        // An empty stored value always meant "use the default" in the old format.
        const QString stored = normalizeChord(settings.value(id).toString());
        if (stored.isEmpty() || stored == supersededLegacyDefault(version, id))
            return;
        const keymap::ActionDef *action = keymap::findAction(defaultsFrom);
        if (!action)
            return;
        QStringList base = keymap::presetChords(*drift, defaultsFrom, keymap::scopeOf(*action));
        base.removeIf(keymap::isPadChord);
        if (base.size() == 1 && base.first() == stored)
            return;
        migrated.insert(id, stored);
    };
    for (const keymap::ActionDef &action : keymap::actions())
        consider(QLatin1String(action.id), QLatin1String(action.id));
    // Split-out ids are no longer in the catalog; load() carries what is stored under them.
    for (const IdMove &move : idMoves()) {
        if (!move.keep)
            consider(QLatin1String(move.from), move.to.first());
    }
    settings.endGroup();

    settings.remove(kLegacyGroup);
    settings.beginGroup(kGroup);
    settings.setValue(QStringLiteral("schemaVersion"), kSchemaVersion);
    for (auto it = migrated.cbegin(); it != migrated.cend(); ++it)
        settings.setValue(QStringLiteral("overrides/") + it.key(), QStringList{it.value()});
    settings.endGroup();
}

QStringList Keymap::presetBindings(const QString &actionId) const
{
    const keymap::Preset *preset = keymap::findPreset(m_presetId);
    if (!preset)
        return {};
    QStringList out;
    for (const QString &chord : keymap::presetChords(*preset, actionId, scopeOf(actionId)))
        out << keymap::translateForPlatform(chord, kMac);
    return out;
}

void Keymap::rebuild()
{
    m_effective.clear();
    m_owners.clear();

    QStringList ids;
    for (const keymap::ActionDef &action : keymap::actions()) {
        ids << QLatin1String(action.id);
        m_effective.insert(QLatin1String(action.id), {});
    }
    // Sorted, so the same settings produce the same map on every launch.
    ids.sort();

    const auto claim = [this](const QString &id, const QString &chord) {
        const QString scope = scopeOf(id);
        if (m_owners[scope].contains(chord))
            return;
        m_owners[scope].insert(chord, id);
        m_effective[id].append(chord);
    };

    // A chord can end up on two actions (a release moves a default onto a chord a stored binding
    // already uses, or a preset's Meta is translated onto an existing chord). Nothing of the
    // user's is overwritten: overrides claim first, then the preset's own chords, then the ones
    // translated from Meta, and the loser simply loses the chord in memory.
    for (const QString &id : std::as_const(ids)) {
        for (const QString &chord : m_overrides.value(id))
            claim(id, chord);
    }
    for (int pass = 0; pass < 2; ++pass) {
        for (const QString &id : std::as_const(ids)) {
            if (m_overrides.contains(id))
                continue;
            const keymap::Preset *preset = keymap::findPreset(m_presetId);
            if (!preset)
                continue;
            for (const QString &raw : keymap::presetChords(*preset, id, scopeOf(id))) {
                const QString chord = keymap::translateForPlatform(raw, kMac);
                if ((chord != raw) == (pass == 1))
                    claim(id, chord);
            }
        }
    }

    QSet<QString> all;
    for (auto it = m_effective.cbegin(); it != m_effective.cend(); ++it) {
        if (keymap::isLocalScope(scopeOf(it.key())))
            continue;
        for (const QString &chord : it.value()) {
            if (!isArrowChord(chord) && !keymap::isPadChord(chord))
                all.insert(chord);
        }
    }
    m_chords = QStringList(all.cbegin(), all.cend());
    m_chords.sort();
}

void Keymap::pruneOverrides()
{
    QSettings settings;
    for (auto it = m_overrides.begin(); it != m_overrides.end();) {
        if (it.value() == presetBindings(it.key())) {
            settings.remove(kGroup + QStringLiteral("/overrides/") + it.key());
            it = m_overrides.erase(it);
        } else {
            ++it;
        }
    }
}

void Keymap::storeOverride(const QString &actionId, const QStringList &chords)
{
    QSettings settings;
    const QString key = kGroup + QStringLiteral("/overrides/") + actionId;
    if (chords == presetBindings(actionId)) {
        m_overrides.remove(actionId);
        settings.remove(key);
    } else {
        m_overrides.insert(actionId, chords);
        settings.setValue(key, chords.isEmpty() ? QStringList{kNoShortcut} : chords);
    }
    rebuild();
    emit changed();
}

void Keymap::persistPreset() const
{
    QSettings settings;
    settings.setValue(kGroup + QStringLiteral("/preset"), m_presetId);
}

QVariantList Keymap::presets() const
{
    QVariantList out;
    for (const keymap::Preset &preset : keymap::presets())
        out.append(QVariantMap{{QStringLiteral("id"), preset.id}, {QStringLiteral("name"), keymap::translatedName(preset)}});
    return out;
}

QVariantList Keymap::categories() const
{
    QVariantList out;
    for (const keymap::CategoryDef &category : keymap::categories()) {
        out.append(QVariantMap{
            {QStringLiteral("id"), QLatin1String(category.id)},
            {QStringLiteral("label"), keymap::translatedCategoryLabel(category)},
            {QStringLiteral("scope"), QLatin1String(category.scope)},
            {QStringLiteral("local"), category.local},
        });
    }
    return out;
}

QVariantList Keymap::actions() const
{
    QVariantList out;
    for (const keymap::ActionDef &action : keymap::actions()) {
        const QString id = QLatin1String(action.id);
        const QStringList chords = m_effective.value(id);
        const keymap::CategoryDef *category = keymap::findCategory(QLatin1String(action.category));
        out.append(QVariantMap{
            {QStringLiteral("id"), id},
            {QStringLiteral("label"), keymap::translatedLabel(action)},
            {QStringLiteral("shortcut"), chords.value(0)},
            {QStringLiteral("shortcuts"), chords},
            {QStringLiteral("category"), QLatin1String(action.category)},
            {QStringLiteral("categoryLabel"), category ? keymap::translatedCategoryLabel(*category) : QString()},
            {QStringLiteral("scope"), keymap::scopeOf(action)},
            {QStringLiteral("modified"), m_overrides.contains(id)},
        });
    }
    return out;
}

QStringList Keymap::bindings(const QString &actionId) const
{
    return m_effective.value(actionId);
}

QString Keymap::scopeOf(const QString &actionId) const
{
    const keymap::ActionDef *action = keymap::findAction(actionId);
    return action ? keymap::scopeOf(*action) : QString();
}

QString Keymap::labelFor(const QString &actionId) const
{
    const keymap::ActionDef *action = keymap::findAction(actionId);
    return action ? keymap::translatedLabel(*action) : actionId;
}

bool Keymap::isModified(const QString &actionId) const
{
    return m_overrides.contains(actionId);
}

QStringList Keymap::chordsFor(const QString &scope) const
{
    QStringList out = m_owners.value(scope).keys();
    out.sort();
    return out;
}

QString Keymap::actionFor(const QString &scope, const QString &chord) const
{
    return m_owners.value(scope).value(chord);
}

QString Keymap::addBinding(const QString &actionId, const QString &chord)
{
    if (!keymap::findAction(actionId))
        return {};
    const QString normalized = normalizeChord(chord);
    if (normalized.isEmpty())
        return {};
    QStringList list = bindings(actionId);
    if (list.contains(normalized))
        return {};
    const QString holder = actionFor(scopeOf(actionId), normalized);
    if (!holder.isEmpty())
        return labelFor(holder);
    list.append(normalized);
    storeOverride(actionId, list);
    return {};
}

QString Keymap::setBindings(const QString &actionId, const QStringList &chords)
{
    if (!keymap::findAction(actionId))
        return {};
    const QString scope = scopeOf(actionId);
    QStringList list;
    for (const QString &chord : chords) {
        const QString normalized = normalizeChord(chord);
        if (normalized.isEmpty() || list.contains(normalized))
            continue;
        const QString holder = actionFor(scope, normalized);
        if (!holder.isEmpty() && holder != actionId)
            return labelFor(holder);
        list.append(normalized);
    }
    storeOverride(actionId, list);
    return {};
}

void Keymap::removeBinding(const QString &actionId, const QString &chord)
{
    if (!keymap::findAction(actionId))
        return;
    QStringList list = bindings(actionId);
    if (list.removeAll(normalizeChord(chord)) == 0)
        return;
    storeOverride(actionId, list);
}

void Keymap::resetAction(const QString &actionId)
{
    if (!m_overrides.contains(actionId))
        return;
    m_overrides.remove(actionId);
    QSettings().remove(kGroup + QStringLiteral("/overrides/") + actionId);
    rebuild();
    emit changed();
}

void Keymap::resetAll()
{
    if (m_overrides.isEmpty())
        return;
    m_overrides.clear();
    QSettings().remove(kGroup + QStringLiteral("/overrides"));
    rebuild();
    emit changed();
}

void Keymap::setPreset(const QString &presetId, bool keepOverrides)
{
    if (!keymap::findPreset(presetId))
        return;
    if (!keepOverrides) {
        m_overrides.clear();
        QSettings().remove(kGroup + QStringLiteral("/overrides"));
    }
    m_presetId = presetId;
    persistPreset();
    pruneOverrides();
    rebuild();
    emit changed();
}

} // namespace drift
