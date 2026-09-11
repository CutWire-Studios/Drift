#pragma once

#include "BinFolder.h"
#include "MediaAsset.h"
#include "Track.h"
#include "Time.h"

#include <QColor>
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>

#include <initializer_list>

namespace drift {

struct Bookmark
{
    TimeUs timeUs = 0;
    QString label;
};

// How the canvas area behind/around clips is filled.
enum class BackgroundKind { Color, Blur };

struct Background
{
    BackgroundKind kind = BackgroundKind::Color;
    QColor color = Qt::black;    // used when kind == Color
    double blurStrength = 20.0;  // px blur radius; used when kind == Blur
};

// A track list that never shares its buffer with the list it was copied from.
//
// Project has value semantics on purpose: the undo stack and every edit path snapshot it by
// copy and then keep mutating the original — very often through a `Track &` or `Clip &` that
// was bound *before* the copy was taken. QList is copy-on-write, so a plain memberwise copy
// leaves both lists pointing at one buffer, and a write through such a reference bypasses
// QList's detach and lands in the snapshot as well as in the project. That silently broke undo
// for most clip edits: the "before" state was mutated into the "after" state, so undoing
// restored the value that had just been set.
//
// Detaching here, on the copy, is what fixes it: the copy gets its own buffers and the original
// keeps sole ownership of its, so references into the original stay valid *and* private. Two
// levels are needed and sufficient — the clip buffers are what those stale references point
// into, and everything below a Clip is reached through a detaching accessor at write time.
struct TrackList : QList<Track>
{
    using QList<Track>::QList;

    TrackList() = default;
    TrackList(const TrackList &other) : QList<Track>(other) { deepDetach(); }
    TrackList(const QList<Track> &other) : QList<Track>(other) { deepDetach(); }
    TrackList(TrackList &&) = default;

    TrackList &operator=(const TrackList &other)
    {
        QList<Track>::operator=(other);
        deepDetach();
        return *this;
    }
    TrackList &operator=(const QList<Track> &other)
    {
        QList<Track>::operator=(other);
        deepDetach();
        return *this;
    }
    TrackList &operator=(TrackList &&) = default;
    // Disambiguates `m_tracks = {...}`, which would otherwise match both the TrackList and the
    // QList overloads above. A fresh list shares nothing, so there is nothing to detach.
    TrackList &operator=(std::initializer_list<Track> items)
    {
        QList<Track>::operator=(items);
        return *this;
    }

    void deepDetach()
    {
        detach();
        for (Track &track : *this) {
            track.clips.detach();
            track.transitions.detach();
        }
    }
};

// Root project document: tracks, assets, output settings.
class Project
{
public:
    static constexpr int kCurrentVersion = 5;

    Project() { resetToDefaultTimeline(); }

    QString name() const { return m_name; }
    void setName(const QString &name) { m_name = name; }

    // Stable across saves; names the extraction directory a packaged project unpacks into, so it
    // must survive a round-trip rather than being reminted per save.
    QString id() const { return m_id; }
    void setId(const QString &id) { m_id = id; }

    QString author() const { return m_author; }
    void setAuthor(const QString &author) { m_author = author; }

    QString description() const { return m_description; }
    void setDescription(const QString &description) { m_description = description; }

    QDateTime createdAt() const { return m_createdAt; }
    void setCreatedAt(const QDateTime &createdAt) { m_createdAt = createdAt; }

    QDateTime modifiedAt() const { return m_modifiedAt; }
    void setModifiedAt(const QDateTime &modifiedAt) { m_modifiedAt = modifiedAt; }

    int fps() const { return m_fps; }
    void setFps(int fps) { m_fps = qMax(1, fps); }

    int width() const { return m_width; }
    int height() const { return m_height; }
    int sampleRate() const { return m_sampleRate; }
    void setResolution(int width, int height) { m_width = width; m_height = height; }
    void setSampleRate(int rate) { m_sampleRate = rate; }

    const QList<Track> &tracks() const { return m_tracks; }
    QList<Track> &tracks() { return m_tracks; }

    const QList<QString> &assetOrder() const { return m_assetOrder; }
    QList<QString> &assetOrder() { return m_assetOrder; }
    const QHash<QString, MediaAsset> &assets() const { return m_assetsById; }
    QHash<QString, MediaAsset> &assets() { return m_assetsById; }

    const QList<QString> &binFolderOrder() const { return m_binFolderOrder; }
    QList<QString> &binFolderOrder() { return m_binFolderOrder; }
    const QHash<QString, BinFolder> &binFolders() const { return m_binFoldersById; }
    QHash<QString, BinFolder> &binFolders() { return m_binFoldersById; }

    const QList<Bookmark> &bookmarks() const { return m_bookmarks; }
    QList<Bookmark> &bookmarks() { return m_bookmarks; }

    // Timeline work area (Mark In / Mark Out). Unset markers use -1.
    TimeUs workAreaInUs() const { return m_workAreaInUs; }
    TimeUs workAreaOutUs() const { return m_workAreaOutUs; }
    void setWorkAreaInUs(TimeUs us) { m_workAreaInUs = us; }
    void setWorkAreaOutUs(TimeUs us) { m_workAreaOutUs = us; }
    void clearWorkArea()
    {
        m_workAreaInUs = -1;
        m_workAreaOutUs = -1;
    }
    bool hasWorkArea() const { return m_workAreaInUs >= 0 && m_workAreaOutUs > m_workAreaInUs; }

    const Background &background() const { return m_background; }
    void setBackground(const Background &background) { m_background = background; }

    void resetToDefaultTimeline();
    TimeUs durationUs() const;

    // Mints an id for every track that lacks one, and drops a nested adjustment lane's
    // `parentTrackId` when the track it names is gone. Track creation is spread over a dozen
    // call sites (plus tests, which build bare `Track{.type = …}` aggregates), so minting is
    // centralised here rather than duplicated: anything that appends a track can leave the id
    // empty and this makes it valid. Idempotent — existing ids are never rewritten.
    void ensureTrackIds();

    int trackIndexById(const QString &id) const;

    // Copy that uniquely owns its Qt containers. A plain `Project copy = *this` shares
    // QMap/QList payloads via implicit sharing; mutating either side while another thread
    // reads the other is a use-after-free. Call this before handing a snapshot to a worker.
    Project detachedCopy() const;

    QString addAsset(MediaAsset asset);
    MediaAsset *asset(const QString &id);
    const MediaAsset *asset(const QString &id) const;
    int assetIndex(const QString &id) const;
    QString assetIdAt(int index) const;

    QString addBinFolder(BinFolder folder);
    BinFolder *binFolder(const QString &id);
    const BinFolder *binFolder(const QString &id) const;
    int binFolderIndex(const QString &id) const;
    QString binFolderIdAt(int index) const;

    static Project fromJson(const QJsonObject &object, QString *errorOut = nullptr);
    QJsonObject toJson() const;
    // Compact JSON of toJson(); SHA-256 hex of those bytes. Undo history and on-disk
    // history snapshots share this so a file named <hash>.json hashes back to <hash>.
    QByteArray toCompactJson() const;
    QString contentHash() const;

private:
    QString m_name = QStringLiteral("Untitled Project");
    QString m_id;
    QString m_author;
    QString m_description;
    QDateTime m_createdAt;
    QDateTime m_modifiedAt;
    int m_fps = 30;
    int m_width = 1920;
    int m_height = 1080;
    int m_sampleRate = 48000;
    TrackList m_tracks;
    QList<Bookmark> m_bookmarks;
    TimeUs m_workAreaInUs = -1;
    TimeUs m_workAreaOutUs = -1;
    Background m_background;
    QList<QString> m_assetOrder;
    QHash<QString, MediaAsset> m_assetsById;
    QList<QString> m_binFolderOrder;
    QHash<QString, BinFolder> m_binFoldersById;
};

} // namespace drift
