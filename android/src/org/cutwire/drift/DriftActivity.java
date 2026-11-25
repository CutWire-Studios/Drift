package org.cutwire.drift;

import android.os.Bundle;
import android.system.ErrnoException;
import android.system.Os;
import android.view.KeyEvent;
import android.view.MotionEvent;

import org.qtproject.qt.android.bindings.QtActivity;

// Qt's Android accessibility bridge turns on whenever any accessibility service is running, not
// just a screen reader. Each tree query blocks the UI thread on the Qt thread, and walking our
// QML tree is quadratic, so it ANRs; while it holds the global deadlock protector, a window
// creating its EGL surface qFatals. Qt reads the switch from Java during activity setup, so it
// must be set here, before super.onCreate(); qputenv in main() would be too late.
public class DriftActivity extends QtActivity
{
    @Override
    public void onCreate(Bundle savedInstanceState)
    {
        try {
            Os.setenv("QT_ANDROID_DISABLE_ACCESSIBILITY", "1", true);
        } catch (ErrnoException e) {
        }
        super.onCreate(savedInstanceState);
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event)
    {
        return GamepadInput.handleKey(event) || super.dispatchKeyEvent(event);
    }

    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent event)
    {
        return GamepadInput.handleMotion(event) || super.dispatchGenericMotionEvent(event);
    }

    @Override
    public void onPointerCaptureChanged(boolean hasCapture)
    {
        super.onPointerCaptureChanged(hasCapture);
        PointerCapture.changed(hasCapture);
    }
}
