#pragma once

#include <QString>
#include <QStringList>
#include <QVariantList>

// One launchable application parsed from an XDG .desktop file.
//
// The fields mirror the desktop entry specification closely enough that the Rust
// core can produce them directly; the C++ parser in appmodel.cpp fills the same
// structure when the project is built without Rust. Shared by AppModel and the
// Rust bridge, so it lives in its own header to keep both include graphs clean.
struct AppEntry {
    QString id;           // desktop file name, e.g. "firefox-esr.desktop"
    QString path;         // absolute path of the file it was parsed from
    QString name;         // Name=, already localised
    QString genericName;  // GenericName=
    QString comment;      // Comment=
    QString exec;         // Exec=, field codes kept verbatim
    QString tryExec;      // TryExec=
    QString icon;         // Icon=
    QStringList categories;
    QStringList keywords;
    QStringList mimeTypes;
    QStringList onlyShowIn;
    QStringList notShowIn;
    QVariantList actions; // [{id,name,exec,icon}] from [Desktop Action ...] groups
    bool terminal = false;
    bool noDisplay = false;
    bool hidden = false;
    bool startupNotify = false;
    bool dbusActivatable = false;
    QString startupWmClass;
    QString sortKey;  // folded name, so sorting does not fold again per compare
    int priority = 0; // higher wins when several directories ship the same id

    // First token of Exec=, i.e. the program name without its arguments.
    QString program() const;
};
