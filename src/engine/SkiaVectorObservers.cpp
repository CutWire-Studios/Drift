#include "SkiaVectorObservers.h"

#include <QSet>

namespace drift::skia {

namespace {

class CollectingLogger final : public skottie::Logger
{
public:
    void log(Level level, const char message[], const char *json) override
    {
        QString line = QString::fromUtf8(message).trimmed();
        if (level == Level::kError)
            line.prepend(QStringLiteral("error: "));
        if (json && *json) {
            QString detail = QString::fromUtf8(json).simplified();
            if (detail.size() > 80)
                detail = detail.left(77) + QStringLiteral("...");
            line += QStringLiteral(" [") + detail + QLatin1Char(']');
        }
        if (!m_seen.contains(line) && m_lines.size() < 64) {
            m_seen.insert(line);
            m_lines.append(line);
        }
    }
    QStringList lines() const { return m_lines; }

private:
    QSet<QString> m_seen;
    QStringList m_lines;
};

class NamedPropertyCollector final : public skottie::PropertyObserver
{
public:
    void onColorProperty(const char node[], const LazyHandle<skottie::ColorPropertyHandle> &) override
    {
        add(node, QStringLiteral("color"));
    }
    void onOpacityProperty(const char node[], const LazyHandle<skottie::OpacityPropertyHandle> &) override
    {
        add(node, QStringLiteral("opacity"));
    }
    void onTextProperty(const char node[], const LazyHandle<skottie::TextPropertyHandle> &) override
    {
        add(node, QStringLiteral("text"));
    }
    void onTransformProperty(const char node[], const LazyHandle<skottie::TransformPropertyHandle> &) override
    {
        add(node, QStringLiteral("transform"));
    }
    QList<vec::VectorNamedProperty> properties() const { return m_props; }

private:
    void add(const char node[], const QString &type)
    {
        if (!node || !*node)
            return;
        const QString name = QString::fromUtf8(node);
        const QString key = name + QLatin1Char('|') + type;
        if (m_seen.contains(key))
            return;
        m_seen.insert(key);
        m_props.append({name, type});
    }
    QSet<QString> m_seen;
    QList<vec::VectorNamedProperty> m_props;
};

class MarkerCollector final : public skottie::MarkerObserver
{
public:
    void onMarker(const char name[], float t0, float t1) override
    {
        m_markers.append({QString::fromUtf8(name), t0, t1});
    }
    QList<vec::VectorMarker> markers() const { return m_markers; }

private:
    QList<vec::VectorMarker> m_markers;
};

} // namespace

struct InspectObservers::Impl
{
    sk_sp<CollectingLogger> logger = sk_make_sp<CollectingLogger>();
    sk_sp<NamedPropertyCollector> properties = sk_make_sp<NamedPropertyCollector>();
    sk_sp<MarkerCollector> markers = sk_make_sp<MarkerCollector>();
};

InspectObservers::InspectObservers() : d(new Impl) {}

InspectObservers::~InspectObservers()
{
    delete d;
}

void InspectObservers::attach(skottie::Animation::Builder &builder) const
{
    builder.setLogger(d->logger).setPropertyObserver(d->properties).setMarkerObserver(d->markers);
}

QStringList InspectObservers::loggedLines() const
{
    return d->logger->lines();
}

QList<vec::VectorNamedProperty> InspectObservers::namedProperties() const
{
    return d->properties->properties();
}

QList<vec::VectorMarker> InspectObservers::markers() const
{
    return d->markers->markers();
}

} // namespace drift::skia
