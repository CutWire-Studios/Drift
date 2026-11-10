#pragma once

#include <QObject>

class AppController;
class QTimer;

// One real compositor render of the selected clip per blend mode, over whatever lies beneath it.
// Published as image://color/blend/<modeId>?s=<serial>. Requests are debounced, and a request
// that arrives mid-render is run once the render finishes.
class BlendPreviewController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(int serial READ serial NOTIFY serialChanged)

public:
    explicit BlendPreviewController(AppController *app, QObject *parent = nullptr);

    bool active() const { return m_active; }
    void setActive(bool active);
    int serial() const { return m_serial; }

    Q_INVOKABLE void request();

signals:
    void activeChanged();
    void serialChanged();

private:
    void fire();
    void startRender();

    AppController *m_app = nullptr;
    QTimer *m_debounce = nullptr;
    bool m_active = false;
    bool m_rendering = false;
    bool m_queued = false;
    int m_serial = 0;
};
