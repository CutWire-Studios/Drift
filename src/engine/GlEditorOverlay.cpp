#include "engine/GlEditorOverlay.h"

#include <QOpenGLShaderProgram>

#include <cmath>
#include <vector>

namespace drift::gl {

namespace {

constexpr const char *kLineVert = R"(#version 330 core
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec4 a_color;
out vec4 v_color;
void main() {
    v_color = a_color;
    gl_Position = vec4(a_pos, 0.0, 1.0);
}
)";

constexpr const char *kLineFrag = R"(#version 330 core
in vec4 v_color;
out vec4 fragColor;
void main() {
    fragColor = v_color;
}
)";

} // namespace

void drawEditorLines(GlRuntime &rt, QOpenGLExtraFunctions *gl, GlTarget &canvas,
                     const QMatrix4x4 &viewProj, const QList<EditorLine> &lines, bool under)
{
    if (!gl || !canvas.isValid() || lines.isEmpty())
        return;
    const float w = float(canvas.width);
    const float h = float(canvas.height);

    // x, y in NDC, then premultiplied rgba: six vertices per line.
    std::vector<float> vertices;
    vertices.reserve(size_t(lines.size()) * 6 * 6);
    const auto vertex = [&](float x, float y, const QColor &c) {
        const float a = float(c.alphaF());
        vertices.insert(vertices.end(), {x / w * 2.f - 1.f, y / h * 2.f - 1.f, float(c.redF()) * a,
                                         float(c.greenF()) * a, float(c.blueF()) * a, a});
    };
    for (const EditorLine &line : lines) {
        if (line.under != under)
            continue;
        QPointF a;
        QPointF b;
        if (!clipEditorSegment(viewProj, line.a, line.b, &a, &b))
            continue;
        const QPointF d = b - a;
        const double len = std::hypot(d.x(), d.y());
        if (len < 1e-3)
            continue;
        const QPointF n = QPointF(-d.y(), d.x()) / len * (line.widthPx * 0.5);
        const QPointF a0 = a + n, a1 = a - n, b0 = b + n, b1 = b - n;
        vertex(float(a0.x()), float(a0.y()), line.color);
        vertex(float(a1.x()), float(a1.y()), line.color);
        vertex(float(b0.x()), float(b0.y()), line.color);
        vertex(float(b0.x()), float(b0.y()), line.color);
        vertex(float(a1.x()), float(a1.y()), line.color);
        vertex(float(b1.x()), float(b1.y()), line.color);
    }
    if (vertices.empty())
        return;

    QOpenGLShaderProgram *program =
        rt.builtinProgram(QStringLiteral("__editor_lines__"), kLineVert, kLineFrag);
    if (!program)
        return;

    GLuint vao = 0;
    GLuint vbo = 0;
    gl->glGenVertexArrays(1, &vao);
    gl->glGenBuffers(1, &vbo);
    gl->glBindVertexArray(vao);
    gl->glBindBuffer(GL_ARRAY_BUFFER, vbo);
    gl->glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(vertices.size() * sizeof(float)), vertices.data(),
                     GL_STREAM_DRAW);
    gl->glEnableVertexAttribArray(0);
    gl->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    gl->glEnableVertexAttribArray(1);
    gl->glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                              reinterpret_cast<void *>(2 * sizeof(float)));

    canvas.fbo->bind();
    gl->glViewport(0, 0, canvas.width, canvas.height);
    gl->glDisable(GL_DEPTH_TEST);
    gl->glEnable(GL_BLEND);
    gl->glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    program->bind();
    gl->glDrawArrays(GL_TRIANGLES, 0, GLsizei(vertices.size() / 6));
    program->release();
    gl->glDisable(GL_BLEND);
    canvas.fbo->release();

    gl->glBindVertexArray(0);
    gl->glBindBuffer(GL_ARRAY_BUFFER, 0);
    gl->glDeleteBuffers(1, &vbo);
    gl->glDeleteVertexArrays(1, &vao);
}

} // namespace drift::gl
