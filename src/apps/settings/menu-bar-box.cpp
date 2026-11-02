#include "menu-bar-box.hpp"

#include <QCheckBox>

#include "choices.hpp"

static const QString panel_table = QStringLiteral("panel");

QCheckBox* menu_bar_box(Choices& choices, const QString& item, const QString& words)
{
    if (!choices.value(panel_table, QStringLiteral("right")).toStringList().contains(item))
    {
        return nullptr;
    }
    auto* box = new QCheckBox(words);
    box->setChecked(!choices.value(panel_table, QStringLiteral("hidden")).toStringList().contains(item));
    QObject::connect(box, &QCheckBox::toggled, box, [&choices, item](bool on) {
        QStringList hidden = choices.value(panel_table, QStringLiteral("hidden")).toStringList();
        hidden.removeAll(item);
        if (!on)
        {
            hidden.append(item);
        }
        choices.set(panel_table, QStringLiteral("hidden"), hidden);
    });
    return box;
}

QStringList items_without_a_pane(Choices& choices)
{
    const QStringList with_a_pane = {QStringLiteral("bluetooth"), QStringLiteral("network"), QStringLiteral("volume"),
        QStringLiteral("battery"), QStringLiteral("clock")};
    QStringList items = choices.value(panel_table, QStringLiteral("right")).toStringList();
    for (const QString& item : with_a_pane)
    {
        items.removeAll(item);
    }
    return items;
}
