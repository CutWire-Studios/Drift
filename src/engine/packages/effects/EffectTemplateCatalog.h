#pragma once

#include <QList>
#include <QMap>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVariant>

struct EffectTemplatePulse
{
    QString param;
    double rest = 0.0;
    double peak = 1.0;
    int decayMs = 100;
    bool valid = false;
};

struct EffectTemplateSpeedPulse
{
    double rest = 1.0;
    double peak = 0.4;
    int decayMs = 600;
    bool valid = false;
};

struct EffectTemplateLayer
{
    QString effectId;
    QMap<QString, QVariant> params;
    EffectTemplatePulse pulse;
};

struct EffectTemplateCloneSpec
{
    int count = 0;
    QList<double> opacities;
    QList<double> scales;
};

struct EffectTemplateTrack
{
    QString role; // "background" | "foreground" | "clone"
    QList<EffectTemplateLayer> layers;
    double opacity = 1.0;
    EffectTemplateSpeedPulse speedPulse;
};

struct EffectTemplateEntry
{
    QString id;
    QString displayName;
    QString category;
    int order = 0;
    QString sync; // "onset" | "beat" | "bar" | "clip"
    bool requiresSegmentation = false;
    QList<EffectTemplateLayer> layers; // single-clip templates (v1)
    QList<EffectTemplateTrack> tracks; // multi-track templates (v2)
    EffectTemplateCloneSpec clones;
    EffectTemplateSpeedPulse speedPulse; // single-clip templates
    QString packageDir;
    QString thumbnailPath;

    bool usesMultiTrack() const { return !tracks.isEmpty(); }
};

const QList<EffectTemplateEntry> &effectTemplateCatalog();
const EffectTemplateEntry *effectTemplateForId(const QString &id);

QList<QPair<QString, QString>> effectTemplateCategories();
QString effectTemplateCategoryLabel(const QString &categoryId);

void reloadEffectTemplateCatalog(const QStringList &packageRoots = {});

QStringList defaultEffectTemplateSearchPaths();

// Installs a template.json into the user templates folder and reloads the catalog. Returns an
// error message, empty on success; idOut receives the template id.
QString installUserTemplate(const QString &templateJsonPath, QString *idOut = nullptr);
