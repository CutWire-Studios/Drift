#pragma once

#include <QImage>
#include <QSize>
#include <QString>

// One decode path for still images, shared by the bin, the thumbnailer and the compositor.
//
// Qt's QImageReader is tried first and handles nearly everything. FFmpeg is the fallback, and it
// exists for two reasons: HEIC and AVIF have no Qt plugin in any official kit, and a kit built
// without qtimageformats silently loses webp and tiff — which is how released Android packages
// shipped with every .webp rendering as nothing. Since libavcodec is linked in anyway and already
// carries the HEVC, AV1 and webp decoders, the fallback costs no new dependency on any platform.
namespace drift {

// Whether Qt can decode this suffix in *this* build. Derived from
// QImageReader::supportedImageFormats(), so it answers for the plugins actually deployed rather
// than the formats the import whitelist claims. Cached; the plugin set cannot change at runtime.
bool qtCanDecodeStill(const QString &path);

// Decoded image, or a null QImage when nothing here can read the file. maxWidth/maxHeight bound
// the decode buffer and are a hint, not an exact size: the source is fitted into that box and
// never upscaled. 0 for either means "no bound". EXIF/display-matrix orientation is applied.
QImage decodeStillImage(const QString &path, int maxWidth = 0, int maxHeight = 0);

// Pixel dimensions with orientation applied, or an empty QSize when the file cannot be read.
// Does not decode when the metadata alone can answer.
QSize stillImageSize(const QString &path);

} // namespace drift
