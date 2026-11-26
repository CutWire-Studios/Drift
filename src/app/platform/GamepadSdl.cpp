#include "app/platform/Gamepad.h"

#ifdef DRIFT_HAVE_SDL

#include <SDL3/SDL.h>

#include <QHash>
#include <QVariantMap>

#include <cmath>

namespace drift {

namespace {

const char *buttonName(SDL_GamepadButton button)
{
    switch (button) {
    case SDL_GAMEPAD_BUTTON_SOUTH: return "A";
    case SDL_GAMEPAD_BUTTON_EAST: return "B";
    case SDL_GAMEPAD_BUTTON_WEST: return "X";
    case SDL_GAMEPAD_BUTTON_NORTH: return "Y";
    case SDL_GAMEPAD_BUTTON_BACK: return "Back";
    case SDL_GAMEPAD_BUTTON_GUIDE: return "Guide";
    case SDL_GAMEPAD_BUTTON_START: return "Start";
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: return "LS";
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return "RS";
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return "LB";
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return "RB";
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return "DpadUp";
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return "DpadDown";
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return "DpadLeft";
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return "DpadRight";
    default: return nullptr;
    }
}

double axisValue(SDL_Gamepad *pad, SDL_GamepadAxis axis)
{
    return std::clamp(SDL_GetGamepadAxis(pad, axis) / 32767.0, -1.0, 1.0);
}

class SdlGamepad : public GamepadBackend
{
public:
    explicit SdlGamepad(Gamepad &service)
        : m_service(service)
    {
    }

    ~SdlGamepad() override
    {
        for (SDL_Gamepad *pad : std::as_const(m_pads))
            SDL_CloseGamepad(pad);
        if (m_initialised)
            SDL_QuitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_SENSOR);
    }

    bool init()
    {
        // Gamepad and sensors only: no SDL video, window or audio is ever touched.
        m_initialised = SDL_InitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_SENSOR);
        if (!m_initialised)
            return false;
        int count = 0;
        SDL_JoystickID *ids = SDL_GetGamepads(&count);
        for (int i = 0; i < count; ++i)
            open(ids[i]);
        SDL_free(ids);
        publishDevices();
        return true;
    }

    void poll() override
    {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_GAMEPAD_ADDED:
                open(event.gdevice.which);
                publishDevices();
                break;
            case SDL_EVENT_GAMEPAD_REMOVED:
                close(event.gdevice.which);
                publishDevices();
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            case SDL_EVENT_GAMEPAD_BUTTON_UP:
                if (const char *name = buttonName(SDL_GamepadButton(event.gbutton.button))) {
                    m_active = event.gbutton.which;
                    if (!event.gbutton.down || m_service.acceptsInput())
                        m_service.ingestButton(QLatin1String(name), event.gbutton.down);
                }
                break;
            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
                if (std::abs(event.gaxis.value) > 8000)
                    m_active = event.gaxis.which;
                break;
            case SDL_EVENT_GAMEPAD_SENSOR_UPDATE:
                if (event.gsensor.sensor == SDL_SENSOR_GYRO && event.gsensor.which == m_active)
                    m_service.ingestGyro(event.gsensor.data[0], event.gsensor.data[1], event.gsensor.data[2]);
                break;
            default:
                break;
            }
        }
        SDL_Gamepad *pad = m_pads.value(m_active, nullptr);
        if (!pad && !m_pads.isEmpty())
            pad = m_pads.cbegin().value();
        if (!pad)
            return;
        if (!m_service.acceptsInput()) {
            m_service.ingestAxes(0, 0, 0, 0, 0, 0);
            return;
        }
        m_service.ingestAxes(axisValue(pad, SDL_GAMEPAD_AXIS_LEFTX), axisValue(pad, SDL_GAMEPAD_AXIS_LEFTY),
                             axisValue(pad, SDL_GAMEPAD_AXIS_RIGHTX), axisValue(pad, SDL_GAMEPAD_AXIS_RIGHTY),
                             std::max(0.0, axisValue(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER)),
                             std::max(0.0, axisValue(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)));
    }

    void rumble(double low, double high, int ms) override
    {
        if (SDL_Gamepad *pad = m_pads.value(m_active, nullptr))
            SDL_RumbleGamepad(pad, Uint16(low * 65535), Uint16(high * 65535), Uint32(ms));
    }

    void setGyroEnabled(bool enabled) override
    {
        m_gyro = enabled;
        for (SDL_Gamepad *pad : std::as_const(m_pads)) {
            if (SDL_GamepadHasSensor(pad, SDL_SENSOR_GYRO))
                SDL_SetGamepadSensorEnabled(pad, SDL_SENSOR_GYRO, enabled);
        }
    }

private:
    void open(SDL_JoystickID id)
    {
        if (m_pads.contains(id))
            return;
        SDL_Gamepad *pad = SDL_OpenGamepad(id);
        if (!pad)
            return;
        m_pads.insert(id, pad);
        if (m_pads.size() == 1)
            m_active = id;
        if (m_gyro && SDL_GamepadHasSensor(pad, SDL_SENSOR_GYRO))
            SDL_SetGamepadSensorEnabled(pad, SDL_SENSOR_GYRO, true);
    }

    void close(SDL_JoystickID id)
    {
        if (SDL_Gamepad *pad = m_pads.take(id))
            SDL_CloseGamepad(pad);
        if (m_active == id)
            m_active = m_pads.isEmpty() ? 0 : m_pads.cbegin().key();
    }

    void publishDevices()
    {
        QVariantList devices;
        for (auto it = m_pads.cbegin(); it != m_pads.cend(); ++it) {
            SDL_Gamepad *pad = it.value();
            const SDL_PropertiesID props = SDL_GetGamepadProperties(pad);
            devices.append(QVariantMap{
                {QStringLiteral("id"), int(it.key())},
                {QStringLiteral("name"), QString::fromUtf8(SDL_GetGamepadName(pad))},
                {QStringLiteral("gyro"), SDL_GamepadHasSensor(pad, SDL_SENSOR_GYRO)},
                {QStringLiteral("rumble"), SDL_GetBooleanProperty(props, SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false)},
            });
        }
        m_service.ingestDevices(devices);
    }

    Gamepad &m_service;
    QHash<SDL_JoystickID, SDL_Gamepad *> m_pads;
    SDL_JoystickID m_active = 0;
    bool m_initialised = false;
    bool m_gyro = false;
};

} // namespace

std::unique_ptr<GamepadBackend> makeSdlGamepadBackend(Gamepad &service)
{
    auto backend = std::make_unique<SdlGamepad>(service);
    if (!backend->init())
        return nullptr;
    return backend;
}

} // namespace drift

#endif
