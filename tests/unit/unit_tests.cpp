// Unit tests for the shell's C++ services that need no display: desktop
// files and default applications, settings profiles, the day/night theme
// schedule, notification muting and urgency, and the compositor.conf text.
// Built with the shell (not installed) and run by scripts/build-release.sh.
#include "appmodel.h"
#include "notifications.h"
#include "screenrecorder.h"
#include "sessionservices.h"
#include "settings.h"
#include "theme.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>

namespace {

int failures = 0;

void check(bool ok, const QString &what)
{
    std::printf("%s: %s\n", ok ? "ok" : "FAIL", qPrintable(what));
    if (!ok)
        ++failures;
}

void writeFile(const QString &path, const QByteArray &text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(text);
}

QString readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

void testApplications(const QString &root)
{
    writeFile(root + QStringLiteral("/data/applications/firefox.desktop"),
              "[Desktop Entry]\nType=Application\nName=Firefox\nName[ru]=Файрфокс\n"
              "Exec=firefox %u\nCategories=Network;WebBrowser;\nKeywords=web;net\\;work;\n"
              "MimeType=text/html;x-scheme-handler/https;x-scheme-handler/http;\n"
              "Actions=new-window;private;\n# a comment; with a semicolon\n\n"
              "[Desktop Action new-window]\nName=New Window\nExec=firefox --new-window\n\n"
              "[Desktop Action private]\nName=Private\nExec=firefox --private-window\n");
    writeFile(root + QStringLiteral("/config/mimeapps.list"),
              "[Added Associations]\ntext/plain=gedit.desktop\n\n"
              "[Default Applications]\nimage/png=eog.desktop\nx-scheme-handler/https=missing.desktop\n");

    Settings settings;
    AppModel apps(&settings);
    QEventLoop loop;
    QObject::connect(&apps, &AppModel::reloadFinished, &loop, &QEventLoop::quit);
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    apps.reload();
    if (apps.count() == 0)
        loop.exec();

    const QVariantMap firefox = apps.byId(QStringLiteral("firefox.desktop"));
    check(firefox.value(QStringLiteral("categories")).toStringList()
              == QStringList({QStringLiteral("Network"), QStringLiteral("WebBrowser")}),
          QStringLiteral("every category of a desktop file is read (';' is no comment)"));
    check(firefox.value(QStringLiteral("keywords")).toStringList()
              == QStringList({QStringLiteral("web"), QStringLiteral("net;work")}),
          QStringLiteral("an escaped ';' stays inside a list item"));
    const QVariantList actions = firefox.value(QStringLiteral("actions")).toList();
    check(actions.size() == 2 && actions.at(0).toMap().value(QStringLiteral("id")) == QLatin1String("new-window"),
          QStringLiteral("application actions keep the order of the Actions key"));
    check(apps.appsForMime(QStringLiteral("x-scheme-handler/https")).size() == 1,
          QStringLiteral("an application is found by every MIME type it lists"));
    check(apps.defaultApp(QStringLiteral("x-scheme-handler/https")).isEmpty(),
          QStringLiteral("a default that is not installed is ignored"));
    check(apps.setDefaultApp({QStringLiteral("x-scheme-handler/http"), QStringLiteral("x-scheme-handler/https")},
                             QStringLiteral("firefox.desktop"))
              && apps.setDefaultApp({QStringLiteral("x-scheme-handler/https")}, QStringLiteral("firefox.desktop")),
          QStringLiteral("the default browser is written"));
    check(apps.defaultApp(QStringLiteral("x-scheme-handler/https")) == QLatin1String("firefox.desktop"),
          QStringLiteral("and read back"));
    const QString list = readFile(root + QStringLiteral("/config/mimeapps.list"));
    check(list.count(QStringLiteral("x-scheme-handler/https=")) == 1 && list.contains(QStringLiteral("image/png=eog.desktop"))
              && list.contains(QStringLiteral("text/plain=gedit.desktop")),
          QStringLiteral("mimeapps.list keeps other entries and has no duplicates"));
}

// A typo made in a text editor: the shell says where it is and keeps the
// broken file, and a fixed file clears the error.
void testBrokenSettings()
{
    const QString file = Settings::configFile();
    const QByteArray broken = "{\n  \"panel.height\": 64\n  \"dock.iconSize\": 40\n}\n";
    writeFile(file, broken);
    Settings settings;
    check(settings.loadError().startsWith(QLatin1String("Line 3")),
          QStringLiteral("a broken settings file names the line of the error (%1)").arg(settings.loadError()));
    check(settings.value(QStringLiteral("panel.height")).toInt() != 64,
          QStringLiteral("values of a broken file are not half-applied"));
    check(readFile(settings.brokenCopy()).toUtf8() == broken,
          QStringLiteral("the broken file is kept next to it"));
    writeFile(file, "{\"panel.height\": 64}");
    settings.reload();
    check(settings.loadError().isEmpty() && settings.value(QStringLiteral("panel.height")).toInt() == 64,
          QStringLiteral("a fixed file clears the error and applies"));
}

// Where screen recordings go: recorder.folder (with ~) and recorder.format.
void testRecorder()
{
    Settings settings;
    ScreenRecorder recorder(&settings, nullptr);
    check(recorder.outputFile().contains(QLatin1String("/Screencasts/Screencast_"))
              && recorder.outputFile().endsWith(QLatin1String(".mp4")),
          QStringLiteral("recordings go to Screencasts as MP4 by default (%1)").arg(recorder.outputFile()));
    settings.setValue(QStringLiteral("recorder.folder"), QStringLiteral("~/Clips"));
    settings.setValue(QStringLiteral("recorder.format"), QStringLiteral("mkv"));
    const QString file = recorder.outputFile();
    check(file.startsWith(QDir::homePath() + QStringLiteral("/Clips/Screencast_")) && file.endsWith(QLatin1String(".mkv"))
              && QFileInfo(QDir::homePath() + QStringLiteral("/Clips")).isDir(),
          QStringLiteral("recorder.folder and recorder.format choose the file (%1)").arg(file));
    settings.setValue(QStringLiteral("recorder.format"), QStringLiteral("avi"));
    check(recorder.outputFile().endsWith(QLatin1String(".mp4")), QStringLiteral("an unknown format records MP4"));
    settings.reset(QStringLiteral("recorder.folder"));
    settings.reset(QStringLiteral("recorder.format"));
}

void testProfiles(const QString &root)
{
    Settings settings;
    settings.setValue(QStringLiteral("panel.height"), 60);
    settings.setValue(QStringLiteral("apps.recent"), QStringList{QStringLiteral("a.desktop")});
    check(settings.saveProfile(QStringLiteral("Look one")), QStringLiteral("a profile is saved"));
    check(!settings.saveProfile(QStringLiteral("../escape")), QStringLiteral("a profile name cannot leave the folder"));
    settings.setValue(QStringLiteral("panel.height"), 30);
    settings.setValue(QStringLiteral("dock.iconSize"), 40);
    settings.setValue(QStringLiteral("apps.recent"), QStringList{QStringLiteral("b.desktop")});
    check(settings.applyProfile(QStringLiteral("Look one")), QStringLiteral("a profile applies"));
    check(settings.value(QStringLiteral("panel.height")).toInt() == 60
              && settings.value(QStringLiteral("dock.iconSize")).toInt() == 28,
          QStringLiteral("applying a profile sets its values and resets the others"));
    check(settings.value(QStringLiteral("apps.recent")).toStringList() == QStringList{QStringLiteral("b.desktop")},
          QStringLiteral("personal data stays out of profiles"));
    writeFile(root + QStringLiteral("/export.json"), "{\"panel.height\": 72, \"not.a.key\": 1}");
    writeFile(root + QStringLiteral("/junk.json"), "{\"a\": 1}");
    const QString name = settings.importProfile(root + QStringLiteral("/export.json"));
    check(name == QLatin1String("export"), QStringLiteral("an exported file imports as a profile"));
    check(settings.importProfile(root + QStringLiteral("/export.json")) == QLatin1String("export (2)"),
          QStringLiteral("a second import gets its own name"));
    check(settings.importProfile(root + QStringLiteral("/junk.json")).isEmpty(),
          QStringLiteral("a file without settings is refused"));
    settings.applyProfile(name);
    check(settings.value(QStringLiteral("panel.height")).toInt() == 72 && !settings.has(QStringLiteral("not.a.key")),
          QStringLiteral("an imported profile applies only known keys"));
    check(settings.deleteProfile(QStringLiteral("export (2)"))
              && !settings.profiles().contains(QStringLiteral("export (2)")),
          QStringLiteral("a profile is deleted"));
}

void testThemeSchedule()
{
    Settings settings;
    ThemeManager theme(&settings);
    settings.setValue(QStringLiteral("apps.colorScheme"), QStringLiteral("dark"));
    settings.setValue(QStringLiteral("theme.accent"), QStringLiteral("#ff0000"));
    check(theme.accent().name() == QLatin1String("#ff0000"), QStringLiteral("theme.accent wins over the theme"));
    settings.setValue(QStringLiteral("theme.schedule"), QStringLiteral("times"));
    settings.setValue(QStringLiteral("theme.dayStart"), QStringLiteral("00:00"));
    settings.setValue(QStringLiteral("theme.nightStart"), QStringLiteral("23:59"));
    check(!theme.night() && theme.name() == QLatin1String("light")
              && settings.value(QStringLiteral("apps.colorScheme")).toString() == QLatin1String("light"),
          QStringLiteral("turning the schedule on by day shows the day theme and light applications"));
    settings.setValue(QStringLiteral("theme.dayStart"), QStringLiteral("23:59"));
    settings.setValue(QStringLiteral("theme.nightStart"), QStringLiteral("00:00"));
    check(theme.night() && settings.value(QStringLiteral("apps.colorScheme")).toString() == QLatin1String("dark"),
          QStringLiteral("at night the night theme and dark applications"));
    settings.setValue(QStringLiteral("theme.schedule"), QStringLiteral("off"));
    check(!theme.night() && theme.name() == QLatin1String("default"), QStringLiteral("off: the chosen theme"));
}

uint post(NotificationDaemon &daemon, const char *app, int urgency)
{
    QVariantMap hints;
    if (urgency >= 0)
        hints.insert(QStringLiteral("urgency"), QVariant::fromValue<uchar>(static_cast<uchar>(urgency)));
    return daemon.Notify(QString::fromLatin1(app), 0, QString(), QStringLiteral("Summary"),
                         QStringLiteral("Body"), {}, hints, -1);
}

void testNotifications()
{
    Settings settings;
    settings.setValue(QStringLiteral("notifications.soundEnabled"), false);
    NotificationDaemon daemon(&settings);
    post(daemon, "Mail", -1);
    check(daemon.active().size() == 1, QStringLiteral("a notification shows"));
    settings.setValue(QStringLiteral("notifications.mutedApps"), QStringList{QStringLiteral("mail")});
    post(daemon, "Mail", 1);
    check(daemon.active().size() == 1 && daemon.history().size() == 2,
          QStringLiteral("a muted application only reaches the history"));
    post(daemon, "Mail", 2);
    check(daemon.active().size() == 2, QStringLiteral("an urgent one from a muted application shows"));
    settings.setValue(QStringLiteral("notifications.doNotDisturb"), true);
    post(daemon, "Chat", 1);
    check(daemon.active().size() == 2, QStringLiteral("do not disturb holds a normal notification"));
    settings.setValue(QStringLiteral("notifications.urgencyLevel"), 1);
    post(daemon, "Chat", 1);
    check(daemon.active().size() == 3, QStringLiteral("urgencyLevel 1 lets normal ones through"));
    settings.setValue(QStringLiteral("notifications.urgencyLevel"), 3);
    post(daemon, "Chat", 2);
    check(daemon.active().size() == 3, QStringLiteral("urgencyLevel 3 holds even urgent ones"));
}

void testCompositorConfig()
{
    Settings settings;
    settings.setValue(QStringLiteral("hotkeys.lockScreen"), QStringLiteral("Super+Shift+K"));
    settings.setValue(QStringLiteral("hotkeys.startMenu"), QStringLiteral("Super"));
    settings.setValue(QStringLiteral("windowRules.workspaces"), QStringLiteral("firefox = 2, bad, x=12, Thunder.bird=3"));
    settings.setValue(QStringLiteral("windowRules.opacity"), QStringLiteral("kitty=85;code=5"));
    settings.setValue(QStringLiteral("hotCorners.topLeft"), QStringLiteral("terminal"));
    settings.setValue(QStringLiteral("hotCorners.topRight"), QStringLiteral("rm -rf /"));
    settings.setValue(QStringLiteral("decorations.middleClick"), QStringLiteral("bogus"));
    settings.setValue(QStringLiteral("windows.gaps"), 200);
    settings.setValue(QStringLiteral("windows.dragModifier"), QStringLiteral("ctrl"));
    SessionServices session(&settings);
    const QStringList lines = session.compositorConfigText().split(QLatin1Char('\n'));
    check(lines.contains(QStringLiteral("hotkey.lockScreen = Super+Shift+K"))
              && !lines.filter(QStringLiteral("hotkey.startMenu")).size(),
          QStringLiteral("moved shortcuts are written, a lone Super is the default"));
    check(lines.contains(QStringLiteral("rules.workspace = firefox=2,Thunder.bird=3"))
              && lines.contains(QStringLiteral("rules.opacity = kitty=85")),
          QStringLiteral("window rules keep only valid application=number pairs"));
    check(lines.contains(QStringLiteral("hotCorners.topLeft = terminal"))
              && lines.contains(QStringLiteral("hotCorners.topRight = none")),
          QStringLiteral("hot corner actions are checked"));
    check(lines.filter(QStringLiteral("decorations.middleClick")).isEmpty()
              && lines.contains(QStringLiteral("windows.gaps = 64")),
          QStringLiteral("bad title bar actions are dropped and gaps clamped"));
    check(lines.contains(QStringLiteral("windows.dragModifier = super")),
          QStringLiteral("an unknown drag key falls back to Super"));
    settings.setValue(QStringLiteral("windows.dragModifier"), QStringLiteral("alt"));
    check(session.compositorConfigText().split(QLatin1Char('\n')).contains(QStringLiteral("windows.dragModifier = alt")),
          QStringLiteral("Alt as the drag key reaches compositor.conf"));
}

} // namespace

int main(int argc, char **argv)
{
    // Every test writes into a private home; nothing touches the real one.
    QTemporaryDir home;
    if (!home.isValid())
        return 2;
    const QString root = home.path();
    qputenv("HOME", root.toUtf8());
    qputenv("XDG_CONFIG_HOME", (root + QStringLiteral("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (root + QStringLiteral("/data")).toUtf8());
    qputenv("XDG_DATA_DIRS", (root + QStringLiteral("/system")).toUtf8());
    qputenv("XDG_CONFIG_DIRS", (root + QStringLiteral("/etc")).toUtf8());
    qputenv("XDG_CACHE_HOME", (root + QStringLiteral("/cache")).toUtf8());
    qputenv("XDG_RUNTIME_DIR", root.toUtf8());
    qputenv("LANG", "ru_RU.UTF-8");
    QCoreApplication app(argc, argv);

    testApplications(root);
    testBrokenSettings();
    testRecorder();
    testProfiles(root);
    testThemeSchedule();
    testNotifications();
    testCompositorConfig();

    std::printf("%s (%d failed)\n", failures == 0 ? "unit tests passed" : "unit tests FAILED", failures);
    return failures == 0 ? 0 : 1;
}
