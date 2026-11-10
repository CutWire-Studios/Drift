#include "app/procedural/ProceduralEditorController.h"

#include "app/editor/AppController.h"
#include "core/project/Project.h"
#include "engine/packages/procedurals/ProceduralCatalog.h"
#include "engine/procedural/ProceduralDiagnostics.h"
#include "playback/PlaybackEngine.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QMetaObject>
#include <QRegularExpression>

namespace {

constexpr auto kManifest = "procedural.json";
constexpr int kMaxConsoleLines = 500;

const char kMainLua[] =
    "local mesh = geo.sphere(1, 32, 16)\n"
    "mesh:transform(mat.compose({canvas.w * 0.5, canvas.h * 0.5, 0}, {0, time * params.speed, 0}, params.radius))\n"
    "scene:add(mesh, \"surface\", {})\n";

const char kVert[] =
    "#version 330 core\n"
    "in vec3 a_position;\n"
    "in vec3 a_normal;\n"
    "\n"
    "uniform mat4 u_model;\n"
    "uniform mat4 u_view;\n"
    "uniform mat4 u_proj;\n"
    "uniform mat3 u_normalMatrix;\n"
    "\n"
    "out vec3 v_normal;\n"
    "\n"
    "void main()\n"
    "{\n"
    "    v_normal = normalize(u_normalMatrix * a_normal);\n"
    "    gl_Position = u_proj * u_view * u_model * vec4(a_position, 1.0);\n"
    "}\n";

const char kFrag[] =
    "#version 330 core\n"
    "in vec3 v_normal;\n"
    "out vec4 fragColor;\n"
    "\n"
    "uniform vec3 tint;\n"
    "\n"
    "void main()\n"
    "{\n"
    "    vec3 n = normalize(v_normal);\n"
    "    float lit = 0.25 + 0.75 * max(dot(n, normalize(vec3(-0.4, -0.6, -0.7))), 0.0);\n"
    "    fragColor = vec4(tint * lit, 1.0);\n"
    "}\n";

QString manifestText(const QJsonObject &manifest)
{
    return QString::fromUtf8(QJsonDocument(manifest).toJson(QJsonDocument::Indented));
}

} // namespace

using drift::ProceduralAuthoringService;

ProceduralEditorController::ProceduralEditorController(AppController *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
}

bool ProceduralEditorController::dirty() const
{
    return m_drafts != m_original;
}

QStringList ProceduralEditorController::fileNames() const
{
    if (m_drafts.isEmpty())
        return {};
    QStringList names{QString::fromLatin1(kManifest), QStringLiteral("main.lua")};
    for (auto it = m_drafts.cbegin(); it != m_drafts.cend(); ++it) {
        if (!names.contains(it.key()))
            names.append(it.key());
    }
    return names;
}

QSize ProceduralEditorController::canvas() const
{
    const drift::Project *project = m_app ? m_app->project() : nullptr;
    return project ? QSize(project->width(), project->height()) : QSize(1920, 1080);
}

void ProceduralEditorController::log(const QString &level, const QString &text)
{
    m_console.append(QVariantMap{{QStringLiteral("level"), level}, {QStringLiteral("text"), text}});
    while (m_console.size() > kMaxConsoleLines)
        m_console.removeFirst();
}

void ProceduralEditorController::clearConsole()
{
    m_console.clear();
    emit consoleChanged();
}

bool ProceduralEditorController::loadSource(const QString &id)
{
    const ProceduralAuthoringService::Source source = ProceduralAuthoringService::getSource(id);
    if (!source.ok) {
        log(QStringLiteral("error"), source.message);
        emit consoleChanged();
        return false;
    }
    m_id = id;
    m_user = source.user;
    m_name = source.manifest.value(QStringLiteral("displayName")).toString();
    if (m_name.isEmpty())
        m_name = id;
    m_original = source.files;
    m_original.insert(QString::fromLatin1(kManifest), manifestText(source.manifest));
    m_drafts = m_original;
    emit packageChanged();
    emit dirtyChanged();
    return true;
}

bool ProceduralEditorController::open(const QString &id)
{
    return loadSource(id);
}

void ProceduralEditorController::close()
{
    m_id.clear();
    m_name.clear();
    m_user = false;
    m_original.clear();
    m_drafts.clear();
    emit packageChanged();
    emit dirtyChanged();
}

QString ProceduralEditorController::text(const QString &file) const
{
    return m_drafts.value(file);
}

void ProceduralEditorController::setText(const QString &file, const QString &text)
{
    if (readOnly() || !m_drafts.contains(file) || m_drafts.value(file) == text)
        return;
    const bool wasDirty = dirty();
    m_drafts[file] = text;
    if (wasDirty != dirty())
        emit dirtyChanged();
}

QString ProceduralEditorController::languageFor(const QString &file) const
{
    const QString suffix = QFileInfo(file).suffix().toLower();
    if (suffix == QLatin1String("lua"))
        return QStringLiteral("lua");
    if (suffix == QLatin1String("json"))
        return QStringLiteral("json");
    if (suffix == QLatin1String("vert") || suffix == QLatin1String("frag"))
        return QStringLiteral("glsl");
    return {};
}

void ProceduralEditorController::revert()
{
    if (!m_id.isEmpty())
        loadSource(m_id);
}

std::optional<QJsonObject> ProceduralEditorController::draftManifest(bool *ok)
{
    *ok = true;
    const QString draft = m_drafts.value(QString::fromLatin1(kManifest));
    if (draft == m_original.value(QString::fromLatin1(kManifest)))
        return std::nullopt;
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(draft.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        log(QStringLiteral("error"),
            tr("procedural.json is not valid JSON: %1")
                .arg(error.error != QJsonParseError::NoError ? error.errorString() : tr("expected an object")));
        emit consoleChanged();
        *ok = false;
        return std::nullopt;
    }
    return doc.object();
}

void ProceduralEditorController::report(const QString &title, const ProceduralAuthoringService::Result &r)
{
    if (r.ok) {
        const QJsonObject &stats = r.stats;
        log(QStringLiteral("info"),
            tr("%1: ok - %2 meshes, %3 vertices, %4 instances (%5 ms)")
                .arg(title)
                .arg(stats.value(QStringLiteral("meshCount")).toInt())
                .arg(stats.value(QStringLiteral("vertexCount")).toInt())
                .arg(stats.value(QStringLiteral("instanceCount")).toInt())
                .arg(qRound(stats.value(QStringLiteral("elapsedMs")).toDouble())));
    } else {
        log(QStringLiteral("error"), tr("%1 failed: %2").arg(title, r.message));
    }
    for (const QString &error : r.errors)
        log(QStringLiteral("error"), error);
    for (const QString &warning : r.warnings)
        log(QStringLiteral("warning"), warning);
    if (!r.warning.isEmpty())
        log(QStringLiteral("warning"), r.warning);
    for (const QString &line : r.luaLog)
        log(QStringLiteral("log"), line);
    for (const ProceduralAuthoringService::ShaderReport &shader : r.shaders) {
        if (!shader.log.trimmed().isEmpty())
            log(QStringLiteral("shader"), QStringLiteral("%1: %2").arg(shader.name, shader.log.trimmed()));
        else if (shader.compiled)
            log(QStringLiteral("info"), tr("shader %1 compiled").arg(shader.name));
    }
    if (r.shaderCheck == QLatin1String("skipped"))
        log(QStringLiteral("warning"), tr("Shaders were not compiled: no OpenGL context"));
    emit consoleChanged();
}

bool ProceduralEditorController::apply()
{
    if (m_id.isEmpty())
        return false;
    if (!m_user) {
        log(QStringLiteral("error"), tr("%1 is read-only. Duplicate it to edit.").arg(m_id));
        emit consoleChanged();
        return false;
    }
    bool ok = true;
    const std::optional<QJsonObject> manifest = draftManifest(&ok);
    if (!ok)
        return false;
    QMap<QString, QString> files;
    for (auto it = m_drafts.cbegin(); it != m_drafts.cend(); ++it) {
        if (it.key() != QLatin1String(kManifest) && m_original.value(it.key()) != it.value())
            files.insert(it.key(), it.value());
    }
    const ProceduralAuthoringService::Result result =
        ProceduralAuthoringService::update(m_id, manifest, files, {}, canvas());
    report(tr("Apply"), result);
    if (!result.ok)
        return false;
    loadSource(m_id);
    afterCatalogChange();
    emit applied(m_id);
    return true;
}

bool ProceduralEditorController::check()
{
    if (m_id.isEmpty())
        return false;
    bool ok = true;
    const std::optional<QJsonObject> manifest = draftManifest(&ok);
    if (!ok)
        return false;
    QMap<QString, QString> files = m_drafts;
    files.remove(QString::fromLatin1(kManifest));
    const ProceduralAuthoringService::Result result =
        ProceduralAuthoringService::check(m_id, manifest, files, canvas());
    report(tr("Check"), result);
    return result.ok;
}

QString ProceduralEditorController::duplicate()
{
    if (m_id.isEmpty())
        return {};
    bool ok = true;
    std::optional<QJsonObject> edited = draftManifest(&ok);
    if (!ok)
        return {};
    QJsonObject manifest = edited ? *edited : ProceduralAuthoringService::getSource(m_id).manifest;
    manifest.insert(QStringLiteral("displayName"), tr("%1 copy").arg(m_name));
    QMap<QString, QString> files = m_drafts;
    files.remove(QString::fromLatin1(kManifest));

    QString base = ProceduralAuthoringService::normalizeSlug(m_id.mid(m_id.lastIndexOf(QLatin1Char('.')) + 1));
    if (base.isEmpty())
        base = QStringLiteral("procedural");
    base = base.left(40);
    for (int attempt = 1; attempt <= 50; ++attempt) {
        const QString slug = attempt == 1 ? base + QStringLiteral("_copy")
                                          : base + QStringLiteral("_copy%1").arg(attempt);
        const ProceduralAuthoringService::Result result =
            ProceduralAuthoringService::create(slug, manifest, files, canvas());
        if (result.code == QLatin1String("exists"))
            continue;
        report(tr("Duplicate"), result);
        if (!result.ok)
            return {};
        loadSource(result.id);
        afterCatalogChange();
        emit applied(result.id);
        return result.id;
    }
    return {};
}

QString ProceduralEditorController::createNew(const QString &name)
{
    const QString trimmed = name.trimmed();
    const QString slug = ProceduralAuthoringService::normalizeSlug(trimmed);
    if (slug.isEmpty()) {
        log(QStringLiteral("error"), tr("Use 1-48 letters, digits or underscores for the name"));
        emit consoleChanged();
        return {};
    }
    const QJsonObject manifest{
        {QStringLiteral("displayName"), trimmed},
        {QStringLiteral("description"), QString()},
        {QStringLiteral("category"), QStringLiteral("custom")},
        {QStringLiteral("parameters"),
         QJsonArray{QJsonObject{{QStringLiteral("identifier"), QStringLiteral("radius")},
                                {QStringLiteral("displayName"), tr("Radius")},
                                {QStringLiteral("type"), QStringLiteral("float")},
                                {QStringLiteral("minValue"), 10},
                                {QStringLiteral("maxValue"), 400},
                                {QStringLiteral("defaultValue"), 120}},
                    QJsonObject{{QStringLiteral("identifier"), QStringLiteral("speed")},
                                {QStringLiteral("displayName"), tr("Speed")},
                                {QStringLiteral("type"), QStringLiteral("float")},
                                {QStringLiteral("minValue"), 0},
                                {QStringLiteral("maxValue"), 4},
                                {QStringLiteral("defaultValue"), 1}},
                    QJsonObject{{QStringLiteral("identifier"), QStringLiteral("tint")},
                                {QStringLiteral("displayName"), tr("Tint")},
                                {QStringLiteral("type"), QStringLiteral("color")},
                                {QStringLiteral("defaultValue"), QStringLiteral("#4fa3ff")}}}}};
    const QMap<QString, QString> files{{QStringLiteral("main.lua"), QString::fromLatin1(kMainLua)},
                                       {QStringLiteral("surface.vert"), QString::fromLatin1(kVert)},
                                       {QStringLiteral("surface.frag"), QString::fromLatin1(kFrag)}};
    const ProceduralAuthoringService::Result result = ProceduralAuthoringService::create(slug, manifest, files, canvas());
    report(tr("Create"), result);
    if (!result.ok)
        return {};
    loadSource(result.id);
    afterCatalogChange();
    emit applied(result.id);
    return result.id;
}

bool ProceduralEditorController::addShaderPair(const QString &name)
{
    static const QRegularExpression kName(QStringLiteral("^[A-Za-z0-9_]{1,40}$"));
    if (m_id.isEmpty() || readOnly())
        return false;
    if (!kName.match(name).hasMatch() || m_drafts.contains(name + QStringLiteral(".vert"))) {
        log(QStringLiteral("error"), tr("Use letters, digits or underscores for a new shader name, not an existing one"));
        emit consoleChanged();
        return false;
    }
    m_drafts.insert(name + QStringLiteral(".vert"), QString::fromLatin1(kVert));
    m_drafts.insert(name + QStringLiteral(".frag"), QString::fromLatin1(kFrag));
    emit packageChanged();
    emit dirtyChanged();
    return true;
}

void ProceduralEditorController::afterCatalogChange()
{
    emit catalogChanged();
    if (!m_app)
        return;
    // The inspector derives its parameter rows from the catalog, so it needs a nudge to re-read them.
    QMetaObject::invokeMethod(m_app, "selectedClipDataChanged");
    m_app->playback()->refreshFrame();
}

QVariantMap ProceduralEditorController::live(const QString &clipId) const
{
    drift::ProceduralClipReport report;
    if (!drift::ProceduralDiagnostics::shared().clip(clipId.toStdString(), &report))
        return {{QStringLiteral("known"), false}};
    const auto toList = [](const std::vector<std::string> &lines) {
        QStringList out;
        for (const std::string &line : lines)
            out << QString::fromStdString(line);
        return out;
    };
    QVariantList shaders;
    const drift::ProceduralPackage package = proceduralForId(QString::fromStdString(report.packageId));
    if (package.valid) {
        for (const auto &[name, status] : drift::ProceduralDiagnostics::shared().shaders(package.sourceHash.toStdString())) {
            shaders.append(QVariantMap{{QStringLiteral("name"), QString::fromStdString(name)},
                                       {QStringLiteral("ok"), status.ok},
                                       {QStringLiteral("log"), QString::fromStdString(status.log)}});
        }
    }
    return {{QStringLiteral("known"), true},
            {QStringLiteral("packageId"), QString::fromStdString(report.packageId)},
            {QStringLiteral("error"), QString::fromStdString(report.error)},
            {QStringLiteral("errors"), report.scene ? toList(report.scene->errors) : QStringList()},
            {QStringLiteral("log"), report.scene ? toList(report.scene->log) : QStringList()},
            {QStringLiteral("serial"), QVariant::fromValue<qulonglong>(report.serial)},
            {QStringLiteral("shaders"), shaders}};
}
