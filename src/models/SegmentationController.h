#pragma once

#include "core/Time.h"
#include "engine/Sam2Segmenter.h"

#include <QAtomicInt>
#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <optional>

class AppController;
class EditorStateTest;

// Cutout pipeline and the interactive prompting session, reached from QML as EditorState.segmentation.
// The session encodes one reference frame off the GUI thread; point edits after that only re-run
// the cheap decoder. segmentClip() is the non-interactive path (MCP and the masks tab).
class SegmentationController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool sessionActive READ sessionActive NOTIFY sessionChanged)
    Q_PROPERTY(bool forTemplate READ forTemplate NOTIFY sessionChanged)
    Q_PROPERTY(bool encoding READ encoding NOTIFY sessionChanged)
    Q_PROPERTY(int revision READ revision NOTIFY sessionChanged)
    Q_PROPERTY(QVariantList points READ points NOTIFY sessionChanged)
    Q_PROPERTY(QString backend READ backend NOTIFY sessionChanged)
    Q_PROPERTY(bool backendUsesPoints READ backendUsesPoints NOTIFY sessionChanged)
    Q_PROPERTY(QSize frameSize READ frameSize NOTIFY sessionChanged)

public:
    explicit SegmentationController(AppController *app, QObject *parent = nullptr);

    bool running() const { return m_segmenting; }
    double progress() const { return m_segmentProgress; }
    QString status() const { return m_segmentStatus; }
    bool sessionActive() const { return m_segSessionActive; }
    bool forTemplate() const { return m_segForTemplate; }
    bool encoding() const { return m_segEncoding; }
    int revision() const { return m_segRevision; }
    QVariantList points() const { return m_segPoints; }
    QString backend() const { return m_segBackend; }
    bool backendUsesPoints() const { return m_segBackend == QLatin1String("sam2"); }
    QSize frameSize() const { return m_segFrame.size(); }

    // Which cutout models are installed: "sam2" (click to pick anything) and/or "rvm" (people,
    // automatic). Empty when neither is.
    Q_INVOKABLE QStringList backends();
    // Installed RVM model variants, best first: "mobilenetv3", "resnet50".
    Q_INVOKABLE QStringList rvmQualities();
    Q_INVOKABLE void setBackend(const QString &backend, const QString &quality = {});
    // points: [{x, y, include}] with x/y normalized to the source frame.
    // outputMode: "clips" (foreground + background on two new tracks) or "mask" (in place).
    Q_INVOKABLE void segmentClip(int trackIndex, int clipIndex, const QVariantList &points,
                                 const QString &outputMode, const QString &backend = {},
                                 const QString &quality = {});
    Q_INVOKABLE void cancel();
    Q_INVOKABLE bool available();
    Q_INVOKABLE QString modelVariant();

    // Interactive prompting session. beginSession encodes the reference frame off the GUI thread.
    Q_INVOKABLE void beginSession(int trackIndex, int clipIndex, double seconds,
                                  bool forTemplate = false);
    Q_INVOKABLE void endSession();
    // Called from AppController's effect-template flow. Not invokable.
    void openSegmentationForTemplate(int trackIndex, int clipIndex);
    Q_INVOKABLE void setFrame(double seconds);
    // Shows the frame at `seconds` while the frame slider is dragged, without the model pass that
    // setFrame runs on release. Requests made while one decodes collapse into the newest.
    Q_INVOKABLE void scrubFrame(double seconds);
    Q_INVOKABLE void addPoint(double x, double y, bool include);
    Q_INVOKABLE void removePoint(int index);
    Q_INVOKABLE void clearPoints();
    Q_INVOKABLE void runSession(const QString &outputMode);

signals:
    void runningChanged();
    void progressChanged();
    void statusChanged();
    void finished(bool ok, const QString &message);
    void sessionChanged();
    void openWindowRequested(int trackIndex, int clipIndex, double startSeconds,
                             double durationSeconds);

private:
    // Segmentation completion is normally reached only through a finished worker, which needs a
    // real backend installed. The test drives it directly instead.
    friend class EditorStateTest;

    void refreshPreview();
    void runSeed(int generation);
    // Completes a segmentation job: pins the matte to the clip as a Mask adjustment on its own
    // lane. Both output modes land here; "clips" is accepted and treated as a mask.
    void finalizeSegmentation(const QString &clipId, const QString &mattePath,
                              const QString &matteFgrPath, drift::TimeUs matteSrcOffsetUs,
                              const QString &outputMode);

    AppController *m_app = nullptr;
    bool m_segmenting = false;
    double m_segmentProgress = 0.0;
    QString m_segmentStatus;
    QAtomicInt m_segmentCancel = 0;
    bool m_segSessionActive = false;
    bool m_segForTemplate = false;
    bool m_segEncoding = false;
    int m_segTrack = -1;
    int m_segClip = -1;
    double m_segSeconds = 0.0;
    int m_segRevision = 0;
    bool m_segScrubBusy = false;
    std::optional<double> m_segScrubPending;
    int m_segGeneration = 0;
    int m_segSeedGeneration = 0;
    bool m_segSeedRunning = false;
    QImage m_segFrame;
    drift::Sam2Embedding m_segEmbedding;
    QString m_segBackend = QStringLiteral("sam2");
    QString m_segQuality;
    QVariantList m_segPoints;
};
