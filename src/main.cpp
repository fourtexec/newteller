// NewTeller - a customizable desktop shell layer for Debian-based systems.
//
// The C++ core provides settings, themes, the application catalogue, session
// actions and plugin discovery. The user interface is QML (with JS logic), so
// every visual part can be replaced or extended by plugins and themes.

#include "appmodel.h"
#include "plugins.h"
#include "settings.h"
#include "shellcontroller.h"
#include "theme.h"

#include <LayerShellQt/Shell>

#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickImageProvider>
#include <QUrl>

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

int main(int argc, char *argv[])
{
    // Must be called before QGuiApplication is created.
    LayerShellQt::Shell::useLayerShell();

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("NewTeller"));
    app.setApplicationVersion(QStringLiteral(NEWTELLER_VERSION));
    app.setDesktopFileName(QStringLiteral("org.newteller.Shell"));
    // The shell lives on as long as the session, even with no visible window.
    app.setQuitOnLastWindowClosed(false);

    // Declared before the engine so they outlive it.
    Settings settings;
    ThemeManager theme(&settings);
    AppModel apps(&settings);
    ShellController shell(&settings);
    PluginManager plugins(&settings);

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("theme"), new ThemeIconProvider);
    engine.rootContext()->setContextProperty(QStringLiteral("Settings"), &settings);
    engine.rootContext()->setContextProperty(QStringLiteral("Theme"), &theme);
    engine.rootContext()->setContextProperty(QStringLiteral("Apps"), &apps);
    engine.rootContext()->setContextProperty(QStringLiteral("Shell"), &shell);
    engine.rootContext()->setContextProperty(QStringLiteral("Plugins"), &plugins);

    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}
