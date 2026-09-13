#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QIcon>

#include <KAboutData>
#include <KLocalizedString>
#include <KCrash>

#include "kf6/kf6mainwindow.h"
#include "wildpalms_version.h"
#include "security/kwalletsecretstore.h"
#include <kalburator/sync/secretstore.h>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Ensure Breeze icons are available even outside a full KDE desktop
    // (e.g. AppImage, GNOME, or minimal window managers).
    QIcon::setFallbackThemeName(QStringLiteral("breeze"));

    // Add bundled icons directory to the search path (for AppImage / installed builds).
    // Qt only searches XDG_DATA_DIRS by default, which doesn't include the AppImage mount.
    QDir appDir(QCoreApplication::applicationDirPath());
    QString bundledIcons = appDir.absoluteFilePath(QStringLiteral("../share/icons"));
    if (QDir(bundledIcons).exists()) {
        QStringList paths = QIcon::themeSearchPaths();
        paths.prepend(QDir(bundledIcons).canonicalPath());
        QIcon::setThemeSearchPaths(paths);
    }

    // Add the lib/ directory next to the executable as a plugin search path.
    // This allows KPluginMetaData::findPlugins() to discover conduit plugins
    // when running from the build directory without installing.
    QString localPluginDir = appDir.absoluteFilePath(QStringLiteral("lib"));
    if (QDir(localPluginDir).exists()) {
        QCoreApplication::addLibraryPath(localPluginDir);
    }

    KLocalizedString::setApplicationDomain("wildpalms");

    KAboutData aboutData(
        QStringLiteral("wildpalms"),
        i18n("Wild Palms"),
        QStringLiteral(WILDPALMS_VERSION_STRING),
        i18n("Modern Palm Pilot synchronization for Linux"),
        KAboutLicense::GPL_V3,
        i18n("Copyright (C) 2024-2026 Clinton Ignatov"));
    aboutData.setOrganizationDomain("vibekoder.ca");
    aboutData.setDesktopFileName(QStringLiteral("ca.vibekoder.wildpalms"));
    KAboutData::setApplicationData(aboutData);

    KCrash::initialize();

    WildPalms::KWalletSecretStore secretStore;
    Kalburator::Sync::SecretStoreRegistry::setDefaultStore(&secretStore);

    QCommandLineParser parser;
    aboutData.setupCommandLine(&parser);
    parser.process(app);
    aboutData.processCommandLine(&parser);

    auto *window = new KF6MainWindow;
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->show();

    return app.exec();
}
