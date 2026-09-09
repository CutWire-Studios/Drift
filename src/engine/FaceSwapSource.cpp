#include "engine/FaceSwapSource.h"

#include "engine/FaceLandmarker.h"
#include "engine/FaceTrack.h"

#include <QFile>
#include "StillImage.h"


#include <algorithm>

namespace drift {

QImage loadFaceSwapPhoto(const QString &path)
{
    if (path.isEmpty())
        return {};

    // Phone photos are usually stored unrotated with an EXIF orientation tag; decodeStillImage
    // applies it. Without that the ingest and the renderer would still agree with each other,
    // but both would be sideways.
    QImage image = drift::decodeStillImage(path);
    if (image.isNull())
        return {};

    if (image.width() > kFaceSwapPhotoMaxEdge || image.height() > kFaceSwapPhotoMaxEdge) {
        image = image.scaled(kFaceSwapPhotoMaxEdge, kFaceSwapPhotoMaxEdge, Qt::KeepAspectRatio,
                             Qt::SmoothTransformation);
    }
    return image.convertToFormat(QImage::Format_RGBA8888);
}

bool ingestFaceSwapSource(const QString &photoPath, QString *errorOut)
{
    const auto fail = [errorOut](const QString &message) {
        if (errorOut)
            *errorOut = message;
        return false;
    };

    const QString sidecar = faceSwapSourcePath(photoPath);
    if (sidecar.isEmpty())
        return fail(QStringLiteral("Could not open the face swap cache"));

    const QImage photo = loadFaceSwapPhoto(photoPath);
    if (photo.isNull())
        return fail(QStringLiteral("Could not read that image"));

    FaceLandmarker &landmarker = FaceLandmarker::instance();
    if (!landmarker.available())
        return fail(landmarker.lastError());

    const QList<FaceAnchors> faces = landmarker.detect(photo);
    const bool anyMesh = std::any_of(faces.cbegin(), faces.cend(), [](const FaceAnchors &a) {
        return a.valid && a.hasMesh;
    });
    if (!anyMesh)
        return fail(QStringLiteral("No face found in that photo"));

    // A still photo's landmarks are exactly a one-frame face track, so this reuses the track
    // writer rather than inventing a second sidecar format. fps is nominal: nothing samples it.
    FaceTrack track;
    track.fps = 1;
    track.startSrcUs = 0;
    track.frames.append(FaceTrackFrame{faces});

    QString error;
    if (!writeFaceTrack(sidecar, track, &error))
        return fail(error.isEmpty() ? QStringLiteral("Could not cache the photo's landmarks") : error);
    return true;
}

bool faceSwapSourceReady(const QString &photoPath)
{
    const QString sidecar = faceSwapSourcePath(photoPath);
    return !sidecar.isEmpty() && QFile::exists(sidecar);
}

} // namespace drift
