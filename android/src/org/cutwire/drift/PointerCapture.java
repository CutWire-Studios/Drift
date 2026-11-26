package org.cutwire.drift;

import android.app.Activity;
import android.content.Context;
import android.view.InputDevice;
import android.view.MotionEvent;
import android.view.View;

// Pointer capture for 3D fly mode: the system hides the cursor and delivers raw relative motion
// (SOURCE_MOUSE_RELATIVE) to the focused view's captured-pointer listener.
public final class PointerCapture {
    private PointerCapture() {}

    private static final View.OnCapturedPointerListener LISTENER = new View.OnCapturedPointerListener() {
        @Override
        public boolean onCapturedPointer(View view, MotionEvent event) {
            if (event.getSource() == InputDevice.SOURCE_MOUSE_RELATIVE
                    && event.getActionMasked() == MotionEvent.ACTION_MOVE)
                nativeMotion(event.getX(), event.getY());
            return true;
        }
    };

    public static void request(final Context context) {
        if (!(context instanceof Activity))
            return;
        final Activity activity = (Activity) context;
        activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                View decor = activity.getWindow().getDecorView();
                decor.setOnCapturedPointerListener(LISTENER);
                View focus = decor.findFocus();
                if (focus != null)
                    focus.setOnCapturedPointerListener(LISTENER);
                decor.requestPointerCapture();
            }
        });
    }

    public static void release(final Context context) {
        if (!(context instanceof Activity))
            return;
        final Activity activity = (Activity) context;
        activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                activity.getWindow().getDecorView().releasePointerCapture();
            }
        });
    }

    // From Activity.onPointerCaptureChanged: the system also revokes capture when focus is lost.
    public static void changed(boolean hasCapture) {
        nativeChanged(hasCapture);
    }

    private static native void nativeMotion(float dx, float dy);
    private static native void nativeChanged(boolean active);
}
