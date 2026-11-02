#pragma once

#include <QDBusArgument>
#include <QDBusContext>
#include <QDBusObjectPath>
#include <QDBusServiceWatcher>
#include <QMap>
#include <QObject>

struct RegisteredMenu
{
    uint window;
    QString service;
    QDBusObjectPath path;
};
using RegisteredMenus = QList<RegisteredMenu>;
Q_DECLARE_METATYPE(RegisteredMenu)
Q_DECLARE_METATYPE(RegisteredMenus)
QDBusArgument& operator<<(QDBusArgument& argument, const RegisteredMenu& menu);
const QDBusArgument& operator>>(const QDBusArgument& argument, RegisteredMenu& menu);

class MenuRegistrar : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.canonical.AppMenu.Registrar")
    Q_PROPERTY(bool registered READ isRegistered CONSTANT)

  public:
    explicit MenuRegistrar(QObject* parent = nullptr);
    ~MenuRegistrar() override;
    bool isRegistered() const;

  public slots:
    void RegisterWindow(uint window, const QDBusObjectPath& path);
    void UnregisterWindow(uint window);
    QString GetMenuForWindow(uint window, QDBusObjectPath& path);
    RegisteredMenus GetMenus();

  signals:
    void WindowRegistered(uint window, const QString& service, const QDBusObjectPath& path);
    void WindowUnregistered(uint window);

  private:
    bool registered = false;
    QMap<uint, RegisteredMenu> menus;
    QDBusServiceWatcher owners;
};
