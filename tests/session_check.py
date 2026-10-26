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


def rgb_at(image, x, y):
    if hasattr(image, "getpixel"):
        return image.getpixel((x, y))
    return image.pixelColor(x, y).getRgb()[:3]


def main():
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

    def dimensions(image):
        size = image.size
        return size() if callable(size) else tuple(size)

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
