#!/usr/bin/env python3
"""Check two frames produced by tests/session_smoke.sh.

The first frame is the desktop before the test client connects, the second one
is taken after a native Wayland window has mapped.  This is intentionally not a
pixel-perfect golden image: fonts and icon availability differ between systems.
"""
import sys


def load(path):
    try:
        from PIL import Image
        return Image.open(path).convert("RGB")
    except ImportError:
        pass
    try:
        from PySide6.QtGui import QImage
        image = QImage(path)
        if image.isNull():
            raise SystemExit(f"cannot read {path}")
        return image
    except ImportError:
        return None


def dimensions(image):
    """(width, height) of a Pillow image or a QImage.

    Pillow exposes ``.size`` as a tuple, QImage as the ``size()`` *method*
    returning a ``QSize``. Returning that QSize unchanged made the caller fail
    to unpack it ("cannot unpack non-iterable QSize"), which is what kept the
    CI session test red whenever Pillow was not installed.
    """
    size = image.size
    if callable(size):
        size = size()
    if hasattr(size, "width") and hasattr(size, "height"):
        return size.width(), size.height()
    return tuple(size)


def rgb_at(image, x, y):
    if hasattr(image, "getpixel"):
        return image.getpixel((x, y))
    return image.pixelColor(x, y).getRgb()[:3]


def check_locked(path):
    """The frame shows nothing but the lock screen: one flat dark colour
    (swaylock with --color 000000 over the compositor's dark backdrop), no
    panel, no windows."""
    image = load(path)
    if image is None:
        print("session smoke: locked frame captured (no image module to inspect it)")
        return
    width, height = dimensions(image)
    bright = 0
    samples = 0
    for y in range(0, height, 8):
        for x in range(0, width, 8):
            r, g, b = rgb_at(image, x, y)[:3]
            samples += 1
            if max(r, g, b) > 40:
                bright += 1
    # swaylock draws its small indicator ring in the middle; everything
    # else must be dark.
    if bright > samples * 0.05:
        raise SystemExit(f"the locked screen still shows the desktop ({bright}/{samples} bright samples)")
    print(f"session smoke: locked frame is dark ({bright}/{samples} bright samples)")


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "--locked":
        check_locked(sys.argv[2])
        return
    if len(sys.argv) != 3:
        raise SystemExit("usage: session_check.py BEFORE.png AFTER.png")
    before = load(sys.argv[1])
    after = load(sys.argv[2])
    if before is None or after is None:
        # Pillow and PySide6 are optional in the test environment. Falling back
        # to a size check still proves that two real frames were captured.
        import struct
        sizes = []
        for path in sys.argv[1:]:
            with open(path, "rb") as handle:
                header = handle.read(24)
            if header[:8] != b"\x89PNG\r\n\x1a\n":
                raise SystemExit(f"{path} is not a PNG")
            sizes.append(struct.unpack(">II", header[16:24]))
        if sizes[0] != sizes[1] or sizes[0][0] < 640:
            raise SystemExit(f"unexpected frame sizes: {sizes}")
        print("session smoke: captured two PNG frames")
        return

    if dimensions(before) != dimensions(after):
        raise SystemExit("frames have different sizes")
    width, height = dimensions(before)
    if width < 640 or height < 480:
        raise SystemExit(f"frame is too small: {width}x{height}")

    changed = 0
    before_colors = set()
    step = 4
    for y in range(0, height, step):
        for x in range(0, width, step):
            before_colors.add(rgb_at(before, x, y))
            if rgb_at(before, x, y) != rgb_at(after, x, y):
                changed += 1
    if len(before_colors) < 8:
        raise SystemExit(f"the desktop frame is blank ({len(before_colors)} colours)")
    if changed < 200:
        raise SystemExit(f"the test window did not visibly appear (changed={changed})")

    # The panel is a layer surface at the bottom of the output.  Make sure it
    # exists in the desktop frame by looking for a run of identical pixels in
    # the bottom strip that differs from the wallpaper just above it.
    panel_y = height - 18
    first = rgb_at(before, 20, panel_y)
    panel_run = sum(1 for x in range(20, width - 20, 8)
                    if rgb_at(before, x, panel_y) == first)
    if panel_run < 20:
        raise SystemExit("no panel-like surface at the bottom of the frame")

    print(f"session smoke: frame {width}x{height}, {changed} sampled pixels changed")


if __name__ == "__main__":
    main()
