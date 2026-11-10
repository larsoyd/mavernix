#include "mouse-pane.hpp"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QProcess>
#include <QRadioButton>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>
#include <iterator>

#include "choices.hpp"
#include "pane-parts.hpp"

// Rewrites the compositor's settings from the config and the user's choices
// (on the path from src/session/start-desktop); the compositor follows at once.
static const QString reconfigure_command = QStringLiteral("mavernix-reconfigure");

// The compositor's pointer speed runs from -1 to 1; the slider has this many
// stops to each side of the middle.
static const int speed_steps = 5;
// The wheel's speed is a factor; 1 is unchanged.
static const double scroll_stops[] = {0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 2.5, 3.0};
static const int scroll_stop_count = int(std::size(scroll_stops));
static const int slider_width_px = 200;
// Each write rewrites the compositor's config, so holding an arrow key on
// a slider writes once it is let go.
static const int write_delay_ms = 500;
// the rests KWin offers before the pointer hides, in seconds, 0 for never
static const QList<int> pointer_rests = {0, 1, 5, 10, 15, 30, 60};
// the sizes the pointer themes draw, so none is scaled
static const QList<double> pointer_sizes = {24, 30, 36, 48, 60, 72, 96};

static int nearest_scroll_stop(double factor)
{
    int nearest = 0;
    for (int stop = 1; stop < scroll_stop_count; stop++)
    {
        if (std::abs(scroll_stops[stop] - factor) < std::abs(scroll_stops[nearest] - factor))
        {
            nearest = stop;
        }
    }
    return nearest;
}

MousePane::MousePane(Choices& choices, QWidget* parent) : QWidget(parent), choices(choices)
{
    const QVariantMap words = choices.configTable(QStringLiteral("settings")).value(QStringLiteral("words")).toMap();
    const auto word = [&words](const char* key, const char* fallback) {
        return words.value(QLatin1String(key), QString::fromUtf8(fallback)).toString();
    };
    slow_word = word("mouse_speed_slow", "Slow");
    fast_word = word("mouse_speed_fast", "Fast");
    const QString table = QStringLiteral("input");

    write_later = new QTimer(this);
    write_later->setSingleShot(true);
    write_later->setInterval(write_delay_ms);
    connect(write_later, &QTimer::timeout, this, &MousePane::writeSliders);

    auto* natural = new QCheckBox(word("mouse_natural_scroll", "Natural scrolling"));
    natural->setChecked(choices.value(table, QStringLiteral("mouse_natural_scroll")).toBool());
    connect(
        natural, &QCheckBox::toggled, this, [this](bool on) { change(QStringLiteral("mouse_natural_scroll"), on); });

    const QVariant chosen_scroll = choices.value(table, QStringLiteral("mouse_scroll_speed"));
    auto* speed_row = speedSlider(speed, 2 * speed_steps + 1,
        speed_steps + qRound(choices.value(table, QStringLiteral("mouse_speed")).toDouble() * speed_steps));
    auto* scrolling_row = speedSlider(
        scrolling, scroll_stop_count, nearest_scroll_stop(chosen_scroll.isValid() ? chosen_scroll.toDouble() : 1.0));

    // Left out, the device's own profile applies, which for a mouse is
    // acceleration.
    const QVariant accelerated = choices.value(table, QStringLiteral("mouse_acceleration"));
    auto* acceleration = new QCheckBox(word("mouse_acceleration", "Acceleration"));
    acceleration->setChecked(!accelerated.isValid() || accelerated.toBool());
    connect(
        acceleration, &QCheckBox::toggled, this, [this](bool on) { change(QStringLiteral("mouse_acceleration"), on); });

    // Pasting what was last selected with the middle button. The compositor
    // reads it only when it starts.
    const QVariant pastes = choices.value(table, QStringLiteral("middle_click_paste"));
    auto* middle_paste =
        new QCheckBox(word("mouse_middle_click_paste", "Paste the selected text with the middle button"));
    middle_paste->setChecked(!pastes.isValid() || pastes.toBool());
    connect(
        middle_paste, &QCheckBox::toggled, this, [this](bool on) { change(QStringLiteral("middle_click_paste"), on); });

    // As on El Capitan; off unless asked for.
    auto* shake = new QCheckBox(word("mouse_shake_to_find", "Shake the pointer to find it"));
    shake->setChecked(choices.value(table, QStringLiteral("shake_to_find")).toBool());
    connect(shake, &QCheckBox::toggled, this, [this](bool on) { change(QStringLiteral("shake_to_find"), on); });
    // How hard a shake must be: 1, a little, to 5, a lot; 3 until chosen.
    const QVariant chosen_shake = choices.value(table, QStringLiteral("shake_sensitivity"));
    auto* shake_needed = stops_slider({1, 2, 3, 4, 5}, chosen_shake.isValid() ? chosen_shake.toDouble() : 3,
        [this](double value) { change(QStringLiteral("shake_sensitivity"), qRound(value)); });
    auto* shake_row =
        with_end_words(shake_needed, word("mouse_shake_little", "Little"), word("mouse_shake_lots", "Lots"));
    shake_row->setEnabled(shake->isChecked());
    connect(shake, &QCheckBox::toggled, shake_row, &QWidget::setEnabled);

    // as KWin's Hide Cursor effect has them, both off unless asked for
    auto* hide_typing = new QCheckBox(word("mouse_hide_pointer_typing", "Hide the pointer while typing"));
    hide_typing->setChecked(choices.value(table, QStringLiteral("hide_pointer_when_typing")).toBool());
    connect(hide_typing, &QCheckBox::toggled, this,
        [this](bool on) { change(QStringLiteral("hide_pointer_when_typing"), on); });
    const int chosen_rest = qMax(0, choices.value(table, QStringLiteral("hide_pointer_after_seconds")).toInt());
    auto* hide_after = new QComboBox;
    for (int seconds : pointer_rests.contains(chosen_rest) ? pointer_rests : pointer_rests + QList<int>{chosen_rest})
    {
        hide_after->addItem(seconds == 0   ? word("mouse_hide_pointer_never", "Never")
                            : seconds == 1 ? word("mouse_hide_pointer_second", "1 second")
                                           : word("mouse_hide_pointer_seconds", "%1 seconds").arg(seconds),
            seconds);
    }
    hide_after->setCurrentIndex(hide_after->findData(chosen_rest));
    connect(hide_after, &QComboBox::activated, this, [this, hide_after](int index) {
        change(QStringLiteral("hide_pointer_after_seconds"), hide_after->itemData(index));
    });

    // kept with the user's choices, so a new import of the theme does not undo it
    const QVariant chosen_pointer = choices.value(QStringLiteral("pointer"), QStringLiteral("size"));
    const QVariant theme_pointer = choices.themeTable(QStringLiteral("cursors")).value(QStringLiteral("size"));
    auto* pointer_size = stops_slider(pointer_sizes,
        chosen_pointer.isValid() ? chosen_pointer.toDouble() : theme_pointer.isValid() ? theme_pointer.toDouble() : 24,
        [this](double size) {
            this->choices.set(QStringLiteral("pointer"), QStringLiteral("size"), qRound(size));
            QProcess::startDetached(reconfigure_command, {});
        });
    auto* pointer_row = with_end_words(
        pointer_size, word("mouse_pointer_normal", "Normal"), word("mouse_pointer_large", "Large"));

    auto* left = new QRadioButton(word("mouse_primary_left", "Left"));
    auto* right = new QRadioButton(word("mouse_primary_right", "Right"));
    auto* buttons = new QButtonGroup(this);
    buttons->addButton(left);
    buttons->addButton(right);
    (choices.value(table, QStringLiteral("left_handed")).toBool() ? right : left)->setChecked(true);
    connect(right, &QRadioButton::toggled, this, [this](bool on) { change(QStringLiteral("left_handed"), on); });
    keep_to_pitch(left);
    keep_to_pitch(right);
    auto* primary = new QWidget;
    auto* primary_rows = new QVBoxLayout(primary);
    primary_rows->setContentsMargins(0, 0, 0, 0);
    primary_rows->setSpacing(0);
    primary_rows->addWidget(left);
    primary_rows->addWidget(right);

    // the scroll direction comes first, as on the original
    auto* form = new PaneForm(this);
    form->addCheck(with_note(natural, word("mouse_natural_scroll_note", "")));
    form->addDivider();
    form->addRow(word("mouse_speed", "Tracking speed:"), speed_row);
    form->addRow(word("mouse_scroll_speed", "Scrolling speed:"), scrolling_row);
    form->addCheck(with_note(acceleration, word("mouse_acceleration_note", "")));
    form->addDivider();
    form->addRow(word("mouse_primary", "Primary mouse button:"), primary);
    form->addDivider();
    form->addCheck(with_note(middle_paste, word("mouse_middle_click_paste_note",
                                               "A middle click pastes the last selected text, after you log in again.")));
    form->addCheck(with_note(shake, word("mouse_shake_to_find_note",
                                        "Shaking the mouse quickly makes the pointer grow for a moment, so you can spot it.")));
    form->addRow(word("mouse_shake_needed", "Shake needed:"), shake_row);
    form->addCheck(with_note(hide_typing, word("mouse_hide_pointer_typing_note",
                                              "It comes back when you move the mouse.")));
    form->addRow(word("mouse_hide_pointer_after", "Hide the pointer after:"), hide_after);
    form->addDivider();
    form->addRow(word("mouse_pointer_size", "Pointer size:"),
        with_note_under(pointer_row,
            word("mouse_pointer_size_note", "Programs opened before keep the old size until they start again."), 0));
}

QWidget* MousePane::speedSlider(QSlider*& slider, int stops, int at)
{
    slider = new QSlider(Qt::Horizontal);
    slider->setRange(0, stops - 1);
    slider->setPageStep(1);
    slider->setTickPosition(QSlider::TicksBelow);
    slider->setTickInterval(1);
    slider->setFixedWidth(slider_width_px);
    slider->setValue(at);
    // Dragged: written on release. Moved with the keys: once they rest.
    QSlider* const moved = slider;
    connect(slider, &QSlider::valueChanged, this, [this, moved]() {
        if (!moved->isSliderDown())
        {
            write_later->start();
        }
    });
    connect(slider, &QSlider::sliderReleased, this, &MousePane::writeSliders);
    return with_end_words(slider, slow_word, fast_word);
}

void MousePane::writeSliders()
{
    write_later->stop();
    choices.set(
        QStringLiteral("input"), QStringLiteral("mouse_speed"), double(speed->value() - speed_steps) / speed_steps);
    change(QStringLiteral("mouse_scroll_speed"), scroll_stops[scrolling->value()]);
}

void MousePane::change(const QString& key, const QVariant& value)
{
    choices.set(QStringLiteral("input"), key, value);
    QProcess::startDetached(reconfigure_command, {});
}
