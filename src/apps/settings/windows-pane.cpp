#include "windows-pane.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QProcess>
#include <QSlider>

#include "choices.hpp"
#include "pane-parts.hpp"

// Rewrites the settings GTK programs follow (src/session/start-desktop puts
// it on the path); window frames follow the user's file by themselves.
static const QString reconfigure_command = QStringLiteral("mavernix-reconfigure");
static const QString left_side = QStringLiteral("left");
static const QString close_button = QStringLiteral("close");

// The buttons from left to right, with the close button moved to the
// window's outer end on the chosen side and the others in their order.
static QVariantList order_for_side(const QVariant& order, const QString& side)
{
    QVariantList arranged;
    for (const QVariant& button : order.toList())
    {
        if (button.toString() != close_button)
        {
            arranged.append(button);
        }
    }
    if (side == left_side)
    {
        arranged.prepend(close_button);
    }
    else
    {
        arranged.append(close_button);
    }
    return arranged;
}

WindowsPane::WindowsPane(Choices& choices, QWidget* parent) : QWidget(parent), choices(choices)
{
    const QVariantMap words = choices.configTable(QStringLiteral("settings")).value(QStringLiteral("words")).toMap();
    const auto word = [&words](const char* key, const char* fallback) {
        return words.value(QLatin1String(key), QString::fromUtf8(fallback)).toString();
    };
    const QString table = QStringLiteral("window");

    auto* snap = new QCheckBox(word("windows_snap", "Windows snap to the screen's sides"));
    snap->setChecked(choices.value(table, QStringLiteral("snap_to_edges")).toBool());
    connect(snap, &QCheckBox::toggled, this,
        [this, table](bool on) { this->choices.set(table, QStringLiteral("snap_to_edges"), on); });

    // a plate of layouts under a window dragged toward the top, it rides on snapping
    const QVariant zones_chosen = choices.value(table, QStringLiteral("zone_picker"));
    auto* zones = new QCheckBox(word("windows_zone_picker", "Show layouts when a window is dragged to the top"));
    zones->setChecked(!zones_chosen.isValid() || zones_chosen.toBool());
    zones->setEnabled(snap->isChecked());
    connect(snap, &QCheckBox::toggled, zones, &QWidget::setEnabled);
    connect(zones, &QCheckBox::toggled, this,
        [this, table](bool on) { this->choices.set(table, QStringLiteral("zone_picker"), on); });

    // As on OS X: Quit, from the menu bar or its key, ends the whole program.
    auto* quit_all = new QCheckBox(word("windows_quit_closes_app", "Quit closes all of a program's windows"));
    quit_all->setChecked(choices.value(table, QStringLiteral("quit_closes_app")).toBool());
    connect(quit_all, &QCheckBox::toggled, this,
        [this, table](bool on) { this->choices.set(table, QStringLiteral("quit_closes_app"), on); });

    // Down on a program in the program switcher shows its windows.
    const QVariant switcher_chosen = choices.value(table, QStringLiteral("switcher_windows"));
    auto* switcher_windows = new QCheckBox(word("windows_switcher_windows", "Choose a window in the program switcher"));
    switcher_windows->setChecked(!switcher_chosen.isValid() || switcher_chosen.toBool());
    connect(switcher_windows, &QCheckBox::toggled, this,
        [this, table](bool on) { this->choices.set(table, QStringLiteral("switcher_windows"), on); });

    // X11 games on the screen they ask for, and their "borderless full screen"
    // full screen instead of fitted between the menu bar and the dock.
    const QVariant borderless_chosen = choices.value(table, QStringLiteral("borderless_full_screen"));
    auto* borderless = new QCheckBox(word("windows_borderless", "Games fill the screen they ask for"));
    borderless->setChecked(!borderless_chosen.isValid() || borderless_chosen.toBool());
    connect(borderless, &QCheckBox::toggled, this,
        [this, table](bool on) { this->choices.set(table, QStringLiteral("borderless_full_screen"), on); });

    // How close to a side a dragged window snaps ([window] snap_distance), and
    // how far outside its edge a window can be taken to resize it
    // ([window] resize_reach), in pixels.
    const QVariant chosen_snap = choices.value(table, QStringLiteral("snap_distance"));
    auto* snap_distance = stops_slider({4, 8, 12, 16, 24, 32, 48}, chosen_snap.isValid() ? chosen_snap.toDouble() : 12,
        [this, table](double value) { this->choices.set(table, QStringLiteral("snap_distance"), qRound(value)); });
    auto* snap_row = with_end_words(snap_distance, word("windows_snap_near", "Close"), word("windows_snap_far", "Far"));
    snap_row->setEnabled(snap->isChecked());
    connect(snap, &QCheckBox::toggled, snap_row, &QWidget::setEnabled);
    const QVariant chosen_reach = choices.value(table, QStringLiteral("resize_reach"));
    auto* resize_reach = stops_slider({2, 4, 6, 8, 12, 16, 24}, chosen_reach.isValid() ? chosen_reach.toDouble() : 8,
        [this, table](double value) { this->choices.set(table, QStringLiteral("resize_reach"), qRound(value)); });
    auto* reach_row =
        with_end_words(resize_reach, word("windows_reach_narrow", "Narrow"), word("windows_reach_wide", "Wide"));

    // The key held to move a window by dragging anywhere on it, and to
    // resize it with the secondary button; written as the compositor
    // writes keys.
    auto* drag_key = new QComboBox;
    const struct
    {
        const char* key;
        const char* word_key;
        const char* fallback;
    } key_choices[] = {
        {"", "windows_drag_key_none", "No key"},
        {"<super>", "windows_drag_key_super", "Super"},
        {"<alt>", "windows_drag_key_alt", "Alt"},
    };
    const QString chosen_key = choices.value(table, QStringLiteral("drag_key")).toString();
    for (const auto& choice : key_choices)
    {
        drag_key->addItem(word(choice.word_key, choice.fallback), QLatin1String(choice.key));
        if (chosen_key == QLatin1String(choice.key))
        {
            drag_key->setCurrentIndex(drag_key->count() - 1);
        }
    }
    connect(drag_key, &QComboBox::activated, this, [this, table, drag_key](int index) {
        this->choices.set(table, QStringLiteral("drag_key"), drag_key->itemData(index).toString());
    });

    // A double click on a title bar ([window] title_double_click): a choice
    // rather than a minimise tick box, as other desktops enlarge the window.
    auto* double_click = new QComboBox;
    const struct
    {
        const char* value;
        const char* word_key;
        const char* fallback;
    } double_click_choices[] = {
        {"none", "windows_double_click_none", "does nothing"},
        {"minimize", "windows_double_click_minimize", "minimises the window"},
        {"maximize", "windows_double_click_maximize", "enlarges the window"},
    };
    const QString chosen_double_click = choices.value(table, QStringLiteral("title_double_click")).toString();
    for (const auto& choice : double_click_choices)
    {
        double_click->addItem(word(choice.word_key, choice.fallback), QLatin1String(choice.value));
        if (chosen_double_click == QLatin1String(choice.value))
        {
            double_click->setCurrentIndex(double_click->count() - 1);
        }
    }
    connect(double_click, &QComboBox::activated, this, [this, table, double_click](int index) {
        this->choices.set(table, QStringLiteral("title_double_click"), double_click->itemData(index).toString());
    });

    // Which side the close, minimise and enlarge buttons are on: for the
    // window frames and for programs that draw their own title bar alike.
    auto* button_side = new QComboBox;
    button_side->addItem(word("windows_buttons_left", "At the left"), left_side);
    button_side->addItem(word("windows_buttons_right", "At the right"), QStringLiteral("right"));
    button_side->setCurrentIndex(
        qMax(0, button_side->findData(choices.value(table, QStringLiteral("button_side")).toString())));
    connect(button_side, &QComboBox::activated, this, [this, table, button_side](int index) {
        const QString side = button_side->itemData(index).toString();
        const QVariantList order = order_for_side(this->choices.value(table, QStringLiteral("button_order")), side);
        for (const QString& toolkit_table : {table, QStringLiteral("gtk")})
        {
            this->choices.set(toolkit_table, QStringLiteral("button_side"), side);
            this->choices.set(toolkit_table, QStringLiteral("button_order"), order);
        }
        QProcess::startDetached(reconfigure_command, {});
    });

    // room for the longest label on one line
    auto* form = new PaneForm(this, 245);
    form->addRow(word("windows_drag_key", "Move and resize a window by holding:"), drag_key);
    form->addRow(word("windows_double_click", "A double click on a title bar:"), double_click);
    form->addRow(word("windows_buttons", "Window buttons:"), button_side);
    form->addDivider();
    form->addCheck(with_note(snap,
        word("windows_snap_note",
            "At the left or right side: that half. In a corner: that quarter. At the top: the whole screen.")));
    form->addRow(word("windows_snap_distance", "Snap when this close:"), snap_row);
    form->addCheck(with_note(zones, word("windows_zone_picker_note",
        "Dropping the window on a part of a layout resizes it, so it fills that part of the screen.")));
    form->addRow(word("windows_resize_reach", "Edge to resize by:"), reach_row);
    form->addDivider();
    form->addCheck(with_note(quit_all, word("windows_quit_closes_app_note",
        "The Close Window shortcut quits the program too, so all of its windows close at once.")));
    form->addCheck(with_note(switcher_windows, word("windows_switcher_windows_note",
        "Press Down on a program in the switcher, so you can pick one of its windows instead of the last one used.")));
    form->addCheck(with_note(borderless, word("windows_borderless_note",
        "Games open full screen on the screen they chose, so borderless ones fill it too.")));
}
