#include "ui/manip/DragSession.h"

namespace drift::manip {

DragSession::DragSession(QObject *parent)
    : QObject(parent)
{
    m_nudgeTimer.setSingleShot(true);
    m_nudgeTimer.setInterval(kNudgeWindowMs);
    connect(&m_nudgeTimer, &QTimer::timeout, this, &DragSession::commit);
}

void DragSession::setHudText(const QString &text)
{
    if (m_hud == text)
        return;
    m_hud = text;
    emit hudTextChanged();
}

void DragSession::open(const QString &kind, Device device, const QString &undoText)
{
    m_kind = kind;
    const bool deviceChanged = m_device != device;
    m_device = device;
    m_interacting = true;
    if (m_hooks.begin)
        m_hooks.begin(undoText);
    emit interactingChanged();
    if (deviceChanged)
        emit this->deviceChanged();
}

void DragSession::close()
{
    m_nudgeTimer.stop();
    m_nudging = false;
    m_interacting = false;
    m_kind.clear();
    setHudText({});
    emit interactingChanged();
}

void DragSession::begin(const QString &kind, Device device, const QString &undoText)
{
    if (m_interacting)
        commit();
    open(kind, device, undoText);
}

void DragSession::update(const QString &hudText)
{
    if (m_interacting)
        setHudText(hudText);
}

void DragSession::commit()
{
    if (!m_interacting)
        return;
    if (m_hooks.commit)
        m_hooks.commit();
    close();
    emit committed();
}

void DragSession::cancel()
{
    if (!m_interacting)
        return;
    if (m_hooks.cancel)
        m_hooks.cancel();
    close();
    emit cancelled();
}

void DragSession::nudge(const QString &undoText)
{
    if (m_interacting && !m_nudging)
        commit();
    if (!m_interacting) {
        open(QStringLiteral("nudge"), Device::Mouse, undoText);
        m_nudging = true;
    }
    m_nudgeTimer.start();
}

} // namespace drift::manip
