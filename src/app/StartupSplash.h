#pragma once

#include <QColor>
#include <QFont>
#include <QPixmap>
#include <QSplashScreen>

class QQuickWindow;

// Shown as soon as QApplication exists, while the editor is built. Painted with QPainter rather
// than Qt Quick: it has to come up before the theme, the QML engine and the GL share context, and
// it repaints synchronously, so each step is visible even though the GUI thread is busy between
// them.
class StartupSplash : public QSplashScreen
{
    Q_OBJECT

public:
    StartupSplash();

    // Status line and overall progress, 0..1. Repaints before returning.
    void step(const QString &status, double progress);
    // Closes on the window's first presented frame.
    void finishOn(QQuickWindow *window);

protected:
    void drawContents(QPainter *painter) override;
    // QSplashScreen hides on any click; a stray click during startup should not lose it.
    void mousePressEvent(QMouseEvent *event) override;

private:
    QString m_status;
    double m_progress = 0.0;
    QPixmap m_logo;
    QFont m_titleFont;
    QFont m_bodyFont;
    QColor m_background;
    QColor m_border;
    QColor m_foreground;
    QColor m_foregroundMuted;
    QColor m_progressTrack;
    QColor m_progressFill;
};
