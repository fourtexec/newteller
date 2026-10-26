# Wayland protocol XML

`xdg-shell.xml` and `wlr-layer-shell-unstable-v1.xml` are vendored so the
compositor can generate the server headers on every supported distribution
without depending on a particular `wayland-protocols` package version.
CMake runs `wayland-scanner server-header` on them at build time. The protocol
licences are included in the XML files themselves.
