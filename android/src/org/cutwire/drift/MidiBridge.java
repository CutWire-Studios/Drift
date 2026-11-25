package org.cutwire.drift;

import android.content.Context;
import android.media.midi.MidiDevice;
import android.media.midi.MidiDeviceInfo;
import android.media.midi.MidiInputPort;
import android.media.midi.MidiManager;
import android.media.midi.MidiOutputPort;
import android.media.midi.MidiReceiver;
import android.os.Handler;
import android.os.Looper;

import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

// USB and Bluetooth MIDI controllers for the control-surface layer. Names are unique per call to
// ports(); the native side addresses a device by that name. Called from the Qt thread, never the
// main looper, which is what lets open() wait for the asynchronous openDevice.
public final class MidiBridge {
    private MidiBridge() {}

    private static final class Open {
        MidiDevice device;
        MidiOutputPort fromDevice;
        MidiInputPort toDevice;
        MidiReceiver receiver;
    }

    private static final Map<String, Open> sOpen = new HashMap<>();
    private static final Map<String, MidiDeviceInfo> sInfos = new HashMap<>();

    private static MidiManager manager(Context context) {
        return (MidiManager) context.getSystemService(Context.MIDI_SERVICE);
    }

    // Devices that send MIDI to the phone, one name per line.
    public static String ports(Context context) {
        MidiManager manager = manager(context);
        if (manager == null)
            return "";
        sInfos.clear();
        List<String> names = new ArrayList<>();
        for (MidiDeviceInfo info : manager.getDevices()) {
            if (info.getOutputPortCount() == 0)
                continue;
            String base = info.getProperties().getString(MidiDeviceInfo.PROPERTY_NAME);
            if (base == null || base.isEmpty())
                base = "MIDI device " + info.getId();
            String name = base;
            for (int n = 2; sInfos.containsKey(name); n++)
                name = base + " (" + n + ")";
            sInfos.put(name, info);
            names.add(name);
        }
        return String.join("\n", names);
    }

    public static boolean open(Context context, final String name) {
        final MidiDeviceInfo info = sInfos.get(name);
        MidiManager manager = manager(context);
        if (info == null || manager == null)
            return false;
        final MidiDevice[] opened = new MidiDevice[1];
        final CountDownLatch latch = new CountDownLatch(1);
        manager.openDevice(info, new MidiManager.OnDeviceOpenedListener() {
            @Override
            public void onDeviceOpened(MidiDevice device) {
                opened[0] = device;
                latch.countDown();
            }
        }, new Handler(Looper.getMainLooper()));
        try {
            if (!latch.await(3, TimeUnit.SECONDS) || opened[0] == null)
                return false;
        } catch (InterruptedException e) {
            return false;
        }
        Open entry = new Open();
        entry.device = opened[0];
        entry.fromDevice = entry.device.openOutputPort(0);
        if (entry.fromDevice == null) {
            close(name);
            try {
                entry.device.close();
            } catch (IOException ignored) {
            }
            return false;
        }
        entry.receiver = new MidiReceiver() {
            @Override
            public void onSend(byte[] data, int offset, int count, long timestamp) {
                byte[] copy = new byte[count];
                System.arraycopy(data, offset, copy, 0, count);
                nativeReceived(name, copy);
            }
        };
        entry.fromDevice.connect(entry.receiver);
        if (info.getInputPortCount() > 0)
            entry.toDevice = entry.device.openInputPort(0);
        sOpen.put(name, entry);
        return true;
    }

    public static void close(String name) {
        Open entry = sOpen.remove(name);
        if (entry == null)
            return;
        try {
            if (entry.fromDevice != null) {
                entry.fromDevice.disconnect(entry.receiver);
                entry.fromDevice.close();
            }
            if (entry.toDevice != null)
                entry.toDevice.close();
            entry.device.close();
        } catch (IOException ignored) {
        }
    }

    public static void send(String name, byte[] data) {
        Open entry = sOpen.get(name);
        if (entry == null || entry.toDevice == null)
            return;
        try {
            entry.toDevice.send(data, 0, data.length);
        } catch (IOException ignored) {
        }
    }

    private static native void nativeReceived(String name, byte[] data);
}
