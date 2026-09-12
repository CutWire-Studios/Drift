#pragma once

// Windows preview import of D3D11VA surfaces into OpenGL through WGL_NV_DX_interop2. Internal to
// GlRuntime; not compiled anywhere else.
//
// The decoder's surface is one NV12 slice of a texture array that only the decoder may bind, and
// the interop extension shares whole textures, not array slices or single planes. So each frame is
// copied out of the array on the decoder's own device and split into its luma and chroma planes by
// two small D3D11 draws, and those two render targets are what GL samples. They are still YUV, so
// GlRuntime's convert shader applies the same matrix and range it does on every other path — the
// driver never picks the colour conversion. Nothing touches system memory.

#include <QString>
#include <QtGui/qopengl.h>

#include <memory>

struct AVFrame;
class QOpenGLExtraFunctions;

namespace drift::gl {

class D3d11GlInterop
{
public:
    enum class Result {
        Locked,   // *texY / *texUV hold the frame until unlock()
        Declined, // this frame cannot be imported; the next one may be
        Failed,   // this machine or driver cannot do it at all; stop asking
    };

    D3d11GlInterop();
    ~D3d11GlInterop();

    // GL context current, frame in AV_PIX_FMT_D3D11. On Locked, texY carries luma in .r at the
    // frame's even-rounded size and texUV chroma in .rg at half that, rows top first like every
    // other upload path. `why` is filled for Declined when there is something worth reporting,
    // and always for Failed.
    Result lock(QOpenGLExtraFunctions *gl, const AVFrame *frame, GLuint *texY, GLuint *texUV,
                QString *why);

    // After the draw that sampled the textures. D3D11 cannot render the next frame into them
    // while GL holds the lock. Safe to call when nothing is locked.
    void unlock();

    // Drops every D3D11 object, interop registration and GL texture name. GL context current.
    void release(QOpenGLExtraFunctions *gl);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace drift::gl
