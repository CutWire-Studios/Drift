package org.cutwire.drift;

import android.content.Context;
import android.hardware.input.InputManager;
import android.os.Handler;
import android.os.Looper;
import android.view.InputDevice;

// Reports whether a mouse is attached, so the touch shell can offer the desktop one. DeX and
// Chromebook touchpads report SOURCE_MOUSE too, which is what we want: they drive a pointer.
public final class PointerDevices {
    private PointerDevices() {}

    private static boolean sListening = false;

    public static boolean hasMouse(Context context) {
        for (int id : InputDevice.getDeviceIds()) {
            InputDevice device = InputDevice.getDevice(id);
            // Samsung phones carry a built-in sec_touchpad (DeX "phone as touchpad") that reports
            // SOURCE_MOUSE while disabled.
            if (device == null || device.isVirtual() || !device.isEnabled())
                continue;
            if ((device.getSources() & InputDevice.SOURCE_MOUSE) == InputDevice.SOURCE_MOUSE)
                return true;
        }
        return false;
    }

    // Idempotent. Callbacks arrive on the main looper; the native side only flips state and
    // queues a signal onto the Qt thread.
    public static void listen(final Context context) {
        if (sListening)
            return;
        InputManager manager = (InputManager) context.getSystemService(Context.INPUT_SERVICE);
        if (manager == null)
            return;
        sListening = true;
        manager.registerInputDeviceListener(new InputManager.InputDeviceListener() {
            @Override
            public void onInputDeviceAdded(int deviceId) { nativeMouseChanged(hasMouse(context)); }
            @Override
            public void onInputDeviceRemoved(int deviceId) { nativeMouseChanged(hasMouse(context)); }
            @Override
            public void onInputDeviceChanged(int deviceId) { nativeMouseChanged(hasMouse(context)); }
        }, new Handler(Looper.getMainLooper()));
    }

    private static native void nativeMouseChanged(boolean present);
}
