#pragma once

#include <QImage>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

namespace drift {

// Single-image restoration models — compression removal (1x) and super-resolution (2x/4x) — run on
// ONNX Runtime one video frame at a time.
//
// Two sources of models. Addons of kind "restore-model" carry a constants.json naming the task and
// scale, either for one model or as a "models" array for a pack of them. Any other .onnx dropped into customModelsDir() is offered too; its scale comes from the
// community naming convention ("4x_Something.onnx", "Something_x2.onnx") or, failing that, one small
// probe inference. Either way the graph must take NCHW float RGB in 0..1, fp32 or fp16, and return
// the same layout scaled up by a whole factor, which is what chaiNNer and the OpenModelDB exports
// produce.
//
// Frames are processed in overlapping tiles so a 4x pass over 1080p does not need gigabytes of
// activations at once.
class Restorer
{
    struct Impl;

public:
    struct Model
    {
        QString id; // constants.json "model" for addons; "custom:<file name>" otherwise
        QString name;
        QString task; // "decompress" (scale 1) or "upscale"
        int scale = 1;
        bool custom = false;
        QString path; // the .onnx file
        // Picker details, from constants.json; empty or 0 for custom models.
        QStringList content; // what it is made for: "anime", "live", "cg", "general"
        QString summary;
        QString thumbnail; // a before/after image, absolute path
        double secondsPerMegapixel = 0.0; // CPU time per megapixel of input on the reference machine
    };

    // Every usable model, addons first. The probe for an unnamed custom model builds a session, so
    // the first call after one is dropped in can block briefly; results are cached by mtime.
    static QList<Model> models();
    // <AppDataLocation>/models/restore, the folder a user drops their own .onnx files into.
    static QString customModelsDir();

    // Loads (or reuses) the model's session. nullptr with *error set when it cannot be used.
    // Blocks — never call this from the GUI thread.
    static std::shared_ptr<Restorer> load(const QString &id, QString *error);

    int scale() const;

    // Replaces `frame` (any format; converted to RGB888) with the restored picture, `scale()` times
    // larger. `progress` is called between tiles with 0..1 and returns false to stop.
    bool process(QImage &frame, const std::function<bool(double)> &progress, QString *error);

    ~Restorer();
    Restorer(const Restorer &) = delete;
    Restorer &operator=(const Restorer &) = delete;

private:
    Restorer();
    std::unique_ptr<Impl> d;
};

} // namespace drift
