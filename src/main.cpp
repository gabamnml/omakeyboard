#include <QFont>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>
#include <QVariant>

#include "core/ConfigRepository.h"
#include "core/HyprlandReloadService.h"
#include "core/KeyCaptureService.h"
#include "core/ThemeService.h"
#include "models/ActionCatalogModel.h"
#include "models/KeyMappingModel.h"
#include "models/ShortcutModel.h"

using namespace omakeyboard;

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("omakeyboard"));
    app.setApplicationDisplayName(QStringLiteral("OmaKeyboard"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));
    app.setDesktopFileName(QStringLiteral("omakeyboard"));
    app.setOrganizationName(QStringLiteral("OmaKeyboard"));
    // The installed theme icon when there is one; the embedded copy otherwise,
    // so a build run straight from the source tree still has its icon.
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("omakeyboard"),
                                       QIcon(QStringLiteral(":/omakeyboard.png"))));

    // Match Omawrite: a normal Wayland toplevel with the Material style, so the
    // window behaves like every other app on the desktop.
    QQuickStyle::setStyle(QStringLiteral("Material"));

    ThemeService theme;
    ConfigRepository repository;
    KeyCaptureService capture(new HyprlandReloadService(&app), &app);

    // Omarchy's shell is monospace throughout, resolved through the fontconfig
    // alias that `omarchy font set` writes, so the app follows whatever font
    // the user picked for their desktop rather than pinning one of its own.
    // Sizes come from the theme's base-size; the desktop's text scale
    // multiplies it, the way Omawrite follows "display text size".
    const auto applyInterfaceFont = [&app, &theme]() {
        QFont font(QStringLiteral("monospace"));
        font.setStyleHint(QFont::Monospace);
        font.setPixelSize(qMax(1, qRound(theme.fontBaseSize() * theme.textScale())));
        app.setFont(font);
    };
    applyInterfaceFont();
    QObject::connect(&theme, &ThemeService::textScaleChanged, &app, applyInterfaceFont);
    QObject::connect(&theme, &ThemeService::themeChanged, &app, applyInterfaceFont);

    KeyMappingModel mappings;
    mappings.setRepository(&repository);
    mappings.setProfileId(repository.activeProfileId());
    QObject::connect(&repository, &ConfigRepository::activeProfileChanged, &mappings,
                     [&mappings, &repository]() {
                         mappings.setProfileId(repository.activeProfileId());
                     });

    ShortcutModel shortcuts;
    shortcuts.setRepository(&repository);

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &app,
                     [](const QList<QQmlError> &warnings) {
                         for (const QQmlError &warning : warnings)
                             qWarning().noquote() << warning.toString();
                     });

    // The catalog models are created per picker in QML, so the type is
    // registered rather than handed over as a single instance.
    qmlRegisterType<ActionCatalogModel>("OmaKeyboard", 1, 0, "CatalogModel");

    QQmlContext *context = engine.rootContext();
    context->setContextProperty(QStringLiteral("config"), &repository);
    context->setContextProperty(QStringLiteral("theme"), &theme);
    context->setContextProperty(QStringLiteral("capture"), &capture);
    context->setContextProperty(QStringLiteral("mappingModel"), &mappings);
    context->setContextProperty(QStringLiteral("shortcutModel"), &shortcuts);

    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (engine.rootObjects().isEmpty()) {
        qCritical() << "Could not load the OmaKeyboard interface.";
        return 1;
    }

    // Development aid, used by bin/smoke and for the screenshots in the
    // README: OMAKEYBOARD_GRAB=<file.png> renders the window and quits.
    // With QT_QPA_PLATFORM=offscreen this works at any size
    // (OMAKEYBOARD_SIZE=WxH) and without a compositor. OMAKEYBOARD_PAGE=n
    // opens a page other than the first, and OMAKEYBOARD_VISIT_PAGES=1 walks
    // through every page, so loading them all is exercised.
    const QString grabPath = qEnvironmentVariable("OMAKEYBOARD_GRAB");
    if (!grabPath.isEmpty()) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        const QStringList size = qEnvironmentVariable("OMAKEYBOARD_SIZE").split(QLatin1Char('x'));
        if (window && size.size() == 2)
            window->resize(size.at(0).toInt(), size.at(1).toInt());

        const bool visitPages = qEnvironmentVariableIntValue("OMAKEYBOARD_VISIT_PAGES") > 0;
        const int page = qEnvironmentVariableIntValue("OMAKEYBOARD_PAGE");
        int delay = 400;
        if (visitPages) {
            for (int index = 0; index < 4; ++index) {
                QTimer::singleShot(delay, &app, [window, index]() {
                    if (window)
                        window->setProperty("pageIndex", index);
                });
                delay += 300;
            }
        }
        if (page > 0) {
            QTimer::singleShot(delay, &app, [window, page]() {
                if (window)
                    window->setProperty("pageIndex", page);
            });
            delay += 300;
        }

        // One dialog, by name, so a screenshot can show it open.
        const QString open = qEnvironmentVariable("OMAKEYBOARD_OPEN");
        if (!open.isEmpty()) {
            QTimer::singleShot(delay, &app, [window, open]() {
                if (window)
                    QMetaObject::invokeMethod(window, "devOpen", Q_ARG(QVariant, open));
            });
            delay += 400;
        }

        QTimer::singleShot(delay + 900, &app, [window, grabPath]() {
            if (window)
                window->grabWindow().save(grabPath);
            QCoreApplication::quit();
        });
    }

    return app.exec();
}
