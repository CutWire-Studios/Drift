#pragma once

#include <QImage>
#include <QList>
#include <QPointF>
#include <QString>
#include <QVector3D>

#include <array>
#include <memory>

namespace drift {

// Slices of FaceAnchors::contour. The loops a cosmetic shader needs are kept as a compact
// 128-point subset. The full 468-point mesh (no iris rings) is stored separately on
// FaceAnchors::mesh so the 3D face-mesh effect can warp; the 10 iris-ring vertices the
// attention head adds are still dropped, because nothing draws with them.
//
// Every loop is closed: the last point connects back to the first. The eyelid arcs are slices of
// the eye rings rather than separate loops, so their points are stored once.
namespace contour {

struct Span
{
    int offset;
    int count;
};

inline constexpr Span kOval{0, 36};
inline constexpr Span kLipOuter{36, 20};
inline constexpr Span kLipInner{56, 20};
inline constexpr Span kEyeLeft{76, 16};
inline constexpr Span kEyeRight{92, 16};
inline constexpr Span kBrowLeft{108, 10};
inline constexpr Span kBrowRight{118, 10};
inline constexpr int kTotalPoints = 128;

// Open arcs along the upper lids, which is where liner and shadow sit. Both eye rings are wound
// so that the first nine points run along the upper lid from inner to outer corner.
inline constexpr Span kEyeLeftUpper{76, 9};
inline constexpr Span kEyeRightUpper{92, 9};

} // namespace contour

// MediaPipe's 468-vertex mesh without the attention-head iris rings. FaceAnchors::mesh is empty or
// this long — never a partial set — so a sidecar can omit the blob entirely on older tracks.
inline constexpr int kFaceMeshPoints = 468;

// Raw MediaPipe mesh indices for the loops that anything outside the landmarker needs. These are
// the definitions the contour spans above are built from; they live here rather than in the .cpp
// because the face-swap alpha ramp seeds its BFS from the same rings and Face111 resamples them,
// and a second copy of these tables could silently drift from this one.
//
// HANDEDNESS: named in *image* space, matching FaceAnchors — "left" is the low-x side of the
// frame, not the subject's own left. MediaPipe names its sets from the subject's point of view,
// so kEyeLeftRing here is MediaPipe's FACEMESH_RIGHT_EYE.
namespace mpidx {

// FACEMESH_FACE_OVAL, the outer boundary contour.
inline constexpr std::array<int, 36> kFaceOval{10,  338, 297, 332, 284, 251, 389, 356, 454,
                                               323, 361, 288, 397, 365, 379, 378, 400, 377,
                                               152, 148, 176, 149, 150, 136, 172, 58,  132,
                                               93,  234, 127, 162, 21,  54,  103, 67,  109};

inline constexpr std::array<int, 20> kLipInner{78,  191, 80,  81,  82,  13,  312, 311, 310, 415,
                                               308, 324, 318, 402, 317, 14,  87,  178, 88,  95};

// Wound from the inner corner along the upper lid to the outer corner, then back along the lower
// lid. contour::kEyeLeftUpper depends on the first nine points being the upper lid.
inline constexpr std::array<int, 16> kEyeLeftRing{33,  246, 161, 160, 159, 158, 157, 173,
                                                  133, 155, 154, 153, 145, 144, 163, 7};
inline constexpr std::array<int, 16> kEyeRightRing{362, 398, 384, 385, 386, 387, 388, 466,
                                                   263, 249, 390, 373, 374, 380, 381, 382};

inline constexpr std::array<int, 20> kLipOuter{61,  185, 40,  39,  37,  0,   267, 269, 270, 409,
                                               291, 375, 321, 405, 314, 17,  84,  181, 91,  146};

inline constexpr std::array<int, 10> kBrowLeftRing{107, 66, 105, 63, 70, 46, 53, 52, 65, 55};
inline constexpr std::array<int, 10> kBrowRightRing{336, 296, 334, 293, 300,
                                                    276, 283, 282, 295, 285};

// MediaPipe has no cheek contour. Four mid-cheek vertices averaged is much steadier than any one
// of them, and blush only needs a centre.
inline constexpr std::array<int, 4> kCheekLeft{50, 101, 205, 36};
inline constexpr std::array<int, 4> kCheekRight{280, 330, 425, 266};

} // namespace mpidx

// Everything a face shader needs, in normalized frame coordinates (0..1, top-left origin).
struct FaceAnchors
{
    bool valid = false;

    // Iris centres. Left and right are as seen in the image, not the subject's own left and right:
    // the model's ROI is eye-aligned before landmarking, so its "left" output always lands on the
    // lower-x side of the crop.
    QPointF leftEye;
    QPointF rightEye;
    QPointF noseTip;
    QPointF mouthCenter;
    QPointF mouthLeft;
    QPointF mouthRight;
    QPointF chin;
    QPointF forehead;

    QPointF faceCenter;      // centroid of the face oval
    double faceRx = 0.0;     // half-axes of the oval, measured in the face's own rotated frame
    double faceRy = 0.0;
    double angle = 0.0;      // radians; rotation of the eye line away from horizontal
    double eyeRadius = 0.0;  // iris radius, the natural falloff scale for eye warps
    double score = 0.0;      // detector confidence, kept for tracking decisions

    // Contour loops, indexed by the spans above. Unlike the anchors, these are stored already in
    // width-normalized space (uv with y scaled by the frame aspect) — every shader that uses them
    // does distance maths, and pre-converting means the SDF is correct without a per-point step.
    //
    // Either empty or exactly contour::kTotalPoints long. Never partially filled.
    bool hasContours = false;
    QList<QPointF> contour;
    QPointF cheekLeft;  // uv, like the other anchors. MediaPipe has no cheek contour, so these
    QPointF cheekRight; // are centroids of four mid-cheek vertices, which are far steadier.

    // Head orientation as a unit quaternion rather than Euler angles: pitch reaches +/-90 when
    // someone looks at the floor, and three separate seam handlers in both the interpolator and
    // the smoother is more code than one nlerp.
    bool hasPose = false;
    double poseQx = 0.0;
    double poseQy = 0.0;
    double poseQz = 0.0;
    double poseQw = 1.0;
    double poseScale = 0.0; // interocular distance, width-normalized: the head's own unit of length
    double poseOx = 0.0;    // origin (eye midpoint), width-normalized 3D
    double poseOy = 0.0;
    double poseOz = 0.0;

    // Full MediaPipe face mesh, width-normalized (uv.x, uv.y*aspect, z). Same units as poseOx/Oy/Oz.
    // Empty or exactly 468. Never partial.
    bool hasMesh = false;
    QList<QVector3D> mesh;
};

// MediaPipe's face mesh on ONNX Runtime: YuNet v2 finds faces, face_landmark_with_attention turns
// each ROI into 468 mesh points plus refined iris rings.
//
// The upstream 030_BlazeFace package the MediaPipe pipeline normally pairs with ships no ONNX
// export, so detection uses YuNet instead. Its five keypoints (eyes, nose, mouth corners) supply
// the same eye-line rotation the landmark model's ROI convention expects.
//
// All work is synchronous on the calling thread; callers run it off the GUI thread.
class FaceLandmarker
{
    struct Impl;

public:
    static FaceLandmarker &instance();

    // Loads both sessions on first use. False if the models are missing or failed to load (see
    // lastError()). Blocks for a moment — never call this from the GUI thread.
    bool available();
    QString lastError() const;

    // Cheap file-existence check that constructs no ONNX session. This is what UI gating must use.
    static bool modelPresent();

    // Detects every face in the frame, largest first, capped at maxFaces().
    //
    // `hint` is the previous frame's result. When a hinted face is still confident, its ROI is
    // derived from those anchors and the detector is skipped for it — detection is the expensive
    // and jittery half, and this is MediaPipe's own strategy. Faces are matched to the hint by
    // centre distance so a given slot keeps following the same person across a clip.
    QList<FaceAnchors> detect(const QImage &frame, const QList<FaceAnchors> *hint = nullptr);

    static int maxFaces();

    FaceLandmarker(const FaceLandmarker &) = delete;
    FaceLandmarker &operator=(const FaceLandmarker &) = delete;

private:
    FaceLandmarker();
    ~FaceLandmarker();

    std::unique_ptr<Impl> d;
};

} // namespace drift
