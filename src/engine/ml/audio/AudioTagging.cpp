#include "engine/ml/audio/AudioTagging.h"

#include <QSet>

#include <algorithm>
#include <cmath>
#include <map>

namespace drift {

namespace {

constexpr double kNearSeconds = 0.10;
constexpr double kBurstSeconds = 0.5;
constexpr double kExposedDb = 12.0;
constexpr float kSpeechVoice = 0.5f;
constexpr float kMusicSection = 0.3f;
constexpr float kExtraSource = 0.08f;
constexpr double kZOk = 2.5;
constexpr double kZWeak = 1.5;
constexpr double kNearSilenceDb = -55.0;

const QSet<QString> &astGenericLabels()
{
    static const QSet<QString> labels = {
        QStringLiteral("Animal"), QStringLiteral("Inside, large room or hall"), QStringLiteral("Inside, small room"),
        QStringLiteral("Music"), QStringLiteral("Musical instrument"), QStringLiteral("Outside, rural or natural"),
        QStringLiteral("Outside, urban or manmade"), QStringLiteral("Silence"), QStringLiteral("Sound effect"),
        QStringLiteral("Speech")};
    return labels;
}

struct Excerpt
{
    double a = 0.0;
    double b = 0.0;
};

Excerpt eventExcerpt(const AudioEvent &e, double t, double dur)
{
    double a, b;
    if (e.kind == AudioEvent::Kind::Swell) {
        a = t - 0.5;
        b = t + 0.3;
    } else {
        a = t - 0.02;
        b = t + std::clamp(e.lengthMs / 1000.0, 0.3, 1.5);
    }
    return {std::max(0.0, a), std::min(dur, b)};
}

ClapSpan clapSpan(const Excerpt &x)
{
    const double rate = ClapTagger::kSampleRate;
    return {static_cast<qint64>(std::llround(x.a * rate)),
            std::max<qint64>(static_cast<qint64>(std::llround(x.a * rate)) + 480, static_cast<qint64>(std::llround(x.b * rate)))};
}

// AST probabilities of excerpts of the 48 kHz audio.
std::vector<std::vector<float>> astOfExcerpts(AstClassifier &ast, const float *mono, qint64 frames,
                                              const std::vector<Excerpt> &excerpts)
{
    std::vector<float> joined;
    std::vector<AstSpan> spans;
    for (const Excerpt &x : excerpts) {
        const qint64 from = std::clamp<qint64>(static_cast<qint64>(std::llround(x.a * ClapTagger::kSampleRate)), 0, frames);
        const qint64 to = std::clamp<qint64>(static_cast<qint64>(std::llround(x.b * ClapTagger::kSampleRate)), from, frames);
        std::vector<float> part = AstClassifier::resample48kTo16k(mono + from, to - from);
        const qint64 begin = static_cast<qint64>(joined.size());
        joined.insert(joined.end(), part.begin(), part.end());
        spans.push_back({begin, static_cast<qint64>(joined.size())});
    }
    if (joined.empty())
        joined.push_back(0.0f);
    return ast.probabilities(joined.data(), static_cast<qint64>(joined.size()), spans);
}

QString sectionLabel(const ClapSection &s)
{
    if (s.digitalSilence)
        return QStringLiteral("digital silence");
    if (s.levelDb < kNearSilenceDb)
        return QStringLiteral("near silence");
    auto best = [&](ClapSectionCategory c) { return s.tags[c].isEmpty() ? ClapTag{} : s.tags[c].first(); };
    const ClapTag genre = best(Genre), mood = best(Mood), instrument = best(Instruments);
    const ClapTag ambience = best(Ambience), voice = best(Voice);
    const bool music = s.hasAst ? s.ast.music >= kMusicSection : genre.z >= ambience.z;
    QStringList parts;
    if (music) {
        QStringList bits{shortTagName(genre.text) + (genre.z >= kZOk ? QString() : QStringLiteral("?"))};
        if (instrument.z >= kZWeak && !instrument.text.isEmpty())
            bits << shortTagName(instrument.text);
        if (mood.z >= kZOk && !mood.text.isEmpty())
            bits << shortTagName(mood.text).replace(QStringLiteral(" music"), QString());
        parts << bits.join(QStringLiteral(", "));
    }
    QStringList extra;
    if (s.hasAst) {
        for (const AstLabel &l : s.ast.top) {
            if (!astGenericLabels().contains(l.label) && l.probability >= kExtraSource && extra.size() < 3)
                extra << l.label.toLower();
        }
    }
    if (!extra.isEmpty())
        parts << QStringLiteral("also: ") + extra.join(QStringLiteral(", ")) + QStringLiteral(" (AST)");
    else if (!ambience.text.isEmpty() && ambience.z >= (music ? kZOk : kZWeak))
        parts << QStringLiteral("amb: ") + shortTagName(ambience.text);
    if (s.hasAst ? s.ast.speech >= kMusicSection : (!voice.text.isEmpty() && voice.z >= kZOk))
        parts << QStringLiteral("voice: ") + shortTagName(voice.text);
    return parts.isEmpty() ? QStringLiteral("unclear (no confident music / ambience / voice tag)")
                           : parts.join(QStringLiteral(" | "));
}

void labelSections(AudioTags &tags, const QList<double> &novelty)
{
    for (ClapSection &s : tags.sections)
        s.label = sectionLabel(s);
    auto key = [](const ClapSection &s) {
        const QStringList parts = s.label.split(QStringLiteral(" | "));
        return std::make_pair(parts.first().split(QLatin1Char(',')).first(), parts.mid(1));
    };
    // Neighbours that read alike with no strong change between them are one section.
    for (int k = 1; k < tags.sections.size();) {
        const ClapSection a = tags.sections.at(k - 1), b = tags.sections.at(k);
        bool change = false;
        for (double t : novelty)
            change = change || std::abs(t - b.start) <= 1.0;
        if (key(a) != key(b) || change) {
            ++k;
            continue;
        }
        ClapSection keep = (a.end - a.start >= b.end - b.start) ? a : b;
        keep.start = a.start;
        keep.end = b.end;
        keep.windows = a.windows + b.windows;
        tags.sections[k - 1] = keep;
        tags.sections.removeAt(k);
    }
}

} // namespace

QString shortTagName(const QString &text)
{
    for (const QLatin1String article : {QLatin1String("a "), QLatin1String("an "), QLatin1String("the ")}) {
        if (text.startsWith(article))
            return text.mid(article.size());
    }
    return text;
}

AudioTags tagAudio(const float *mono, qint64 frames, double startSeconds, const QList<AudioEvent> &events,
                   const AudioTagOptions &options)
{
    AudioTags out;
    const double dur = double(frames) / ClapTagger::kSampleRate;
    ClapTagger &clap = ClapTagger::instance();
    AstClassifier &ast = AstClassifier::instance();
    if (ClapTagger::modelPresent()) {
        out.clap = clap.available();
        if (!out.clap)
            out.clapError = clap.lastError();
    }
    if (AstClassifier::modelPresent()) {
        out.ast = ast.available();
        if (!out.ast)
            out.astError = ast.lastError();
    }
    if (!out.clap && !out.ast)
        return out;
    // Progress is split roughly by model windows: events, then sections, then AST.
    auto step = [&](double fraction) { return !options.progress || options.progress(fraction); };

    // ---- events: the ones near a cut first, then the most prominent, spread over the span
    if (!events.isEmpty() && options.maxTagged > 0) {
        QList<int> order(events.size());
        for (int i = 0; i < events.size(); ++i)
            order[i] = i;
        auto isNear = [&](const AudioEvent &e) {
            for (double t : options.near) {
                if (std::abs(t - (e.seconds - startSeconds)) <= kNearSeconds)
                    return true;
            }
            return false;
        };
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            const AudioEvent &x = events.at(a), &y = events.at(b);
            const bool nx = isNear(x), ny = isNear(y);
            if (nx != ny)
                return nx;
            if (x.prominenceDb != y.prominenceDb)
                return x.prominenceDb > y.prominenceDb;
            return x.contrastDb > y.contrastDb;
        });
        // One tag per burst: later same-band events within 0.5 s of a kept one go last.
        QList<int> kept, burst;
        for (int i : order) {
            bool same = false;
            for (int j : kept)
                same = same || (std::abs(events.at(j).seconds - events.at(i).seconds) < kBurstSeconds
                                && events.at(j).band == events.at(i).band);
            (same ? burst : kept).append(i);
        }
        const QList<int> ranked = kept + burst;
        const int n = std::min<int>(options.maxTagged, events.size());
        const int bins = std::max(1, n / 2);
        QList<int> first, taken;
        QSet<int> seenBin;
        for (int i : ranked) {
            const int bin = std::clamp(static_cast<int>((events.at(i).seconds - startSeconds) / std::max(dur, 1e-6) * bins), 0, bins - 1);
            if (!seenBin.contains(bin)) {
                seenBin.insert(bin);
                first.append(i);
            }
        }
        for (int i : ranked) {
            if (!first.contains(i))
                first.append(i);
        }
        taken = first.mid(0, n);
        std::sort(taken.begin(), taken.end());

        std::vector<Excerpt> excerpts;
        for (int i : taken)
            excerpts.push_back(eventExcerpt(events.at(i), events.at(i).seconds - startSeconds, dur));
        QList<TaggedEvent> tagged;
        for (int i : taken) {
            TaggedEvent t;
            t.index = i;
            tagged.append(t);
        }
        if (out.clap) {
            std::vector<ClapSpan> spans;
            for (const Excerpt &x : excerpts)
                spans.push_back(clapSpan(x));
            const auto embeddings = clap.embed(mono, frames, spans, [&](double f) { return step(0.4 * f); });
            if (embeddings.empty()) {
                out.cancelled = options.progress != nullptr;
                out.clap = false;
                out.clapError = clap.lastError();
            } else {
                out.clapWindows += static_cast<int>(embeddings.size());
                for (size_t k = 0; k < embeddings.size(); ++k) {
                    TaggedEvent &t = tagged[static_cast<int>(k)];
                    t.tags = clap.eventTags(embeddings[k], 3);
                    if (t.tags.isEmpty())
                        continue;
                    t.kind = t.tags.first().kind;
                    t.label = shortTagName(t.tags.first().text);
                    t.exposed = events.at(t.index).prominenceDb >= kExposedDb;
                    t.soundsLike = clap.topTerms(embeddings[k], 2, {QStringLiteral("quality"), QStringLiteral("production")});
                }
            }
        }
        if (out.ast && options.astEvents > 0) {
            QList<int> astOrder;
            for (int i : ranked) {
                if (taken.contains(i) && astOrder.size() < options.astEvents)
                    astOrder.append(i);
            }
            std::vector<Excerpt> astExcerpts;
            for (int i : astOrder)
                astExcerpts.push_back(eventExcerpt(events.at(i), events.at(i).seconds - startSeconds, dur));
            const auto rows = astOfExcerpts(ast, mono, frames, astExcerpts);
            for (size_t k = 0; k < rows.size(); ++k) {
                TaggedEvent *t = nullptr;
                for (TaggedEvent &candidate : tagged) {
                    if (candidate.index == astOrder.at(static_cast<int>(k)))
                        t = &candidate;
                }
                if (!t)
                    continue;
                const AstSummary summary = ast.summarise(rows[k], 4, 0.05f);
                t->hasAst = true;
                t->ast = summary.top;
                t->speech = summary.speech;
                if (summary.speech >= kSpeechVoice && t->kind != QLatin1String("VOICE")) {
                    t->kind = QStringLiteral("VOICE");
                    t->label = QStringLiteral("speech (AST %1)").arg(double(summary.speech), 0, 'f', 2);
                    t->exposed = true;
                }
            }
        }
        // Without CLAP an event only has what AST said about it.
        for (const TaggedEvent &t : std::as_const(tagged)) {
            if (out.clap || t.hasAst)
                out.events.append(t);
        }
    }

    // ---- sections
    if (options.sections && out.clap && dur >= 1.0) {
        ClapSections found = clap.sections(mono, frames, options.sectionWindow, options.sectionHop,
                                           options.sectionMaxWindows, [&](double f) { return step(0.4 + 0.4 * f); });
        if (found.sections.isEmpty() && found.windows == 0) {
            if (!clap.lastError().isEmpty())
                out.clapError = clap.lastError();
        } else {
            out.clapWindows += found.windows;
            out.sections = found.sections;
            if (out.ast && options.astSections > 0 && !out.sections.isEmpty()) {
                QList<int> order(out.sections.size());
                for (int i = 0; i < order.size(); ++i)
                    order[i] = i;
                std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
                    return out.sections.at(a).end - out.sections.at(a).start > out.sections.at(b).end - out.sections.at(b).start;
                });
                std::map<int, int> plan;
                for (int k = 0; k < std::min<int>(options.astSections, order.size()); ++k)
                    plan[order.at(k)] = 1;
                for (int k : order) {
                    int total = 0;
                    for (const auto &[unused, n] : plan)
                        total += n;
                    const ClapSection &s = out.sections.at(k);
                    if (total < options.astSections && plan.count(k) && s.end - s.start > 15.0)
                        plan[k] = 2;
                }
                std::vector<Excerpt> crops;
                std::vector<int> owner;
                for (const auto &[k, n] : plan) {
                    const ClapSection &s = out.sections.at(k);
                    const double d = s.end - s.start;
                    const double w = std::min(d, 10.24);
                    for (int j = 0; j < n; ++j) {
                        const double c0 = n > 1 ? s.start + (d - w) * (j + 0.5) / n : s.start + (d - w) / 2.0;
                        crops.push_back({c0, c0 + w});
                        owner.push_back(k);
                    }
                }
                const auto rows = astOfExcerpts(ast, mono, frames, crops);
                for (const auto &[k, n] : plan) {
                    std::vector<float> mean(AstClassifier::kClasses, 0.0f);
                    int count = 0;
                    for (size_t r = 0; r < rows.size(); ++r) {
                        if (owner[r] != k)
                            continue;
                        for (size_t c = 0; c < mean.size(); ++c)
                            mean[c] += rows[r][c];
                        ++count;
                    }
                    if (count == 0)
                        continue;
                    for (float &v : mean)
                        v /= float(count);
                    ClapSection &s = out.sections[k];
                    s.ast = ast.summarise(mean, 5, 0.05f);
                    s.hasAst = true;
                }
            }
            labelSections(out, found.novelty);
        }
    }
    step(1.0);
    return out;
}

MomentIndex indexMoments(ClapTagger &clap, const float *mono, qint64 frames, double startSeconds,
                         const QList<AudioEvent> &events, const std::function<bool(double)> &progress)
{
    constexpr int kMaxWindows = 40;
    constexpr int kMaxEvents = 48;
    constexpr double kMaxWindowSeconds = 10.0;
    constexpr double kBucketSeconds = 2.0;
    const double rate = ClapTagger::kSampleRate;
    const double dur = double(frames) / rate;

    MomentIndex index;
    double win = std::clamp(dur / 6.0, 1.0, 5.0);
    while (win < kMaxWindowSeconds && (dur - win) / (win / 2.0) + 1.0 > kMaxWindows)
        win = std::min(kMaxWindowSeconds, win * 1.25);
    if (dur <= win) {
        index.entries.push_back({0.0, dur, false});
    } else {
        double hop = win / 2.0;
        if ((dur - win) / hop + 1.0 > kMaxWindows)
            hop = std::min(win, (dur - win) / (kMaxWindows - 1));
        for (double s = 0.0; s + win <= dur + 1e-6; s += hop)
            index.entries.push_back({s, s + win, false});
        if (dur - win - index.entries.back().start > 1e-3)
            index.entries.push_back({dur - win, dur, false});
    }

    // Event excerpts: round-robin over 2 s buckets, the most prominent first.
    std::map<int, std::vector<const AudioEvent *>> buckets;
    QList<const AudioEvent *> sorted;
    for (const AudioEvent &e : events)
        sorted.append(&e);
    std::stable_sort(sorted.begin(), sorted.end(), [](const AudioEvent *a, const AudioEvent *b) { return a->prominenceDb > b->prominenceDb; });
    for (const AudioEvent *e : std::as_const(sorted))
        buckets[static_cast<int>((e->seconds - startSeconds) / kBucketSeconds)].push_back(e);
    std::vector<const AudioEvent *> picked;
    for (size_t round = 0; picked.size() < kMaxEvents; ++round) {
        bool any = false;
        for (auto &[bucket, list] : buckets) {
            if (round < list.size() && picked.size() < kMaxEvents) {
                picked.push_back(list[round]);
                any = true;
            }
        }
        if (!any)
            break;
    }
    for (const AudioEvent *e : picked) {
        const double t = e->seconds - startSeconds;
        const double a = std::max(0.0, t - 0.005);
        const double b = std::min(dur, t + std::clamp(e->lengthMs / 1000.0, 0.25, 1.5));
        if (b - a > 0.02)
            index.entries.push_back({a, b, true});
    }

    std::vector<ClapSpan> spans;
    for (const MomentIndex::Entry &e : index.entries)
        spans.push_back({static_cast<qint64>(std::llround(e.start * rate)), static_cast<qint64>(std::llround(e.end * rate))});
    index.embeddings = clap.embed(mono, frames, spans, progress);
    if (index.embeddings.empty())
        index.entries.clear();
    return index;
}

QList<AudioMoment> findMoments(ClapTagger &clap, const MomentIndex &index, int term, int top)
{
    struct Scored
    {
        size_t entry;
        float z, cosine;
    };
    std::vector<Scored> all;
    for (size_t i = 0; i < index.entries.size(); ++i) {
        const auto scores = clap.termScores(index.embeddings[i]);
        if (static_cast<size_t>(term) >= scores.size())
            return {};
        all.push_back({i, scores[static_cast<size_t>(term)].z, scores[static_cast<size_t>(term)].cosine});
    }
    std::stable_sort(all.begin(), all.end(), [](const Scored &a, const Scored &b) { return a.z > b.z; });
    QList<AudioMoment> out;
    for (const Scored &s : all) {
        if (out.size() >= top)
            break;
        const MomentIndex::Entry &e = index.entries[s.entry];
        bool clear = true;
        for (const AudioMoment &m : std::as_const(out))
            clear = clear && (e.end <= m.start + 1e-3 || e.start >= m.end - 1e-3);
        if (clear)
            out.append({e.start, e.end, s.z, s.cosine, e.event});
    }
    return out;
}

} // namespace drift
