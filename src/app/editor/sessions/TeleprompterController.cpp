#include "app/editor/sessions/TeleprompterController.h"

#include "app/editor/AppController.h"
#include "app/editor/ProjectFileController.h"
#include "core/project/Project.h"

#include <QSettings>

#include <algorithm>

TeleprompterController::TeleprompterController(AppController *app, QObject *parent)
    : QObject(parent), m_app(app)
{
    QSettings settings;
    m_speed = std::clamp(settings.value(QStringLiteral("teleprompter/speed"), m_speed).toDouble(), 0.1, 5.0);
    m_fontSize = std::clamp(settings.value(QStringLiteral("teleprompter/fontSize"), m_fontSize).toInt(), 12, 96);
    m_overlayEnabled = settings.value(QStringLiteral("teleprompter/overlayEnabled"), m_overlayEnabled).toBool();
    m_followRecording =
        settings.value(QStringLiteral("teleprompter/followRecording"), m_followRecording).toBool();

    m_timer.setInterval(16);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &TeleprompterController::tick);

    connect(m_app, &AppController::audioRecordingStateChanged, this,
            [this] { onRecordingChanged(m_app->isRecordingAudio()); });
    connect(m_app, &AppController::audioRecordingPausedChanged, this, [this] {
        if (m_app->isRecordingAudio())
            onRecordingChanged(!m_app->isAudioRecordingPaused());
    });
    connect(m_app->projectFile(), &ProjectFileController::projectMetadataChanged, this,
            &TeleprompterController::onProjectMetadataChanged);
}

QString TeleprompterController::script() const
{
    return m_app->m_project.teleprompterScript();
}

void TeleprompterController::setScript(const QString &script)
{
    if (m_app->m_project.teleprompterScript() == script)
        return;
    m_app->m_project.setTeleprompterScript(script);
    m_lastScript = script;
    m_app->m_projectFile->setDirty(true);
    emit scriptChanged();
}

void TeleprompterController::setPlaying(bool playing)
{
    if (m_playing == playing)
        return;
    m_playing = playing;
    if (playing) {
        m_clock.start();
        m_timer.start();
    } else {
        m_timer.stop();
    }
    emit playingChanged();
}

void TeleprompterController::setPosition(double position)
{
    position = std::max(0.0, position);
    if (qFuzzyCompare(m_position, position))
        return;
    m_position = position;
    emit positionChanged();
}

void TeleprompterController::setSpeed(double speed)
{
    speed = std::clamp(speed, 0.1, 5.0);
    if (qFuzzyCompare(m_speed, speed))
        return;
    m_speed = speed;
    QSettings().setValue(QStringLiteral("teleprompter/speed"), m_speed);
    emit speedChanged();
}

void TeleprompterController::setFontSize(int fontSize)
{
    fontSize = std::clamp(fontSize, 12, 96);
    if (m_fontSize == fontSize)
        return;
    m_fontSize = fontSize;
    QSettings().setValue(QStringLiteral("teleprompter/fontSize"), m_fontSize);
    emit fontSizeChanged();
}

void TeleprompterController::setOverlayEnabled(bool enabled)
{
    if (m_overlayEnabled == enabled)
        return;
    m_overlayEnabled = enabled;
    QSettings().setValue(QStringLiteral("teleprompter/overlayEnabled"), m_overlayEnabled);
    emit overlayEnabledChanged();
}

void TeleprompterController::setFollowRecording(bool follow)
{
    if (m_followRecording == follow)
        return;
    m_followRecording = follow;
    QSettings().setValue(QStringLiteral("teleprompter/followRecording"), m_followRecording);
    emit followRecordingChanged();
}

void TeleprompterController::toggle()
{
    setPlaying(!m_playing);
}

void TeleprompterController::reset()
{
    setPlaying(false);
    setPosition(0.0);
}

void TeleprompterController::tick()
{
    const double seconds = m_clock.restart() / 1000.0;
    setPosition(m_position + m_speed * seconds);
}

void TeleprompterController::onRecordingChanged(bool recording)
{
    if (m_followRecording)
        setPlaying(recording);
}

void TeleprompterController::onProjectMetadataChanged()
{
    const QString current = script();
    if (current == m_lastScript)
        return;
    m_lastScript = current;
    emit scriptChanged();
    reset();
}
