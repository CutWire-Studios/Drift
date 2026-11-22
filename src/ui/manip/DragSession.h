#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

#include <functional>

namespace drift::manip {

// What the session drives. In the app these are beginPreviewDrag, commitPreviewDrag and
// cancelPreviewDrag (which restores the pre-drag project); tests inject fakes.
struct DragHooks
{
    std::function<void(const QString &undoText)> begin;
    std::function<void()> commit;
    std::function<void()> cancel;
};

// One interaction = one undo step, with Esc reverting it whole.
class DragSession : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool interacting READ interacting NOTIFY interactingChanged)
    Q_PROPERTY(Device device READ device NOTIFY deviceChanged)
    Q_PROPERTY(QString kind READ kind NOTIFY interactingChanged)
    Q_PROPERTY(QString hudText READ hudText WRITE setHudText NOTIFY hudTextChanged)

public:
    enum class Device { Mouse, Touch, Pen };
    Q_ENUM(Device)

    static constexpr int kNudgeWindowMs = 600;

    explicit DragSession(QObject *parent = nullptr);

    void setHooks(DragHooks hooks) { m_hooks = std::move(hooks); }
    void setNudgeWindowMs(int ms) { m_nudgeTimer.setInterval(ms); }

    bool interacting() const { return m_interacting; }
    Device device() const { return m_device; }
    QString kind() const { return m_kind; }
    QString hudText() const { return m_hud; }
    void setHudText(const QString &text);

    // A drag or nudge burst already open is committed first.
    Q_INVOKABLE void begin(const QString &kind, Device device = Device::Mouse, const QString &undoText = {});
    Q_INVOKABLE void update(const QString &hudText);
    Q_INVOKABLE void commit();
    // Reverts through the hooks; no undo entry is made.
    Q_INVOKABLE void cancel();
    // Call before applying a keyboard nudge. The first call of a burst opens the undo group, each
    // further one within the window extends it, and it commits when the window lapses.
    Q_INVOKABLE void nudge(const QString &undoText = {});

signals:
    void interactingChanged();
    void deviceChanged();
    void hudTextChanged();
    void committed();
    void cancelled();

private:
    void open(const QString &kind, Device device, const QString &undoText);
    void close();

    DragHooks m_hooks;
    QTimer m_nudgeTimer;
    bool m_interacting = false;
    bool m_nudging = false;
    Device m_device = Device::Mouse;
    QString m_kind;
    QString m_hud;
};

} // namespace drift::manip
