#include "app/jobs/vision/RestoreController.h"
#include "app/bin/MediaBinController.h"

#include "app/editor/AppController.h"
#include "ui/imageproviders/segment/SegmentImageStore.h"
#include "engine/decode/readers/ClipReaderPool.h"
#include "engine/encode/MediaEditor.h"
#include "engine/decode/probe/MediaProbe.h"
#include "engine/ml/vision/Restorer.h"
#include "app/jobs/JobRegistry.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>
#include <QSettings>
#include <QUrl>
#include <QtConcurrent>

#include <algorithm>

RestoreController::RestoreController(AppController *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
}

namespace {

constexpr quint64 kRestorePreviewStreamId = 0xA5'11'5C'A4'00'00'00'0Aull;

int clipIndexById(const drift::Track &track, const QString &id)
{
    for (int i = 0; i < track.clips.size(); ++i) {
        if (track.clips.at(i).id == id)
            return i;
    }
    return -1;
}

} // namespace

QVariantList RestoreController::models()
{
    QVariantList out;
    for (const drift::Restorer::Model &m : drift::Restorer::models()) {
        out.append(QVariantMap{{QStringLiteral("id"), m.id},
                               {QStringLiteral("name"), m.name},
                               {QStringLiteral("task"), m.task},
                               {QStringLiteral("scale"), m.scale},
                               {QStringLiteral("custom"), m.custom},
                               {QStringLiteral("content"), m.content},
                               {QStringLiteral("summary"), m.summary},
                               {QStringLiteral("thumbnail"),
                                m.thumbnail.isEmpty() ? QString()
                                                      : QUrl::fromLocalFile(m.thumbnail).toString()},
                               {QStringLiteral("secondsPerMegapixel"), m.secondsPerMegapixel}});
    }
    return out;
}

QString RestoreController::modelsFolder()
{
    const QString dir = drift::Restorer::customModelsDir();
    QDir().mkpath(dir);
    return dir;
}

std::optional<RestoreController::RestoreSource> RestoreController::source(const QString &targetId) const
{
    if (targetId.isEmpty())
        return std::nullopt;
    for (const drift::Track &track : m_app->m_project.tracks()) {
        const int index = clipIndexById(track, targetId);
        if (index < 0)
            continue;
        const drift::Clip &clip = track.clips.at(index);
        if (clip.type != drift::ClipType::Video || clip.path.isEmpty() || clip.srcOut <= clip.srcIn)
            return std::nullopt;
        return RestoreSource{clip.path, clip.srcIn, clip.srcOut, clip.rotationCorrection,
                             m_app->m_project.asset(clip.assetId)};
    }
    const drift::MediaAsset *asset = m_app->m_project.asset(targetId);
    if (!asset || asset->kind != drift::MediaKind::Video || asset->path.isEmpty()
        || asset->durationUs <= 0)
        return std::nullopt;
    return RestoreSource{asset->path, 0, asset->durationUs, drift::rotationCorrectionOf(*asset),
                         asset};
}

QString RestoreController::run(const QString &targetId, const QString &decompressModel,
                               const QString &upscaleModel)
{
    const std::optional<RestoreSource> src = source(targetId);
    if (!src) {
        m_app->setLastMessage(tr("Select a video clip to enhance"), QStringLiteral("warning"));
        return {};
    }
    if (decompressModel.isEmpty() && upscaleModel.isEmpty()) {
        m_app->setLastMessage(tr("Choose a model to enhance with"), QStringLiteral("warning"));
        return {};
    }
    if (m_app->m_jobs->hasActive(QStringLiteral("restore"), targetId)) {
        m_app->setLastMessage(tr("This clip is already being enhanced"), QStringLiteral("warning"));
        return {};
    }

    const drift::MediaAsset *source = src->asset;
    const QString sourceName = source ? source->name : QFileInfo(src->path).completeBaseName();
    const QString folderId = source ? source->folderId : m_app->m_bin->currentFolderId();
    drift::MediaEditSpec spec;
    spec.inputPath = src->path;
    spec.outputPath = drift::newEditedMediaPath(m_app->m_project.id(), QStringLiteral("video"));
    spec.kind = QStringLiteral("video");
    spec.inSeconds = drift::usToSeconds(src->srcIn);
    spec.outSeconds = drift::usToSeconds(src->srcOut);
    spec.rotationOverride = source ? source->rotationOverride : -1;
    auto info = std::make_shared<MediaInfo>();
    auto scale = std::make_shared<int>(1);
    QPointer<RestoreController> self(this);

    const QString id = m_app->m_jobs->start(
        QStringLiteral("restore"), targetId, JobRegistry::Lane::Model,
        [self, spec, targetId, decompressModel, upscaleModel, info, scale](JobContext &ctx) mutable {
            const auto notify = [self, targetId]() {
                QMetaObject::invokeMethod(
                    self.data(),
                    [self, targetId]() {
                        if (self)
                            emit self->jobChanged(targetId);
                    },
                    Qt::QueuedConnection);
            };
            ctx.progress(0.0, QObject::tr("Loading the models…"));
            notify();

            QList<std::shared_ptr<drift::Restorer>> chain;
            QString error;
            if (!drift::Restorer::loadChain({decompressModel, upscaleModel}, &chain, scale.get(), &error))
                return ctx.fail(QStringLiteral("model_error"), error);

            const auto cancelled = [&ctx](double) { return !ctx.cancelled(); };
            spec.frameHook = [&](QImage &frame, QString *frameError) {
                return drift::Restorer::processChain(chain, frame, cancelled, frameError);
            };
            int lastPermille = -1;
            QElapsedTimer timer;
            timer.start();
            const bool ok = drift::editMedia(spec, &error, [&](double fraction) {
                const int permille = int(fraction * 1000);
                if (permille != lastPermille) {
                    lastPermille = permille;
                    const QString percent = QString::number(fraction * 100, 'f', 1);
                    // Extrapolated from the pace so far, once there is enough of it to go on.
                    const double elapsed = timer.elapsed() / 1000.0;
                    QString status;
                    if (fraction > 0.005 && elapsed > 10.0) {
                        const qint64 left = qint64(elapsed * (1.0 - fraction) / fraction);
                        const QString eta =
                            left < 90 ? QObject::tr("%1 s").arg(left)
                            : left < 5400 ? QObject::tr("%1 min").arg((left + 30) / 60)
                                          : QObject::tr("%1 h %2 min").arg(left / 3600).arg(left % 3600 / 60);
                        status = QObject::tr("Enhancing… %1% — about %2 left").arg(percent, eta);
                    } else {
                        status = QObject::tr("Enhancing… %1%").arg(percent);
                    }
                    ctx.progress(fraction, status);
                    notify();
                }
                return !ctx.cancelled();
            });
            if (ctx.cancelled())
                return;
            if (!ok)
                return ctx.fail(QStringLiteral("render_error"), error);
            *info = MediaProbe::probe(spec.outputPath);
            ctx.succeed({{QStringLiteral("path"), spec.outputPath}});
        },
        [this, targetId, spec, sourceName, folderId, info, scale](const QJsonObject &job) {
            if (job.value(QStringLiteral("ok")).toBool()) {
                m_app->addRenderedVideoAsset(spec.outputPath,
                                             *scale > 1 ? tr("%1 (enhanced %2x)").arg(sourceName).arg(*scale)
                                                        : tr("%1 (enhanced)").arg(sourceName),
                                             folderId, *info, tr("Enhance Video"));
                m_app->setLastMessage(tr("Enhanced video added to the media bin"));
            } else {
                QFile::remove(spec.outputPath);
                const QJsonObject error = job.value(QStringLiteral("error")).toObject();
                if (error.value(QStringLiteral("code")).toString() != QLatin1String("cancelled"))
                    m_app->setLastMessage(error.value(QStringLiteral("message")).toString(),
                                          QStringLiteral("error"));
            }
            emit jobChanged(targetId);
            return QJsonObject{};
        });
    emit jobChanged(targetId);
    m_app->setLastMessage(tr("Enhancing video…"));
    return id;
}

void RestoreController::cancel(const QString &targetId)
{
    for (const QJsonValue &v : m_app->m_jobs->jobs()) {
        const QJsonObject job = v.toObject();
        const QString kind = job.value(QStringLiteral("kind")).toString();
        if ((kind == QLatin1String("restore") || kind == QLatin1String("restore-preview"))
            && job.value(QStringLiteral("target")).toString() == targetId
            && job.value(QStringLiteral("active")).toBool()) {
            m_app->m_jobs->cancel(job.value(QStringLiteral("id")).toString());
        }
    }
}

void RestoreController::setPreviewFrame(const QString &targetId, double offsetSeconds)
{
    const std::optional<RestoreSource> src = source(targetId);
    if (!src)
        return;
    // The decode pool is shared with playback.
    m_app->setPlaying(false);

    m_frameLoading = true;
    m_enhancedSize = QSize();
    SegmentImageStore::setRestoreEnhanced(QImage());
    ++m_previewRevision;
    emit previewChanged();

    // A dragged slider asks faster than full-resolution decodes finish.
    if (m_decodeBusy) {
        m_pendingFrame = std::make_pair(targetId, offsetSeconds);
        return;
    }
    m_decodeBusy = true;

    const quint64 generation = ++m_previewGeneration;
    const QString path = src->path;
    const drift::TimeUs sourceUs =
        std::clamp(src->srcIn + drift::secondsToUs(offsetSeconds), src->srcIn,
                   std::max(src->srcIn, src->srcOut - 1));
    const int rotationCorrection = src->rotationCorrection;

    QPointer<RestoreController> self(this);
    (void)QtConcurrent::run([self, generation, path, sourceUs, rotationCorrection]() {
        // Bounded only to keep a pathological file from allocating without limit: the preview has
        // to show the source's own pixels, the ones the models will see.
        const QImage frame = ClipReaderPool::instance().readVideoFrame(
            path, kRestorePreviewStreamId, sourceUs, 8192, 8192, rotationCorrection);
        QMetaObject::invokeMethod(
            self.data(),
            [self, generation, frame]() {
                if (!self)
                    return;
                self->m_decodeBusy = false;
                if (generation == self->m_previewGeneration) {
                    self->m_frameLoading = self->m_pendingFrame.has_value();
                    self->m_originalSize = frame.size();
                    SegmentImageStore::setRestoreOriginal(frame);
                    ++self->m_previewRevision;
                    emit self->previewChanged();
                }
                if (self->m_pendingFrame) {
                    const auto [target, offset] = *self->m_pendingFrame;
                    self->m_pendingFrame.reset();
                    self->setPreviewFrame(target, offset);
                }
            },
            Qt::QueuedConnection);
    });
}

QString RestoreController::preview(const QString &targetId, const QString &decompressModel,
                                   const QString &upscaleModel)
{
    if (!source(targetId))
        return {};
    const QImage original = SegmentImageStore::restoreOriginal();
    if (original.isNull() || m_frameLoading)
        return {};
    if (decompressModel.isEmpty() && upscaleModel.isEmpty()) {
        m_app->setLastMessage(tr("Choose a model to enhance with"), QStringLiteral("warning"));
        return {};
    }
    if (m_app->m_jobs->hasActive(QStringLiteral("restore"), targetId)
        || m_app->m_jobs->hasActive(QStringLiteral("restore-preview"), targetId)) {
        return {};
    }

    const quint64 generation = m_previewGeneration;
    auto result = std::make_shared<QImage>();
    auto elapsed = std::make_shared<double>(0.0);
    QPointer<RestoreController> self(this);

    const QString id = m_app->m_jobs->start(
        QStringLiteral("restore-preview"), targetId, JobRegistry::Lane::Model,
        [self, targetId, decompressModel, upscaleModel, original, result, elapsed](JobContext &ctx) {
            const auto notify = [self, targetId]() {
                QMetaObject::invokeMethod(
                    self.data(),
                    [self, targetId]() {
                        if (self)
                            emit self->jobChanged(targetId);
                    },
                    Qt::QueuedConnection);
            };
            ctx.progress(0.0, QObject::tr("Loading the models…"));
            notify();

            QList<std::shared_ptr<drift::Restorer>> chain;
            QString loadError;
            if (!drift::Restorer::loadChain({decompressModel, upscaleModel}, &chain, nullptr, &loadError))
                return ctx.fail(QStringLiteral("model_error"), loadError);

            QImage frame = original;
            QElapsedTimer timer;
            timer.start();
            for (int i = 0; i < chain.size(); ++i) {
                QString error;
                const bool ok = chain.at(i)->process(frame, [&](double fraction) {
                    ctx.progress((i + fraction) / chain.size(),
                                 QObject::tr("Enhancing this frame… %1%")
                                     .arg(int((i + fraction) / chain.size() * 100)));
                    notify();
                    return !ctx.cancelled();
                }, &error);
                if (ctx.cancelled())
                    return;
                if (!ok)
                    return ctx.fail(QStringLiteral("model_error"), error);
            }
            *elapsed = timer.elapsed() / 1000.0;
            *result = frame;
            ctx.succeed({});
        },
        [this, targetId, generation, result, elapsed, decompressModel, upscaleModel,
         megapixels = original.width() * original.height() / 1e6](const QJsonObject &job) {
            if (job.value(QStringLiteral("ok")).toBool())
                calibrateSpeed({decompressModel, upscaleModel}, megapixels, *elapsed);
            if (job.value(QStringLiteral("ok")).toBool() && generation == m_previewGeneration
                && !m_frameLoading) {
                m_enhancedSize = result->size();
                SegmentImageStore::setRestoreEnhanced(*result);
                ++m_previewRevision;
                emit previewChanged();
            }
            emit jobChanged(targetId);
            return QJsonObject{};
        });
    emit jobChanged(targetId);
    return id;
}

void RestoreController::calibrateSpeed(const QStringList &modelIds, double megapixels,
                                       double seconds)
{
    double reference = 0.0;
    for (const QString &id : modelIds) {
        if (id.isEmpty())
            continue;
        double spm = 0.0;
        for (const drift::Restorer::Model &m : drift::Restorer::models()) {
            if (m.id == id)
                spm = m.secondsPerMegapixel;
        }
        // A model without a reference speed leaves nothing to compare against.
        if (spm <= 0.0)
            return;
        reference += spm * megapixels;
    }
    if (reference <= 0.0 || seconds <= 0.0)
        return;
    // Averaged with the last reading: one frame is a noisy sample of a loaded machine.
    QSettings settings;
    const double measured = seconds / reference;
    const bool calibrated = settings.value(QStringLiteral("restore/speedCalibrated")).toBool();
    const double previous = settings.value(QStringLiteral("restore/speedFactor"), measured).toDouble();
    settings.setValue(QStringLiteral("restore/speedFactor"),
                      calibrated ? (previous + measured) / 2.0 : measured);
    settings.setValue(QStringLiteral("restore/speedCalibrated"), true);
}

QVariantMap RestoreController::speed() const
{
    QSettings settings;
    return {{QStringLiteral("factor"), settings.value(QStringLiteral("restore/speedFactor"), 1.0).toDouble()},
            {QStringLiteral("calibrated"),
             settings.value(QStringLiteral("restore/speedCalibrated"), false).toBool()}};
}

QVariantMap RestoreController::targetInfo(const QString &targetId) const
{
    const std::optional<RestoreSource> src = source(targetId);
    if (!src)
        return {};
    int width = src->asset ? src->asset->width : 0;
    int height = src->asset ? src->asset->height : 0;
    if (src->asset) {
        const int rotation = drift::effectiveRotation(*src->asset) % 180;
        if (rotation == 90)
            std::swap(width, height);
    }
    const double fps = src->asset && src->asset->fps > 0 ? src->asset->fps : m_app->m_project.fps();
    return {{QStringLiteral("seconds"), drift::usToSeconds(src->srcOut - src->srcIn)},
            {QStringLiteral("width"), width},
            {QStringLiteral("height"), height},
            {QStringLiteral("fps"), fps}};
}

QVariantMap RestoreController::previewState() const
{
    return {{QStringLiteral("revision"), m_previewRevision},
            {QStringLiteral("width"), m_originalSize.width()},
            {QStringLiteral("height"), m_originalSize.height()},
            {QStringLiteral("enhancedWidth"), m_enhancedSize.width()},
            {QStringLiteral("enhancedHeight"), m_enhancedSize.height()},
            {QStringLiteral("loading"), m_frameLoading}};
}

void RestoreController::endPreview()
{
    ++m_previewGeneration;
    m_pendingFrame.reset();
    m_frameLoading = false;
    m_originalSize = QSize();
    m_enhancedSize = QSize();
    SegmentImageStore::setRestoreOriginal(QImage());
    SegmentImageStore::setRestoreEnhanced(QImage());
    ++m_previewRevision;
    emit previewChanged();
}

QVariantMap RestoreController::job(const QString &targetId) const
{
    const QJsonArray jobs = m_app->m_jobs->jobs();
    for (qsizetype i = jobs.size() - 1; i >= 0; --i) {
        const QJsonObject job = jobs.at(i).toObject();
        const QString kind = job.value(QStringLiteral("kind")).toString();
        if ((kind != QLatin1String("restore") && kind != QLatin1String("restore-preview"))
            || job.value(QStringLiteral("target")).toString() != targetId) {
            continue;
        }
        return {{QStringLiteral("kind"), kind},
                {QStringLiteral("active"), job.value(QStringLiteral("active")).toBool()},
                {QStringLiteral("progress"), job.value(QStringLiteral("progress")).toDouble()},
                {QStringLiteral("status"), job.value(QStringLiteral("status")).toString()},
                {QStringLiteral("error"), job.value(QStringLiteral("error"))
                                              .toObject()
                                              .value(QStringLiteral("message"))
                                              .toString()}};
    }
    return {};
}

#include "moc_RestoreController.cpp"
