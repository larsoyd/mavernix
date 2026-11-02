#include "dock-pane.hpp"

#include <QCheckBox>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QScreen>
#include <QSignalBlocker>
#include <QSlider>

#include "choices.hpp"
#include "pane-parts.hpp"

DockPane::DockPane(Choices& choices, QWidget* parent) : QWidget(parent), choices(choices)
{
    const QVariantMap words = choices.configTable(QStringLiteral("settings")).value(QStringLiteral("words")).toMap();
    const auto word = [&words](const char* key, const char* fallback) {
        return words.value(QLatin1String(key), QString::fromUtf8(fallback)).toString();
    };

    const QVariantMap dock_looks = choices.themeTable(QStringLiteral("dock"));
    const int smallest = dock_looks.value(QStringLiteral("smallest_icon_size"), 16).toInt();
    const int largest = dock_looks.value(QStringLiteral("largest_icon_size"), 128).toInt();
    // Wide enough to be set finely.
    const int slider_width = 208;

    size = new QSlider(Qt::Horizontal);
    size->setRange(smallest, largest);
    size->setValue(currentIconSize());
    size->setMinimumWidth(slider_width);
    connect(size, &QSlider::valueChanged, this,
        [this](int value) { this->choices.set(QStringLiteral("dock"), QStringLiteral("icon_size"), value); });

    magnification = new QCheckBox(word("dock_magnification", "Enlarge under the pointer:"));
    magnification->setChecked(choices.value(QStringLiteral("dock"), QStringLiteral("magnification")).toBool());

    magnified_size = new QSlider(Qt::Horizontal);
    magnified_size->setRange(smallest, largest);
    const QVariant chosen_largest = choices.value(QStringLiteral("dock"), QStringLiteral("largest_icon_size"));
    magnified_size->setValue(chosen_largest.isValid() ? chosen_largest.toInt() : largest);
    magnified_size->setMinimumWidth(slider_width);
    magnified_size->setEnabled(magnification->isChecked());
    connect(magnified_size, &QSlider::valueChanged, this,
        [this](int value) { this->choices.set(QStringLiteral("dock"), QStringLiteral("largest_icon_size"), value); });
    connect(magnification, &QCheckBox::toggled, this, [this](bool on) {
        magnified_size->setEnabled(on);
        this->choices.set(QStringLiteral("dock"), QStringLiteral("magnification"), on);
    });

    hides = new QCheckBox(word("dock_autohide", "Hide the dock until the pointer is at the screen's edge"));
    hides->setChecked(choices.value(QStringLiteral("dock"), QStringLiteral("autohide")).toBool());
    connect(hides, &QCheckBox::toggled, this,
        [this](bool on) { this->choices.set(QStringLiteral("dock"), QStringLiteral("autohide"), on); });

    // the axis of the original's Dock pane, measured from the window's side
    auto* form = new PaneForm(this, 245);
    form->addRow(word("dock_size", "Size:"),
        with_end_words(size, word("dock_size_small", "Small"), word("dock_size_large", "Large")));
    form->addRow(magnification,
        with_end_words(magnified_size, word("dock_magnification_min", "Min"), word("dock_magnification_max", "Max")));
    form->addCheck(hides);

    auto* click_minimizes =
        new QCheckBox(word("dock_click_minimizes", "Click the front program's icon to minimize its window"));
    click_minimizes->setChecked(choices.value(QStringLiteral("dock"), QStringLiteral("click_minimizes")).toBool());
    connect(click_minimizes, &QCheckBox::toggled, this,
        [this](bool on) { this->choices.set(QStringLiteral("dock"), QStringLiteral("click_minimizes"), on); });
    form->addRow(QString(), click_minimizes);

    // Effects the original didn't have, or had otherwise; all off at first.
    // The compositor follows at once (the shell restarts for them).
    auto* reflections = new QCheckBox(word("dock_window_reflections", "Reflect windows in the Dock's glass"));
    reflections->setChecked(choices.value(QStringLiteral("dock"), QStringLiteral("window_reflections")).toBool());
    connect(reflections, &QCheckBox::toggled, this,
        [this](bool on) { this->choices.set(QStringLiteral("dock"), QStringLiteral("window_reflections"), on); });
    form->addCheck(reflections);

    // How strongly windows show in the glass ([dock] window_reflection_opacity); the shell follows.
    const QVariant chosen_reflection =
        choices.value(QStringLiteral("dock"), QStringLiteral("window_reflection_opacity"));
    auto* reflection_strength = stops_slider({0.15, 0.25, 0.35, 0.45, 0.55, 0.65, 0.75},
        chosen_reflection.isValid() ? chosen_reflection.toDouble() : 0.35, [this](double value) {
        this->choices.set(QStringLiteral("dock"), QStringLiteral("window_reflection_opacity"), value);
    });
    auto* reflection_row = with_end_words(
        reflection_strength, word("dock_reflection_faint", "Faint"), word("dock_reflection_strong", "Strong"));
    reflection_row->setEnabled(reflections->isChecked());
    connect(reflections, &QCheckBox::toggled, reflection_row, &QWidget::setEnabled);
    form->addRow(word("dock_reflection_strength", "Reflections:"), reflection_row);

    auto* dock_blur = new QCheckBox(word("dock_blur", "Blur behind the Dock"));
    dock_blur->setChecked(choices.value(QStringLiteral("blur"), QStringLiteral("dock_shelf")).toBool());
    connect(dock_blur, &QCheckBox::toggled, this,
        [this](bool on) { this->choices.set(QStringLiteral("blur"), QStringLiteral("dock_shelf"), on); });
    form->addCheck(dock_blur);

    // How far both blurs reach ([blur] offset); the theme's until chosen.
    const QVariant chosen_blur = choices.value(QStringLiteral("blur"), QStringLiteral("offset"));
    const double blur_reach =
        chosen_blur.isValid()
            ? chosen_blur.toDouble()
            : choices.themeTable(QStringLiteral("blur")).value(QStringLiteral("offset"), 1.7).toDouble();
    auto* blur_amount = stops_slider({0.5, 1.0, 1.7, 2.5, 3.5, 5.0, 7.0}, blur_reach,
        [this](double value) { this->choices.set(QStringLiteral("blur"), QStringLiteral("offset"), value); });
    auto* blur_row = with_end_words(blur_amount, word("blur_light", "Light"), word("blur_strong", "Strong"));
    // the menu bar's blur box is in the Desktop pane
    const auto show_blur_row = [this, blur_row, dock_blur]() {
        blur_row->setEnabled(
            dock_blur->isChecked() || this->choices.value(QStringLiteral("blur"), QStringLiteral("menu_bar")).toBool());
    };
    show_blur_row();
    connect(dock_blur, &QCheckBox::toggled, blur_row, show_blur_row);
    form->addRow(word("blur_amount", "Blur:"), blur_row);
    // Only where the desktop lets that surface blur ([blur] panel and dock,
    // Sea Lion's); elsewhere the boxes would change nothing.
    const bool panel_can_blur = choices.value(QStringLiteral("blur"), QStringLiteral("panel")).toBool();
    const bool dock_can_blur = choices.value(QStringLiteral("blur"), QStringLiteral("dock")).toBool();
    form->setRowVisible(dock_blur, dock_can_blur);
    form->setRowVisible(blur_row, panel_can_blur || dock_can_blur);

    // How long a notification's banner stays ([notifications] banner_seconds); the shell follows.
    const QVariant chosen_seconds = choices.value(QStringLiteral("notifications"), QStringLiteral("banner_seconds"));
    auto* banner_time = stops_slider(
        {2, 3, 4, 5, 7, 10, 15, 20}, chosen_seconds.isValid() ? chosen_seconds.toDouble() : 5, [this](double value) {
        this->choices.set(QStringLiteral("notifications"), QStringLiteral("banner_seconds"), qRound(value));
    });
    form->addRow(word("banner_time", "Notification banners stay:"),
        with_end_words(banner_time, word("banner_time_short", "Briefly"), word("banner_time_long", "Long")));

    // The folder is watched too, for a choices file replaced whole or not
    // there yet (as in the Extensions pane).
    const QString choices_file = choices.userChoicesFile();
    watcher = new QFileSystemWatcher(this);
    watcher->addPath(QFileInfo(choices_file).absolutePath());
    auto follow = [this, choices_file]() {
        if (QFileInfo::exists(choices_file) && !watcher->files().contains(choices_file))
        {
            watcher->addPath(choices_file);
        }
        showCurrent();
    };
    follow();
    connect(watcher, &QFileSystemWatcher::fileChanged, this, follow);
    connect(watcher, &QFileSystemWatcher::directoryChanged, this, follow);
}

void DockPane::showCurrent()
{
    // Shown without being written back.
    const QSignalBlocker size_blocker(size);
    const QSignalBlocker magnification_blocker(magnification);
    const QSignalBlocker magnified_size_blocker(magnified_size);
    const QSignalBlocker hides_blocker(hides);

    size->setValue(currentIconSize());
    magnification->setChecked(choices.current(QStringLiteral("dock"), QStringLiteral("magnification")).toBool());
    magnified_size->setEnabled(magnification->isChecked());
    const QVariant chosen_largest = choices.current(QStringLiteral("dock"), QStringLiteral("largest_icon_size"));
    if (chosen_largest.isValid())
    {
        magnified_size->setValue(chosen_largest.toInt());
    }
    hides->setChecked(choices.current(QStringLiteral("dock"), QStringLiteral("autohide")).toBool());
}

int DockPane::currentIconSize() const
{
    const QVariant chosen = choices.current(QStringLiteral("dock"), QStringLiteral("icon_size"));
    if (chosen.isValid())
    {
        return chosen.toInt();
    }

    // The theme's size, or its smaller one on a low screen: the dock's own
    // rule (src/shell/DockLayout.js, iconSize).
    const QVariantMap looks = choices.themeTable(QStringLiteral("dock"));
    const int screen_height = QGuiApplication::primaryScreen()->size().height();
    if (looks.contains(QStringLiteral("compact_icon_size")) &&
        screen_height <= looks.value(QStringLiteral("compact_up_to_height")).toInt())
    {
        return looks.value(QStringLiteral("compact_icon_size")).toInt();
    }

    return looks.value(QStringLiteral("icon_size")).toInt();
}
