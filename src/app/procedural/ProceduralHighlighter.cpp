#include "app/procedural/ProceduralHighlighter.h"

#include <QQuickTextDocument>
#include <QRegularExpression>
#include <QSet>
#include <QTextCharFormat>

namespace {

const QSet<QString> &luaKeywords()
{
    static const QSet<QString> set{
        "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto", "if", "in",
        "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while"};
    return set;
}

const QSet<QString> &luaBuiltins()
{
    static const QSet<QString> set{
        "geo", "mat", "buf", "noise", "rand", "scene", "camera", "params", "time", "frame", "seed", "canvas",
        "print", "math", "string", "table", "utf8", "pairs", "ipairs", "type", "tostring", "tonumber", "select",
        "error", "assert", "pcall", "next", "unpack"};
    return set;
}

const QSet<QString> &glslKeywords()
{
    static const QSet<QString> set{
        "if", "else", "for", "while", "do", "return", "break", "continue", "discard", "switch", "case", "default",
        "struct", "uniform", "in", "out", "inout", "const", "layout", "flat", "smooth", "precision", "highp",
        "mediump", "lowp", "true", "false", "attribute", "varying"};
    return set;
}

const QSet<QString> &glslTypes()
{
    static const QSet<QString> set{
        "void", "bool", "int", "uint", "float", "vec2", "vec3", "vec4", "ivec2", "ivec3", "ivec4", "uvec2",
        "uvec3", "uvec4", "bvec2", "bvec3", "bvec4", "mat2", "mat3", "mat4", "mat2x2", "mat3x3", "mat4x4",
        "sampler2D", "sampler3D", "samplerCube", "isampler2D", "usampler2D"};
    return set;
}

const QSet<QString> &glslBuiltins()
{
    static const QSet<QString> set{
        "gl_Position", "gl_FragCoord", "gl_FragDepth", "gl_FrontFacing", "gl_PointSize", "gl_VertexID",
        "gl_InstanceID", "normalize", "dot", "cross", "mix", "clamp", "max", "min", "pow", "sin", "cos", "tan",
        "abs", "length", "reflect", "refract", "smoothstep", "step", "floor", "ceil", "fract", "mod", "sqrt",
        "exp", "log", "sign", "texture", "transpose", "inverse", "distance", "atan", "asin", "acos", "dFdx",
        "dFdy", "fwidth", "round", "trunc"};
    return set;
}

bool isIdentStart(QChar c) { return c.isLetter() || c == QLatin1Char('_'); }
bool isIdentPart(QChar c) { return c.isLetterOrNumber() || c == QLatin1Char('_'); }

// Block state: 0 plain, 1 + n inside a long comment whose closer is `]` n `=` `]` (Lua) or `*/` (GLSL, n = 0).
int findLongCommentEnd(const QString &text, int from, int level, bool lua)
{
    if (!lua)
        return text.indexOf(QLatin1String("*/"), from);
    const QString closer = QLatin1Char(']') + QString(level, QLatin1Char('=')) + QLatin1Char(']');
    return text.indexOf(closer, from);
}

} // namespace

ProceduralHighlighter::ProceduralHighlighter(QObject *parent)
    : QSyntaxHighlighter(parent)
{
    connect(this, &ProceduralHighlighter::colorsChanged, this, [this] { rehighlight(); });
}

void ProceduralHighlighter::setTextDocument(QQuickTextDocument *document)
{
    if (m_quickDocument == document)
        return;
    m_quickDocument = document;
    setDocument(document ? document->textDocument() : nullptr);
    emit textDocumentChanged();
}

void ProceduralHighlighter::setLanguage(const QString &language)
{
    if (m_language == language)
        return;
    m_language = language;
    m_lang = language == QLatin1String("lua")    ? Lang::Lua
             : language == QLatin1String("glsl") ? Lang::Glsl
             : language == QLatin1String("json") ? Lang::Json
                                                 : Lang::Plain;
    rehighlight();
    emit languageChanged();
}

void ProceduralHighlighter::highlightBlock(const QString &text)
{
    setCurrentBlockState(0);
    if (m_lang == Lang::Plain)
        return;
    const bool lua = m_lang == Lang::Lua;
    const int n = text.size();
    const auto paint = [&](int start, int length, const QColor &color) {
        if (length <= 0 || !color.isValid())
            return;
        QTextCharFormat format;
        format.setForeground(color);
        setFormat(start, length, format);
    };

    int pos = 0;
    const int previous = previousBlockState();
    if (previous > 0) {
        const int end = findLongCommentEnd(text, 0, previous - 1, lua);
        if (end < 0) {
            paint(0, n, m_comment);
            setCurrentBlockState(previous);
            return;
        }
        const int length = lua ? previous + 1 : 2;
        paint(0, end + length, m_comment);
        pos = end + length;
    }

    static const QRegularExpression luaLongOpen(QStringLiteral("^--\\[(=*)\\["));
    while (pos < n) {
        const QChar c = text.at(pos);
        if (m_lang == Lang::Json) {
            if (c == QLatin1Char('"')) {
                int end = pos + 1;
                while (end < n && text.at(end) != QLatin1Char('"'))
                    end += text.at(end) == QLatin1Char('\\') ? 2 : 1;
                end = qMin(end + 1, n);
                paint(pos, end - pos, m_string);
                pos = end;
                continue;
            }
        } else if (lua && c == QLatin1Char('-') && text.mid(pos, 2) == QLatin1String("--")) {
            const QRegularExpressionMatch open = luaLongOpen.match(text.mid(pos, 40));
            if (open.hasMatch()) {
                const int level = open.captured(1).size();
                const int bodyStart = pos + open.capturedLength();
                const int end = findLongCommentEnd(text, bodyStart, level, true);
                if (end < 0) {
                    paint(pos, n - pos, m_comment);
                    setCurrentBlockState(1 + level);
                    return;
                }
                paint(pos, end + level + 2 - pos, m_comment);
                pos = end + level + 2;
                continue;
            }
            paint(pos, n - pos, m_comment);
            return;
        } else if (!lua && c == QLatin1Char('/') && pos + 1 < n && text.at(pos + 1) == QLatin1Char('/')) {
            paint(pos, n - pos, m_comment);
            return;
        } else if (!lua && c == QLatin1Char('/') && pos + 1 < n && text.at(pos + 1) == QLatin1Char('*')) {
            const int end = findLongCommentEnd(text, pos + 2, 0, false);
            if (end < 0) {
                paint(pos, n - pos, m_comment);
                setCurrentBlockState(1);
                return;
            }
            paint(pos, end + 2 - pos, m_comment);
            pos = end + 2;
            continue;
        } else if (!lua && c == QLatin1Char('#') && text.left(pos).trimmed().isEmpty()) {
            paint(pos, n - pos, m_keyword);
            return;
        } else if (lua && (c == QLatin1Char('"') || c == QLatin1Char('\''))) {
            int end = pos + 1;
            while (end < n && text.at(end) != c)
                end += text.at(end) == QLatin1Char('\\') ? 2 : 1;
            end = qMin(end + 1, n);
            paint(pos, end - pos, m_string);
            pos = end;
            continue;
        }

        if (c.isDigit() || (c == QLatin1Char('.') && pos + 1 < n && text.at(pos + 1).isDigit())) {
            int end = pos + 1;
            while (end < n && (text.at(end).isLetterOrNumber() || text.at(end) == QLatin1Char('.')
                               || ((text.at(end) == QLatin1Char('-') || text.at(end) == QLatin1Char('+'))
                                   && (text.at(end - 1) == QLatin1Char('e') || text.at(end - 1) == QLatin1Char('E')))))
                ++end;
            paint(pos, end - pos, m_number);
            pos = end;
            continue;
        }
        if (isIdentStart(c)) {
            int end = pos + 1;
            while (end < n && isIdentPart(text.at(end)))
                ++end;
            const QString word = text.mid(pos, end - pos);
            if (m_lang == Lang::Json) {
                if (word == QLatin1String("true") || word == QLatin1String("false") || word == QLatin1String("null"))
                    paint(pos, end - pos, m_keyword);
            } else if (lua) {
                if (luaKeywords().contains(word))
                    paint(pos, end - pos, m_keyword);
                else if (luaBuiltins().contains(word))
                    paint(pos, end - pos, m_builtin);
            } else if (glslKeywords().contains(word)) {
                paint(pos, end - pos, m_keyword);
            } else if (glslTypes().contains(word)) {
                paint(pos, end - pos, m_type);
            } else if (glslBuiltins().contains(word)) {
                paint(pos, end - pos, m_builtin);
            }
            pos = end;
            continue;
        }
        ++pos;
    }
}
