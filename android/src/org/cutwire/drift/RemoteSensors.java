package org.cutwire.drift;

import android.content.Context;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;

// Gyroscope rates for the phone remote's fly-mode steering. The native side turns them into
// relative control movements.
public final class RemoteSensors {
    private RemoteSensors() {}

    private static SensorManager sManager;
    private static SensorEventListener sListener;

    public static synchronized boolean start(Context context) {
        if (sListener != null)
            return true;
        sManager = (SensorManager) context.getSystemService(Context.SENSOR_SERVICE);
        if (sManager == null)
            return false;
        Sensor gyro = sManager.getDefaultSensor(Sensor.TYPE_GYROSCOPE);
        if (gyro == null)
            return false;
        sListener = new SensorEventListener() {
            @Override
            public void onSensorChanged(SensorEvent event) {
                nativeGyro(event.values[0], event.values[1], event.values[2]);
            }

            @Override
            public void onAccuracyChanged(Sensor sensor, int accuracy) {}
        };
        return sManager.registerListener(sListener, gyro, SensorManager.SENSOR_DELAY_GAME);
    }

    public static synchronized void stop() {
        if (sManager != null && sListener != null)
            sManager.unregisterListener(sListener);
        sListener = null;
    }

    private static native void nativeGyro(float x, float y, float z);
}
