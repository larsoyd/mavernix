#include "xmenu-properties.hpp"

#include <QElapsedTimer>
#include <QTimer>
#include <xcb/xcb.h>
#include <xcb/xcbext.h>

#include <array>
#include <cstdlib>
#include <cstring>

namespace
{
const std::array<const char*, 7> properties = {"_KDE_NET_WM_APPMENU_SERVICE_NAME", "_KDE_NET_WM_APPMENU_OBJECT_PATH",
    "_GTK_UNIQUE_BUS_NAME", "_GTK_MENUBAR_OBJECT_PATH", "_GTK_APP_MENU_OBJECT_PATH", "_GTK_APPLICATION_OBJECT_PATH",
    "_GTK_WINDOW_OBJECT_PATH"};
}

struct XMenuProperties::State
{
    XMenuProperties* owner;
    xcb_connection_t* connection = nullptr;
    uint window = 0;
    std::array<unsigned, 7> pending{};
    std::array<xcb_atom_t, 7> atoms{};
    std::array<QString, 7> values{};
    bool readingAtoms = true;
    QTimer poll;
    QElapsedTimer deadline;

    explicit State(XMenuProperties* owner) : owner(owner)
    {
        poll.setInterval(10);
        QObject::connect(&poll, &QTimer::timeout, owner, [this] { receive(); });
    }

    void cancel()
    {
        poll.stop();
        for (auto& sequence : pending)
        {
            if (sequence && connection)
                xcb_discard_reply(connection, sequence);
            sequence = 0;
        }
    }

    void receive()
    {
        if (xcb_connection_has_error(connection) || deadline.elapsed() > 1000)
        {
            cancel();
            emit owner->resolved(window, {});
            return;
        }
        bool complete = true;
        for (size_t i = 0; i < pending.size(); ++i)
        {
            if (!pending[i])
                continue;
            void* reply = nullptr;
            xcb_generic_error_t* error = nullptr;
            if (!xcb_poll_for_reply(connection, pending[i], &reply, &error))
            {
                complete = false;
                continue;
            }
            pending[i] = 0;
            if (reply && !error)
            {
                if (readingAtoms)
                    atoms[i] = static_cast<xcb_intern_atom_reply_t*>(reply)->atom;
                else
                {
                    auto* property = static_cast<xcb_get_property_reply_t*>(reply);
                    if (property->format == 8 && property->bytes_after == 0)
                        values[i] = QString::fromUtf8(static_cast<char*>(xcb_get_property_value(property)),
                            xcb_get_property_value_length(property));
                }
            }
            free(reply);
            free(error);
        }
        if (!complete)
            return;
        if (readingAtoms)
        {
            readingAtoms = false;
            for (size_t i = 0; i < atoms.size(); ++i)
                if (atoms[i])
                    pending[i] =
                        xcb_get_property(connection, false, window, atoms[i], XCB_GET_PROPERTY_TYPE_ANY, 0, 1024)
                            .sequence;
            xcb_flush(connection);
            return;
        }
        poll.stop();
        QVariantMap address;
        if (!values[0].isEmpty() && !values[1].isEmpty())
            address = {{"protocol", "dbusmenu"}, {"service", values[0]}, {"path", values[1]}};
        else if (!values[2].isEmpty() && (!values[3].isEmpty() || !values[4].isEmpty()))
            address = {{"protocol", "gmenu"}, {"service", values[2]},
                {"path", values[3].isEmpty() ? values[4] : values[3]}, {"appMenu", values[4]},
                {"appActions", values[5]}, {"windowActions", values[6]}};
        emit owner->resolved(window, address);
    }
};

XMenuProperties::XMenuProperties(QObject* parent) : QObject(parent), state(std::make_unique<State>(this)) {}
XMenuProperties::~XMenuProperties()
{
    state->cancel();
    if (state->connection)
        xcb_disconnect(state->connection);
}

void XMenuProperties::request(uint window)
{
    state->cancel();
    state->window = window;
    if (!window || qEnvironmentVariableIsEmpty("DISPLAY"))
    {
        emit resolved(window, {});
        return;
    }
    if (!state->connection)
        state->connection = xcb_connect(nullptr, nullptr);
    state->values = {};
    state->atoms = {};
    state->readingAtoms = true;
    for (size_t i = 0; i < properties.size(); ++i)
        state->pending[i] = xcb_intern_atom(state->connection, true, strlen(properties[i]), properties[i]).sequence;
    xcb_flush(state->connection);
    state->deadline.start();
    state->poll.start();
}
