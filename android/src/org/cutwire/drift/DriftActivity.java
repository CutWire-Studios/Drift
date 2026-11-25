package org.cutwire.drift;

import android.os.Bundle;
import android.system.ErrnoException;
import android.system.Os;

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
    public void onPointerCaptureChanged(boolean hasCapture)
    {
        super.onPointerCaptureChanged(hasCapture);
        PointerCapture.changed(hasCapture);
    }
}
