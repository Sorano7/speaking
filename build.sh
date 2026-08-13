gcc main.c player.c overlay.c tray.c \
    -Iinclude \
    -Llib \
    -lole32 \
    -lksuser \
    -lgdi32 \
    -lsherpa-onnx-c-api \
    -mwindows \
    -municode \
    "$@" \
    -o build/speaking
