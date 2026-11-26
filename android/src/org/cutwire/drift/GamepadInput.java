package org.cutwire.drift;

import android.content.Context;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.hardware.input.InputManager;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.os.VibratorManager;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;

import java.util.ArrayList;
import java.util.List;

// Game controllers for the editor. DriftActivity hands every joystick/gamepad key and motion event
// to handleKey/handleMotion; the sticks and buttons go to Gamepad.cpp by name, already in the
// shape the SDL backend produces on desktop (sticks y-down, triggers 0..1). Gyro and rumble come
// from the controller's own sensor and vibrator managers, which Android only exposes from 12 (API 31).
public final class GamepadInput {
    private GamepadInput() {}

    private static boolean sStarted = false;
    private static boolean sGyro = false;
    private static InputManager.InputDeviceListener sListener;
    private static SensorEventListener sSensorListener;
    private static SensorManager sSensors;

    public static boolean isGamepad(InputDevice device) {
        if (device == null)
            return false;
        int sources = device.getSources();
        return (sources & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
                || (sources & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
    }

    private static String buttonName(int keyCode) {
        switch (keyCode) {
            case KeyEvent.KEYCODE_BUTTON_A: return "A";
            case KeyEvent.KEYCODE_BUTTON_B: return "B";
            case KeyEvent.KEYCODE_BUTTON_X: return "X";
            case KeyEvent.KEYCODE_BUTTON_Y: return "Y";
            case KeyEvent.KEYCODE_BUTTON_L1: return "LB";
            case KeyEvent.KEYCODE_BUTTON_R1: return "RB";
            case KeyEvent.KEYCODE_BUTTON_SELECT: return "Back";
            case KeyEvent.KEYCODE_BUTTON_START: return "Start";
            case KeyEvent.KEYCODE_BUTTON_MODE: return "Guide";
            case KeyEvent.KEYCODE_BUTTON_THUMBL: return "LS";
            case KeyEvent.KEYCODE_BUTTON_THUMBR: return "RS";
            case KeyEvent.KEYCODE_DPAD_UP: return "DpadUp";
            case KeyEvent.KEYCODE_DPAD_DOWN: return "DpadDown";
            case KeyEvent.KEYCODE_DPAD_LEFT: return "DpadLeft";
            case KeyEvent.KEYCODE_DPAD_RIGHT: return "DpadRight";
            default: return null;
        }
    }

    // True when the event was a controller button and has been consumed. L2/R2 keys are left to the
    // axes, which carry the analog value.
    public static boolean handleKey(KeyEvent event) {
        if (!sStarted || !isGamepad(event.getDevice()) || event.getRepeatCount() > 0)
            return false;
        int code = event.getKeyCode();
        if (code == KeyEvent.KEYCODE_BUTTON_L2 || code == KeyEvent.KEYCODE_BUTTON_R2)
            return true;
        String name = buttonName(code);
        if (name == null)
            return false;
        nativeButton(name, event.getAction() == KeyEvent.ACTION_DOWN);
        return true;
    }

    private static float axis(MotionEvent event, int axis) {
        return event.getAxisValue(axis);
    }

    public static boolean handleMotion(MotionEvent event) {
        if (!sStarted || (event.getSource() & InputDevice.SOURCE_JOYSTICK) != InputDevice.SOURCE_JOYSTICK
                || event.getActionMasked() != MotionEvent.ACTION_MOVE)
            return false;
        float lt = Math.max(axis(event, MotionEvent.AXIS_LTRIGGER), axis(event, MotionEvent.AXIS_BRAKE));
        float rt = Math.max(axis(event, MotionEvent.AXIS_RTRIGGER), axis(event, MotionEvent.AXIS_GAS));
        nativeAxes(axis(event, MotionEvent.AXIS_X), axis(event, MotionEvent.AXIS_Y),
                axis(event, MotionEvent.AXIS_Z), axis(event, MotionEvent.AXIS_RZ), lt, rt);
        float hatX = axis(event, MotionEvent.AXIS_HAT_X);
        float hatY = axis(event, MotionEvent.AXIS_HAT_Y);
        nativeButton("DpadLeft", hatX < -0.5f);
        nativeButton("DpadRight", hatX > 0.5f);
        nativeButton("DpadUp", hatY < -0.5f);
        nativeButton("DpadDown", hatY > 0.5f);
        return true;
    }

    private static List<InputDevice> pads() {
        List<InputDevice> out = new ArrayList<>();
        for (int id : InputDevice.getDeviceIds()) {
            InputDevice device = InputDevice.getDevice(id);
            if (device != null && !device.isVirtual() && isGamepad(device))
                out.add(device);
        }
        return out;
    }

    private static void publish() {
        List<InputDevice> devices = pads();
        String[] rows = new String[devices.size()];
        for (int i = 0; i < rows.length; ++i) {
            InputDevice device = devices.get(i);
            boolean gyro = false;
            boolean rumble = false;
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                gyro = device.getSensorManager().getDefaultSensor(Sensor.TYPE_GYROSCOPE) != null;
                rumble = device.getVibratorManager().getVibratorIds().length > 0;
            }
            rows[i] = device.getName() + "\t" + (gyro ? "1" : "0") + "\t" + (rumble ? "1" : "0");
        }
        nativeDevices(rows);
        if (sGyro)
            attachGyro(devices);
    }

    public static void start(final Context context) {
        if (sStarted)
            return;
        InputManager manager = (InputManager) context.getSystemService(Context.INPUT_SERVICE);
        if (manager == null)
            return;
        sStarted = true;
        sListener = new InputManager.InputDeviceListener() {
            @Override public void onInputDeviceAdded(int id) { publish(); }
            @Override public void onInputDeviceRemoved(int id) { publish(); }
            @Override public void onInputDeviceChanged(int id) { publish(); }
        };
        manager.registerInputDeviceListener(sListener, new Handler(Looper.getMainLooper()));
        publish();
    }

    public static void stop(Context context) {
        if (!sStarted)
            return;
        sStarted = false;
        InputManager manager = (InputManager) context.getSystemService(Context.INPUT_SERVICE);
        if (manager != null)
            manager.unregisterInputDeviceListener(sListener);
        detachGyro();
    }

    public static void setGyroEnabled(Context context, boolean enabled) {
        sGyro = enabled;
        if (enabled)
            attachGyro(pads());
        else
            detachGyro();
    }

    private static void detachGyro() {
        if (sSensors != null && sSensorListener != null)
            sSensors.unregisterListener(sSensorListener);
        sSensors = null;
        sSensorListener = null;
    }

    private static void attachGyro(List<InputDevice> devices) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.S || sSensorListener != null)
            return;
        for (InputDevice device : devices) {
            SensorManager sensors = device.getSensorManager();
            Sensor gyro = sensors.getDefaultSensor(Sensor.TYPE_GYROSCOPE);
            if (gyro == null)
                continue;
            sSensorListener = new SensorEventListener() {
                @Override public void onSensorChanged(SensorEvent e) { nativeGyro(e.values[0], e.values[1], e.values[2]); }
                @Override public void onAccuracyChanged(Sensor s, int accuracy) {}
            };
            sSensors = sensors;
            sensors.registerListener(sSensorListener, gyro, SensorManager.SENSOR_DELAY_GAME);
            return;
        }
    }

    // One vibrator per controller at most; the two motors are folded into one amplitude.
    public static void rumble(Context context, float low, float high, int ms) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.S)
            return;
        float strength = Math.max(low, high);
        for (InputDevice device : pads()) {
            VibratorManager manager = device.getVibratorManager();
            if (manager.getVibratorIds().length == 0)
                continue;
            Vibrator vibrator = manager.getDefaultVibrator();
            vibrator.vibrate(VibrationEffect.createOneShot(ms, Math.max(1, Math.round(strength * 255))));
            return;
        }
    }

    private static native void nativeAxes(float lx, float ly, float rx, float ry, float lt, float rt);
    private static native void nativeButton(String name, boolean down);
    private static native void nativeGyro(float x, float y, float z);
    private static native void nativeDevices(String[] rows);
}
