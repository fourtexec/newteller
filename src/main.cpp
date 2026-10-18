// NewTeller - a customizable desktop shell layer for Debian-based systems.
//
// The C++ core provides settings, themes, the application catalogue, session
// actions, system metrics and plugin discovery. The user interface is QML
// (with JS logic), so every visual part can be replaced or extended by
// plugins and themes.

#include "appmodel.h"
#include "plugins.h"
#include "settings.h"
#include "shellcontroller.h"
#include "shellwindow.h"
#include "systemmonitor.h"
#include "theme.h"
#include "windowsystem.h"

#include <QDesktopServices>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonDocument>
#include <QLockFile>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickImageProvider>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include <cstdio>
#include <cstring>

Q_LOGGING_CATEGORY(lcShell, "newteller")

// Serves system icon theme icons as image://theme/<name>.
class ThemeIconProvider : public QQuickImageProvider {
public:
    ThemeIconProvider()
        : QQuickImageProvider(QQuickImageProvider::Pixmap)
    {
    }

    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override
    {
        const int px = requestedSize.width() > 0 ? requestedSize.width() : 48;
        const QIcon icon = QIcon::fromTheme(id, QIcon::fromTheme(QStringLiteral("application-x-executable")));
        const QPixmap pixmap = icon.pixmap(px, px);
        if (size)
            *size = pixmap.size();
        return pixmap;
    }
};

namespace {

// NewTeller's own flags. They are scanned by hand because QCommandLineParser
// rejects Qt's own options (`-platform xcb` and friends).
struct CliOptions {
    bool printConfig = false;
    bool diagnostics = false;
    bool reset = false;
    bool ok = true;
    QString theme;
    QStringList files;
};

void printUsage()
{
    std::printf(
        "Usage: newteller [options] [files...]\n"
        "\n"
        "Options:\n"
        "  -h, --help           Show this help and exit\n"
        "  -v, --version        Show the version and exit\n"
        "  --print-config       Print the merged configuration as JSON and exit\n"
        "  --diagnostics        Print diagnostics as JSON and exit\n"
        "  --theme <name>       Select the theme (for example: default, light, nord)\n"
        "  --reset              Reset the configuration to defaults before starting\n"
        "\n"
        "Files and URLs are opened with their default applications.\n");
}

CliOptions scanArguments(int argc, char **argv)
{
    CliOptions options;
    for (int i = 1; i < argc; ++i) {
        const QByteArray arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printUsage();
            options.ok = false;
        } else if (arg == "--version" || arg == "-v") {
            std::printf("NewTeller %s\n", NEWTELLER_VERSION);
            options.ok = false;
        } else if (arg == "--print-config") {
            options.printConfig = true;
        } else if (arg == "--diagnostics") {
            options.diagnostics = true;
        } else if (arg == "--reset") {
            options.reset = true;
        } else if (arg == "--theme") {
            if (i + 1 < argc)
                options.theme = QString::fromLocal8Bit(argv[++i]);
            else
                options.ok = false;
        } else if (arg.startsWith("--theme=")) {
            options.theme = QString::fromUtf8(arg.mid(8));
        } else if (!arg.startsWith('-')) {
            // Only real files and URLs count; values of unknown options
            // (`xcb` after `-platform`, for example) are left alone.
            const QString item = QString::fromLocal8Bit(arg);
            const QUrl url = QUrl::fromUserInput(item);
            if (QFile::exists(item) || url.scheme().length() > 1)
                options.files << item;
        }
    }
    return options;
}

QString printJson(const QVariant &value)
{
    const QJsonDocument doc = QJsonDocument::fromVariant(value);
    return QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
}

} // namespace

int main(int argc, char *argv[])
{
    const CliOptions cli = scanArguments(argc, argv);
    if (!cli.ok)
        return cli.printConfig || cli.diagnostics ? 1 : 0;

    // Must run before QGuiApplication is created: native Wayland when the
    // compositor supports wlr-layer-shell, XWayland when it does not.
    //
    // Layer-shell is enabled per window by the org.kde.layershell attached
    // properties in the QML files. LayerShellQt::Shell::useLayerShell() is not
    // used: it makes Qt's whole Wayland platform plugin require the protocol,
    // so the plugin fails to load on compositors without it (GNOME).
    WindowSystem::selectPlatform(argc, argv);

    QGuiApplication app(argc, argv);
    WindowSystem::finishSetup();
    app.setApplicationName(QStringLiteral("NewTeller"));
    app.setApplicationDisplayName(QStringLiteral("NewTeller"));
    app.setApplicationVersion(QStringLiteral(NEWTELLER_VERSION));
    app.setDesktopFileName(QStringLiteral("org.newteller.Shell"));
    // The shell lives on as long as the session, even with no visible window.
    app.setQuitOnLastWindowClosed(false);

    // One shell per user session; the read-only commands work next to it.
    QString lockDir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (lockDir.isEmpty())
        lockDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QLockFile lock(lockDir + QStringLiteral("/newteller.lock"));
    lock.setStaleLockTime(0);
    const bool readOnly = cli.printConfig || cli.diagnostics;
    if (!readOnly && !lock.tryLock(0)) {
        std::fputs("NewTeller is already running.\n", stderr);
        return 1;
    }

    // Declared before the engine so they outlive it.
    Settings settings;
    if (cli.reset) {
        const QStringList keys = settings.keys();
        for (const QString &key : keys)
            settings.reset(key);
        settings.flush();
    }
    if (!cli.theme.isEmpty())
        settings.setValue(QStringLiteral("theme.name"), cli.theme);

    ThemeManager theme(&settings);
    AppModel apps(&settings);
    ShellController shell(&settings);
    PluginManager plugins(&settings);
    SystemMonitor system(&settings);

    if (cli.printConfig) {
        std::printf("%s", qUtf8Printable(printJson(settings.values())));
        return 0;
    }
    if (cli.diagnostics) {
        QVariantMap out = shell.diagnostics();
        out.insert(QStringLiteral("settingsPath"), settings.configPath());
        out.insert(QStringLiteral("configProblems"), settings.validate());
        out.insert(QStringLiteral("apps"), apps.stats());
        out.insert(QStringLiteral("systemAvailable"), system.available());
        std::printf("%s", qUtf8Printable(printJson(out)));
        return 0;
    }

    // import NewTeller 1.0 -> ShellWindow.role (X11 window types and struts).
    qmlRegisterUncreatableType<ShellWindow>("NewTeller", 1, 0, "ShellWindow",
                                            QStringLiteral("ShellWindow is only available as an attached property"));

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("theme"), new ThemeIconProvider);
    engine.rootContext()->setContextProperty(QStringLiteral("Settings"), &settings);
    engine.rootContext()->setContextProperty(QStringLiteral("Theme"), &theme);
    engine.rootContext()->setContextProperty(QStringLiteral("Apps"), &apps);
    engine.rootContext()->setContextProperty(QStringLiteral("Shell"), &shell);
    engine.rootContext()->setContextProperty(QStringLiteral("Plugins"), &plugins);
    engine.rootContext()->setContextProperty(QStringLiteral("System"), &system);

    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    // `newteller file.txt` opens the files with their default applications,
    // the same as double-clicking them on the desktop.
    if (!cli.files.isEmpty()) {
        const QStringList files = cli.files;
        QTimer::singleShot(0, &app, [files] {
            for (const QString &item : files)
                QDesktopServices::openUrl(QUrl::fromUserInput(item));
        });
    }

    return app.exec();
}
