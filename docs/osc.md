# OSC control

Drift listens for OSC 1.0 messages over UDP, so TouchOSC, Open Stage Control, Max, Pure Data, a script or anything else that speaks OSC can drive the editor. Messages and bundles are both accepted. Turn it on under Settings, Control surfaces, OSC.

| Setting | Default | Meaning |
|---|---|---|
| Listen port | 8000 | UDP port Drift receives on. |
| Feedback port | 9000 | UDP port on the sender's machine that Drift answers to. |
| Allowed hosts | empty | One host or CIDR range per line (`192.168.1.40`, `10.0.0.0/24`). Empty accepts the local network only: loopback, private ranges and link-local addresses. |

Drift answers whichever host it last heard from. A phone that sends from 192.168.1.40 gets feedback at 192.168.1.40:9000.

OSC has no authentication. Leave it off on networks you do not trust and keep the allowed hosts list as narrow as you can.

## Argument types

Receives `i`, `f`, `d`, `h`, `s`, `b`, `T`, `F`, `N` and `I`. A message with no arguments counts as 1 (pressed). Sends `i`, `f`, `s`, `T` and `F`.

## Fixed address space

Anything under `/drift/` has a fixed meaning.

| Address | Argument | Effect |
|---|---|---|
| `/drift/transport/play` | none or > 0 | Play or pause. |
| `/drift/transport/stop` | none or > 0 | Stop and reset the playback speed. |
| `/drift/action/<id>` | none or > 0 | Run an editor action. `<id>` is any action id from the keyboard shortcuts list, for example `/drift/action/split` or `/drift/action/zoomIn`. |
| `/drift/param/<target>` | float | Set or nudge a parameter, see below. |
| `/drift/playhead` | float seconds | Move the playhead. |
| `/drift/ping` | none | Drift replies `/drift/pong`. |

### Parameter targets

`/drift/param/<target>` takes the id of a control-surface target. What the value means depends on the target:

- An absolute target takes 0 to 1 and jumps there. There is no pickup, the value is applied as sent.
- A relative target takes a signed number of ticks and moves by that much.
- An action target runs when the value is above 0.

| Target | Kind | What it drives |
|---|---|---|
| `playhead.jog` | relative | Step the playhead, one tick is one frame. |
| `playhead.shuttle` | absolute | A spring fader: 0.5 is still, towards 0 rewinds, towards 1 plays forward. |
| `playhead.scrub` | absolute | Playhead position across the whole timeline. |
| `timeline.zoom` | relative | Timeline zoom in (positive) or out (negative). |
| `mixer.volume.1` to `mixer.volume.8` | absolute | Strip volume. 0.75 is unity, 1 is +6 dB. |
| `mixer.pan.1` to `mixer.pan.8` | absolute | Strip pan, 0.5 is centre. |
| `mixer.mute.1` to `mixer.mute.8` | action | Toggle mute. |
| `mixer.solo.1` to `mixer.solo.8` | action | Toggle solo. |
| `mixer.bankPrev`, `mixer.bankNext` | action | Move the eight strips to the previous or next group of tracks. |
| `clip.volume`, `clip.opacity`, `clip.scale`, `clip.rotation` | absolute | The selected clip. |
| `color.exposure`, `color.contrast`, `color.highlights`, `color.shadows`, `color.whites`, `color.blacks`, `color.temperature`, `color.tint`, `color.saturation`, `color.vibrance` | absolute | The selected clip's colour grade. |
| `color.lift.x`, `color.lift.y`, `color.lift.luma` (also `gamma`, `gain`) | absolute | The colour wheels. 0.5 is the centre. |
| `view3d.fly.yaw`, `view3d.fly.pitch`, `view3d.fly.forward`, `view3d.fly.strafe` | relative | Fly the 3D editor view or the camera being looked through. |

Targets that act on "the selected clip" do nothing when no clip is selected.

## Feedback

Drift sends these to the last sender's feedback port as they change, at most ten times a second.

| Address | Argument |
|---|---|
| `/drift/playhead` | float, seconds |
| `/drift/transport/playing` | int, 1 while playing |

## Your own layout

Any address outside `/drift/` becomes a control of the "OSC" device in Settings, Control surfaces the first time it arrives. A float argument makes it a fader or knob, anything else makes it a button. Turn on learn mode, click a parameter in Drift, then touch the control in your app, and the two are bound. Drift sends the parameter's current value back to the same address, so faders in TouchOSC and Open Stage Control follow the editor.

The default TouchOSC layouts send `/1/fader1`, `/1/push1` and similar. They work as they are.
