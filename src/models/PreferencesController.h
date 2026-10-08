#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>

// App-wide QSettings preferences and persisted UI layout. Reached from QML as
// EditorState.preferences. Nothing here reads the open project.
class PreferencesController : public QObject
{
    Q_OBJECT

    // App-wide theme preference, backed by QSettings("ui/darkMode"). Until the user
    // toggles once, darkModeOverridden is false and the UI follows the OS colour
    // scheme live; after that the stored choice wins on every launch.
    Q_PROPERTY(bool darkModeOverridden READ darkModeOverridden NOTIFY darkModePreferenceChanged)
    Q_PROPERTY(bool darkModePreferred READ darkModePreferred NOTIFY darkModePreferenceChanged)
    // Editor workspace arrangement. In the landscape workspace the preview sits in the
    // three-pane top row, so its size is bounded by that row's *height* — a 9:16 canvas
    // ends up postage-stamp small. The portrait workspace gives the preview a full-height
    // column beside the whole editing stack instead. Follows the canvas orientation until
    // the user picks one explicitly, after which the stored choice wins on every launch —
    // the same override rule as the theme above, backed by QSettings("ui/workspaceLayout").
    // The effective layout is resolved in QML so it can track both signals at once.
    Q_PROPERTY(bool workspaceLayoutOverridden READ workspaceLayoutOverridden
                   NOTIFY workspaceLayoutPreferenceChanged)
    Q_PROPERTY(QString workspaceLayoutPreferred READ workspaceLayoutPreferred
                   NOTIFY workspaceLayoutPreferenceChanged)
    // On by default. Worth turning off on a very long timeline: the strip repaints every clip
    // in the project on every edit.
    Q_PROPERTY(bool timelineOverviewVisible READ timelineOverviewVisible
                   WRITE setTimelineOverviewVisible NOTIFY timelineOverviewVisibleChanged)
    Q_PROPERTY(bool audioMixerVisible READ audioMixerVisible
                   WRITE setAudioMixerVisible NOTIFY audioMixerVisibleChanged)
    // Width the user dragged the mixer to; 0 means size to fit its channel strips.
    Q_PROPERTY(qreal audioMixerWidth READ audioMixerWidth
                   WRITE setAudioMixerWidth NOTIFY audioMixerWidthChanged)
    Q_PROPERTY(qreal trackLabelsWidth READ trackLabelsWidth
                   WRITE setTrackLabelsWidth NOTIFY trackLabelsWidthChanged)
    // Timeline toolbar layout: action ids shown as buttons, then the ones in its More menu.
    // Empty means the QML defaults; ids are validated on the QML side, which owns the registry.
    Q_PROPERTY(QStringList timelineToolbarItems READ timelineToolbarItems
                   NOTIFY timelineToolbarLayoutChanged)
    Q_PROPERTY(QStringList timelineMenuItems READ timelineMenuItems
                   NOTIFY timelineToolbarLayoutChanged)
    // Opt-in: on launch, restore the last open project (saved .drift or unsaved recovery snapshot).
    Q_PROPERTY(bool reopenLastProject READ reopenLastProject WRITE setReopenLastProject NOTIFY reopenLastProjectChanged)
    // Preview zero-copy import: VAAPI dma-buf on Linux, D3D11 interop on Windows. Takes effect
    // after restart; hidden when this machine has no decode backend for either.
    Q_PROPERTY(bool vaapiZeroCopy READ vaapiZeroCopy WRITE setVaapiZeroCopy NOTIFY vaapiZeroCopyChanged)
    Q_PROPERTY(bool vaapiZeroCopySupported READ vaapiZeroCopySupported CONSTANT)
    // Android MediaCodec zero-copy preview. Same shape and the same caveat as the VAAPI pair
    // above: a driver can import the surface and still sample it wrongly, which shows up as a
    // corrupt preview with nothing to catch it — so it is opt-in and takes effect on restart.
    Q_PROPERTY(bool mediaCodecZeroCopy READ mediaCodecZeroCopy WRITE setMediaCodecZeroCopy NOTIFY
                   mediaCodecZeroCopyChanged)
    Q_PROPERTY(bool mediaCodecZeroCopySupported READ mediaCodecZeroCopySupported CONSTANT)
    // Hybrid-graphics Windows laptops: which GPU Drift asks to run on — "auto", "integrated" or
    // "discrete". The driver picks the GPU when it loads, so this takes effect on the next
    // launch. Hidden on single-GPU machines and off Windows.
    Q_PROPERTY(QString preferredGpu READ preferredGpu WRITE setPreferredGpu NOTIFY preferredGpuChanged)
    Q_PROPERTY(bool gpuPreferenceSupported READ gpuPreferenceSupported CONSTANT)
    Q_PROPERTY(bool gpuPreferenceInSystemSettings READ gpuPreferenceInSystemSettings CONSTANT)
    Q_PROPERTY(bool invertTimelineScroll READ invertTimelineScroll WRITE setInvertTimelineScroll
                   NOTIFY invertTimelineScrollChanged)
    // App-wide interface language, QSettings("ui/language"). Empty means follow the OS locale.
    // "en" is the source catalog (no .qm). Other codes match i18n/drift_<code>.qm.
    // needsUiLanguagePrompt is true only on a brand-new install, before the first-launch chooser
    // (or a later language pick from the header / Android Settings) has written ui/languageChosen.
    Q_PROPERTY(QString uiLanguage READ uiLanguage WRITE setUiLanguage NOTIFY uiLanguageChanged)
    Q_PROPERTY(QVariantList uiLanguages READ uiLanguages NOTIFY uiLanguageChanged)
    Q_PROPERTY(bool needsUiLanguagePrompt READ needsUiLanguagePrompt NOTIFY uiLanguageChanged)
    // Extra UI scale on top of the OS display scale. QSettings("ui/scale"), 1.0..2.0 in
    // 0.25 steps. Applied as QT_SCALE_FACTOR before QApplication; a change needs a restart.
    Q_PROPERTY(double uiScale READ uiScale WRITE setUiScale NOTIFY uiScaleChanged)
    Q_PROPERTY(double appliedUiScale READ appliedUiScale CONSTANT)
    Q_PROPERTY(bool uiScaleNeedsRestart READ uiScaleNeedsRestart NOTIFY uiScaleChanged)

public:
    explicit PreferencesController(QObject *parent = nullptr);

    bool darkModeOverridden() const { return m_darkModeOverridden; }
    bool darkModePreferred() const { return m_darkModePreferred; }
    Q_INVOKABLE void setDarkModePreference(bool enabled);
    Q_INVOKABLE void clearDarkModePreference();

    bool workspaceLayoutOverridden() const { return m_workspaceLayoutOverridden; }
    QString workspaceLayoutPreferred() const { return m_workspaceLayoutPreferred; }
    // "portrait" / "landscape"; anything else is treated as landscape.
    Q_INVOKABLE void setWorkspaceLayoutPreference(const QString &layout);
    // Back to following the canvas orientation.
    Q_INVOKABLE void clearWorkspaceLayoutPreference();

    bool timelineOverviewVisible() const { return m_timelineOverviewVisible; }
    void setTimelineOverviewVisible(bool visible);
    bool audioMixerVisible() const { return m_audioMixerVisible; }
    void setAudioMixerVisible(bool visible);
    qreal audioMixerWidth() const { return m_audioMixerWidth; }
    void setAudioMixerWidth(qreal width);
    qreal trackLabelsWidth() const { return m_trackLabelsWidth; }
    void setTrackLabelsWidth(qreal width);
    QStringList timelineToolbarItems() const { return m_timelineToolbarItems; }
    QStringList timelineMenuItems() const { return m_timelineMenuItems; }
    Q_INVOKABLE void setTimelineToolbarLayout(const QStringList &toolbarItems,
                                              const QStringList &menuItems);

    bool reopenLastProject() const { return m_reopenLastProject; }
    void setReopenLastProject(bool enabled);
    bool vaapiZeroCopy() const { return m_vaapiZeroCopy; }
    void setVaapiZeroCopy(bool enabled);
    bool vaapiZeroCopySupported() const;
    bool mediaCodecZeroCopy() const { return m_mediaCodecZeroCopy; }
    void setMediaCodecZeroCopy(bool enabled);
    bool mediaCodecZeroCopySupported() const;
    QString preferredGpu() const { return m_preferredGpu; }
    void setPreferredGpu(const QString &id);
    bool gpuPreferenceSupported() const;
    bool gpuPreferenceInSystemSettings() const;
    bool invertTimelineScroll() const { return m_invertTimelineScroll; }
    void setInvertTimelineScroll(bool enabled);

    QString uiLanguage() const { return m_uiLanguage; }
    QVariantList uiLanguages() const;
    bool needsUiLanguagePrompt() const { return m_needsUiLanguagePrompt; }
    void setUiLanguage(const QString &language);
    // First-launch chooser: persist the pick and never ask again. Settings uses setUiLanguage.
    Q_INVOKABLE void chooseUiLanguage(const QString &code);
    double uiScale() const { return m_uiScale; }
    double appliedUiScale() const;
    bool uiScaleNeedsRestart() const;
    void setUiScale(double scale);
    // Snaps to 1.0, 1.25, 1.5, 1.75, or 2.0. Safe before any PreferencesController exists.
    static double storedUiScale();
    // Writes QT_SCALE_FACTOR from ui/scale unless the environment already set one.
    // Call once before QApplication; organization/application names must already be set.
    static void applyStoredUiScale();
    // Installs the .qm for ui/language (or the system locale). Call once after QApplication
    // is named, and again from setUiLanguage. Safe before any PreferencesController exists.
    static void installUiTranslators();

signals:
    void darkModePreferenceChanged();
    void workspaceLayoutPreferenceChanged();
    void timelineOverviewVisibleChanged();
    void audioMixerVisibleChanged();
    void audioMixerWidthChanged();
    void trackLabelsWidthChanged();
    void timelineToolbarLayoutChanged();
    void reopenLastProjectChanged();
    void vaapiZeroCopyChanged();
    void mediaCodecZeroCopyChanged();
    void preferredGpuChanged();
    void invertTimelineScrollChanged();
    void uiLanguageChanged();
    void uiScaleChanged();
    // setVaapiZeroCopy, setMediaCodecZeroCopy and setPreferredGpu take effect on the next
    // launch. AppController turns this into a status toast.
    void restartNoticeRequested(const QString &text);

private:
    bool m_darkModeOverridden = false;
    bool m_darkModePreferred = true;
    bool m_workspaceLayoutOverridden = false;
    QString m_workspaceLayoutPreferred = QStringLiteral("landscape");
    bool m_timelineOverviewVisible = false;
    bool m_audioMixerVisible = false;
    qreal m_audioMixerWidth = 0;
    qreal m_trackLabelsWidth = 130;
    QStringList m_timelineToolbarItems;
    QStringList m_timelineMenuItems;
    bool m_reopenLastProject = false;
    bool m_vaapiZeroCopy = false;
    bool m_mediaCodecZeroCopy = false;
    QString m_preferredGpu = QStringLiteral("auto");
    bool m_invertTimelineScroll = false;
    QString m_uiLanguage;
    bool m_needsUiLanguagePrompt = false;
    double m_uiScale = 1.0;
};
