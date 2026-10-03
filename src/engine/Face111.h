#pragma once

#include <QList>
#include <QVector>
#include <QVector3D>

#include <array>
#include <cstdint>

// The 111-point face layout GPUPixel's makeup filters draw over (Face++'s 106 points plus a mouth
// centre, two brow centres and two cheek points), rebuilt from Drift's 468-point MediaPipe mesh.
//
// The triangle list and reference coordinates are copied verbatim from GPUPixel
// (https://github.com/pixpark/gpupixel, src/filter/face_makeup_filter.cc),
// Copyright (c) 2021 PixPark, licensed under the Apache License, Version 2.0. See
// effects/face_retouch/NOTICE.
//
// Point order, in image space ("left" is the low-x side of the frame, like FaceAnchors):
//   0-32 jaw, image-left temple through the chin (16) to the image-right temple
//   33-37 / 38-42 upper brow edges, 64-67 / 68-71 lower brow edges, 107 / 108 brow centres
//   43-46 nose bridge to tip, 47-51 nostril line, 78-83 nose sides
//   52-57 + 72-73 left eye ring, 58-63 + 75-76 right eye ring, 74 / 77 and 104 / 105 eye centres
//   84-95 outer lip, 96-103 inner lip, 106 mouth centre
//   109 / 110 cheeks
namespace drift::face111 {

inline constexpr int kPoints = 111;

// Indices into the 111 points, three per triangle.
extern const std::array<uint32_t, 528> kTriangles;

// The template face the makeup PNGs were painted against: x, y in [0, 1] of a 1280x1280 frame.
// A template texture's bounds are given in those 1280 pixels.
extern const std::array<float, kPoints * 2> kReferenceUv;

// Maps a 468-point mesh (FaceAnchors::mesh, width-normalized) to the 111 points as interleaved
// x, y in the same space. `out` is resized to kPoints * 2; left empty unless mesh468 has exactly
// kFaceMeshPoints entries.
void fromMediaPipe(const QList<QVector3D> &mesh468, QVector<float> *out);

} // namespace drift::face111
