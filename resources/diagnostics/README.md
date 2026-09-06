# Diagnostics benchmark clip

`bench1080p60.mp4` is the fixed input for the playback diagnostics benchmark. Its point is to
be the *same* input everywhere, so two bug reports produce comparable numbers — the timeline's
own clips are measured separately, for the reporter's real codec and resolution.

1080p60 on purpose: that is the material the preview struggles with, and a 30 fps clip would
not exercise the frame budget the reports are about. `testsrc2` rather than a solid colour
because a static frame compresses to nothing and decodes instantly, which would measure the
demuxer rather than the decoder.

Regenerate with:

    ffmpeg -y -f lavfi -i "testsrc2=size=1920x1080:duration=1:rate=60" \
        -c:v libx264 -preset veryslow -crf 32 -pix_fmt yuv420p -g 30 bench1080p60.mp4

Keep it small — it ships in every build.
