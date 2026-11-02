#include <QCoreApplication>
#include <QDBusConnection>
#include <QJsonDocument>
#include <QTimer>

#include "active-window.hpp"
#include "application-menu.hpp"
#include "menu-registrar.hpp"

class MenuProbe : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mavernix.MenuTest")
    MenuRegistrar registrar;
    ActiveWindow active;
    ApplicationMenu menu;

  public:
    MenuProbe()
    {
        connect(&active, &ActiveWindow::changed, this, [this] { menu.setAddress(active.menuAddress()); });
    }

  public slots:
    QString State()
    {
        return QString::fromUtf8(QJsonDocument::fromVariant(
            QVariantMap{{"address", active.menuAddress()}, {"app", active.appId()}, {"items", menu.items()}})
                .toJson(QJsonDocument::Compact));
    }
    void Activate(int id) { menu.activate(id); }
    void AboutToShow(int id) { menu.aboutToShow(id); }
    void MenusClosed() { menu.menusClosed(); }
    void Quit() { QCoreApplication::quit(); }
};

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    MenuProbe probe;
    auto bus = QDBusConnection::sessionBus();
    if (!bus.registerObject("/", &probe, QDBusConnection::ExportAllSlots) ||
        !bus.registerService("org.mavernix.MenuTest"))
        return 1;
    QTimer::singleShot(45000, &app, &QCoreApplication::quit);
    return app.exec();
}

#include "menu-probe.moc"
