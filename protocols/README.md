# Wayland protocol XML

`xdg-shell.xml`, `wlr-layer-shell-unstable-v1.xml`,
`pointer-constraints-unstable-v1.xml`, `cursor-shape-v1.xml`, `tablet-v2.xml`,
`ext-image-copy-capture-v1.xml` and `wlr-output-power-management-unstable-v1.xml` are vendored so the
compositor can generate the server headers on every supported distribution
without depending on a particular `wayland-protocols` package version.
CMake runs `wayland-scanner server-header` on them at build time. The protocol
licences are included in the XML files themselves.
