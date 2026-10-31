#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QQmlPropertyMap>
#include <QStringList>
#include <QUrl>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

#include <optional>

// One JSON theme document, exposed as a fixed set of QQmlPropertyMaps.
//
// The map objects themselves stay put across loadTheme() / setVariant(). Values
// are pushed with insert(), so a QML binding to ThemeManager.semantic.panel.border
// (or Theme.c.button.primary.background, once a component group exists) keeps
// watching the same object and updates when that key changes. Replacing the map
// would drop those subscriptions.
class ThemeManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QQmlPropertyMap *palette READ palette CONSTANT)
    Q_PROPERTY(QQmlPropertyMap *semantic READ semantic CONSTANT)
    Q_PROPERTY(QQmlPropertyMap *components READ components CONSTANT)
    Q_PROPERTY(QQmlPropertyMap *metrics READ metrics CONSTANT)
    Q_PROPERTY(QQmlPropertyMap *typography READ typography CONSTANT)
    Q_PROPERTY(QQmlPropertyMap *motion READ motion CONSTANT)
    Q_PROPERTY(QQmlPropertyMap *window READ window CONSTANT)
    Q_PROPERTY(QString currentTheme READ currentTheme NOTIFY themeChanged)
    Q_PROPERTY(QStringList availableThemes READ availableThemes NOTIFY availableThemesChanged)
    // "system" (follow Theme.darkMode), "dark", "light", or a user theme id.
    // Persisted as QSettings Theme/selection. Loaded once, in the constructor.
    Q_PROPERTY(QString selection READ selection NOTIFY selectionChanged)
    // "dark" or "light" of the theme currently on screen. Not themeChanged:
    // a token reload must not invalidate Theme.darkMode, which writes the
    // variant back through setVariant while that binding is evaluating.
    Q_PROPERTY(QString effectiveVariant READ effectiveVariant NOTIFY effectiveVariantChanged)
    // True when a single-variant user theme is on screen. Theme.darkMode then
    // follows effectiveVariant, and the light/dark toggle leaves the theme.
    Q_PROPERTY(bool appearanceFollowsTheme READ appearanceFollowsTheme NOTIFY appearanceFollowsThemeChanged)
    Q_PROPERTY(QVariantList themes READ themes NOTIFY availableThemesChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    // Set when the theme on screen declares a newer schemaVersion than this
    // build reads. It still loads; unknown keys are ignored.
    Q_PROPERTY(QString lastWarning READ lastWarning NOTIFY lastWarningChanged)
    Q_PROPERTY(bool hotReload READ hotReload WRITE setHotReload NOTIFY hotReloadChanged)

public:
    // Bump when a release adds tokens a theme author should know about or changes
    // what an existing key means. A document without schemaVersion predates the
    // field and reads as 0. Missing tokens are filled from the built-in of the
    // theme's variant whatever the version, so older themes keep working.
    static constexpr int kSchemaVersion = 1;

    explicit ThemeManager(QObject *parent = nullptr);

    // Process-wide instance QML binds to. main() publishes it with
    // registerSingleton(); a ThemeManager constructed anywhere else stays private.
    static ThemeManager *shared();
    static void registerSingleton();
    // One component block of the built-in "dark" or "light" theme, resolved, without building
    // a ThemeManager. For UI shown before the theme system is up (the startup splash).
    static QVariantMap builtinComponent(const QString &variant, const QString &component);

    QQmlPropertyMap *palette() const { return m_palette; }
    QQmlPropertyMap *semantic() const { return m_semantic; }
    QQmlPropertyMap *components() const { return m_components; }
    QQmlPropertyMap *metrics() const { return m_metrics; }
    QQmlPropertyMap *typography() const { return m_typography; }
    QQmlPropertyMap *motion() const { return m_motion; }
    QQmlPropertyMap *window() const { return m_window; }

    QString currentTheme() const { return m_currentTheme; }
    QStringList availableThemes() const;
    QString selection() const { return m_selection; }
    QString effectiveVariant() const { return m_effectiveVariant; }
    bool appearanceFollowsTheme() const { return m_userTheme && !m_hasVariants; }
    QVariantList themes() const;
    QString lastError() const { return m_lastError; }
    QString lastWarning() const { return m_lastWarning; }
    bool hotReload() const { return m_hotReload; }
    void setHotReload(bool enabled);

    Q_INVOKABLE QVariant token(const QString &dottedPath) const;
    // Built-in dark/light documents, independent of whichever theme is active.
    // Theme.qml uses this for the _dark / _light maps, which stay constant while
    // setVariant() swaps the live groups.
    Q_INVOKABLE QVariant builtinToken(const QString &variant, const QString &dottedPath) const;
    Q_INVOKABLE bool loadTheme(const QString &nameOrPath);
    Q_INVOKABLE void reload();
    // Applies a dark/light side. Selection "system" swaps the built-in. A
    // dual-variant user theme swaps its overlay and stays selected. A
    // single-variant user theme records the request and stays put, so this
    // never writes back into the value Theme.darkMode is bound to.
    // Selection "dark" or "light" is pinned and ignores the request.
    // A session override (applySessionOverride) is not replaced by a built-in.
    Q_INVOKABLE void setVariant(const QString &variant);
    // "system", "dark", "light", or a user theme id. Persists the choice.
    // Clears a session override: this is an explicit selection.
    Q_INVOKABLE bool select(const QString &id);
    // Loads nameOrPath for this process only. Does not write Theme/selection.
    // setVariant will not replace it with a built-in; a dual-variant document
    // still follows the requested side. select() ends the override.
    bool applySessionOverride(const QString &nameOrPath);
    // Copies a .json file, a theme folder, or a .zip into the user themes
    // directory. Returns the new id, or empty and sets lastError. With replace,
    // an installed theme of the same name is overwritten and loses QML trust.
    Q_INVOKABLE QString importTheme(const QUrl &fileOrFolderOrZip, bool replace = false);
    // User themes only. The current selection falls back to system.
    Q_INVOKABLE bool removeTheme(const QString &id);
    // Fully resolved current theme, extends omitted, as a standalone JSON file.
    Q_INVOKABLE bool exportTheme(const QUrl &file);
    // JSON for theme `id` renamed to `name`, for editing outside Drift. Without
    // starter: every resolved token, like exportTheme. With starter: extends
    // `id` plus its resolved core palette, metrics, typography, motion and
    // window, a small file that tracks the base.
    QByteArray themeTemplate(const QString &id, const QString &name, bool starter, QString *error) const;
    Q_INVOKABLE QUrl themesFolderUrl() const;
    // surface, panel, accent, foreground, primary for the swatch. Dual-variant
    // themes preview the side setVariant last requested.
    Q_INVOKABLE QVariantMap previewColors(const QString &id) const;
    // File URL of qml/<slot>.qml when the current theme is a trusted folder
    // theme that contains that file. Empty for built-ins, single-file themes,
    // untrusted themes, and missing slots.
    Q_INVOKABLE QUrl overrideUrl(const QString &slot) const;
    Q_INVOKABLE bool isThemeTrusted(const QString &name) const;
    Q_INVOKABLE void setThemeTrusted(const QString &name, bool trusted);
    // Basenames of qml/*.qml in a folder theme, for the trust dialog.
    Q_INVOKABLE QStringList overrideSlots(const QString &name) const;

signals:
    void themeChanged();
    void effectiveVariantChanged();
    void appearanceFollowsThemeChanged();
    void hotReloadChanged();
    void availableThemesChanged();
    void selectionChanged();
    void lastErrorChanged();
    void lastWarningChanged();

private:
    struct Loaded {
        QString name;
        QString path;
        QString variant;
        bool user = false;
        bool hasVariants = false;
        int schemaVersion = 0;
        QVariantMap document;
    };

    void setLastError(const QString &error);
    void writeSelection() const;
    void restorePersisted();
    QVariantMap themeRecord(const QString &id, const QString &path, bool builtIn) const;
    QString importLocal(const QString &path, bool replace);
    bool installDirectory(const QString &sourceDir, const QString &id, bool replace, QString *error) const;
    bool installJsonFile(const QString &sourceFile, const QString &id, bool replace, QString *error) const;
    bool extractZipTheme(const QString &zipPath, const QString &destDir, QString *error) const;
    bool removeInstalled(const QString &path, QString *error) const;

    QString locate(const QString &nameOrPath, QString *error) const;
    QString pathForThemeName(const QString &name, QString *error) const;
    QVariantMap compose(const QString &path, QStringList *stack, QString *error) const;
    std::optional<Loaded> readTheme(const QString &nameOrPath, QString *error) const;
    void apply(const Loaded &loaded);
    void updateWatch();
    // Directory of qml overrides for a folder theme (theme.json). Empty otherwise.
    QString themeQmlDirectory(const QString &themeJsonPath) const;
    bool isFolderTheme(const QString &themeJsonPath) const;
    bool isThemeWatchPath(const QString &path) const;
    QStringList readTrustedThemes() const;
    void writeTrustedThemes(const QStringList &names);

    QQmlPropertyMap *m_palette = nullptr;
    QQmlPropertyMap *m_semantic = nullptr;
    QQmlPropertyMap *m_components = nullptr;
    QQmlPropertyMap *m_metrics = nullptr;
    QQmlPropertyMap *m_typography = nullptr;
    QQmlPropertyMap *m_motion = nullptr;
    QQmlPropertyMap *m_window = nullptr;

    QFileSystemWatcher m_watcher;
    QString m_currentTheme;
    QString m_sourcePath;
    QString m_selection = QStringLiteral("system");
    QString m_effectiveVariant = QStringLiteral("dark");
    QString m_requestedVariant = QStringLiteral("dark");
    QString m_lastError;
    QString m_lastWarning;
    QVariantMap m_resolved;
    QVariantMap m_builtinDark;
    QVariantMap m_builtinLight;
    bool m_userTheme = false;
    bool m_hasVariants = false;
    bool m_hotReload = false;
    bool m_inReload = false;
    // DRIFT_THEME. The persisted selection stays as restorePersisted left it.
    bool m_sessionOverride = false;

private slots:
    void onThemeFileChanged(const QString &path);
};
