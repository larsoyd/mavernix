#include "clipboard-pane.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>

#include "choices.hpp"
#include "menu-bar-box.hpp"
#include "pane-parts.hpp"

static const QString clipboard_table = QStringLiteral("clipboard");
static const QString dock_table = QStringLiteral("dock");

static bool is_clipboard_tile(const QVariant& place)
{
    return place.typeId() == QMetaType::QVariantMap && place.toMap().value(QStringLiteral("clipboard")).toBool();
}

// a popup of whole numbers, the current one added when it is not among them
static QComboBox* number_popup(const QList<int>& numbers, int current, const QString& words)
{
    QList<int> offered = numbers;
    if (!offered.contains(current))
    {
        offered << current;
        std::sort(offered.begin(), offered.end());
    }
    auto* popup = new QComboBox;
    for (int number : offered)
    {
        popup->addItem(words.arg(number), number);
    }
    popup->setCurrentIndex(popup->findData(current));
    return popup;
}

ClipboardPane::ClipboardPane(Choices& choices, QWidget* parent) : QWidget(parent), choices(choices)
{
    const QVariantMap words = choices.configTable(QStringLiteral("settings")).value(QStringLiteral("words")).toMap();
    const auto word = [&words](const char* key, const char* fallback) {
        return words.value(QLatin1String(key), QString::fromUtf8(fallback)).toString();
    };

    QComboBox* size = number_popup({25, 50, 100, 200, 500}, chosen(QStringLiteral("keep_items")).toInt(),
        word("clipboard_items", "%1 items"));
    connect(size, &QComboBox::activated, this,
        [this, size]() { this->choices.set(clipboard_table, QStringLiteral("keep_items"), size->currentData()); });

    auto* remember = new QCheckBox(word("clipboard_remember", "Keep the history after logging out"));
    remember->setChecked(chosen(QStringLiteral("remember")).toBool());
    connect(remember, &QCheckBox::toggled, this,
        [this](bool on) { this->choices.set(clipboard_table, QStringLiteral("remember"), on); });

    auto* pictures = new QCheckBox(word("clipboard_pictures", "Keep copied pictures"));
    pictures->setChecked(chosen(QStringLiteral("keep_images")).toBool());
    QComboBox* largest = number_popup({5, 10, 20, 50, 100}, chosen(QStringLiteral("image_limit_mb")).toInt(),
        word("clipboard_megabytes", "%1 MB"));
    largest->setEnabled(pictures->isChecked());
    connect(pictures, &QCheckBox::toggled, this, [this, largest](bool on) {
        largest->setEnabled(on);
        this->choices.set(clipboard_table, QStringLiteral("keep_images"), on);
    });
    connect(largest, &QComboBox::activated, this, [this, largest]() {
        this->choices.set(clipboard_table, QStringLiteral("image_limit_mb"), largest->currentData());
    });

    auto* inDock = new QCheckBox(word("clipboard_in_dock", "Show clipboard history in the Dock"));
    inDock->setChecked(dockHolds());
    connect(inDock, &QCheckBox::toggled, this, &ClipboardPane::showInDock);

    auto* form = new PaneForm(this);
    form->addRow(word("clipboard_keep", "Remember up to:"),
        with_note_under(size, word("clipboard_keep_note", "Pinned items are kept as well and never count."), 0));
    form->addCheck(remember);
    form->addDivider();
    form->addCheck(pictures);
    form->addRow(word("clipboard_largest", "Largest item:"),
        with_note_under(largest, word("clipboard_largest_note", "A bigger copy is pasted as usual but not kept."), 0));
    form->addDivider();
    if (QCheckBox* menuBar = menu_bar_box(choices, QStringLiteral("clipboard"),
            word("clipboard_in_menu_bar", "Show clipboard history in menu bar")))
    {
        form->addCheck(menuBar);
    }
    form->addCheck(with_note(inDock,
        word("clipboard_shortcut_note", "Super+V opens it. Passwords from password managers are never kept.")));
}

QVariant ClipboardPane::chosen(const QString& key) const
{
    const QVariant own = choices.value(clipboard_table, key);
    if (own.isValid())
    {
        return own;
    }
    static const QVariantMap fallback{{QStringLiteral("keep_items"), 100}, {QStringLiteral("remember"), true},
        {QStringLiteral("keep_images"), true}, {QStringLiteral("image_limit_mb"), 20}};
    const QVariantMap defaults =
        choices.configTable(QStringLiteral("status")).value(clipboard_table).toMap();
    return defaults.value(key, fallback.value(key));
}

bool ClipboardPane::dockHolds() const
{
    for (const QVariant& group : choices.value(dock_table, QStringLiteral("groups")).toList())
    {
        const QVariantList places = group.toList();
        if (std::any_of(places.cbegin(), places.cend(), is_clipboard_tile))
        {
            return true;
        }
    }
    return false;
}

void ClipboardPane::showInDock(bool on)
{
    QVariantList groups = choices.current(dock_table, QStringLiteral("groups")).toList();
    if (groups.isEmpty())
    {
        groups = choices.value(dock_table, QStringLiteral("groups")).toList();
    }
    for (QVariant& group : groups)
    {
        QVariantList places = group.toList();
        places.removeIf(is_clipboard_tile);
        group = places;
    }
    if (on)
    {
        if (groups.isEmpty())
        {
            groups << QVariant(QVariantList());
        }
        QVariantList first = groups[0].toList();
        const QVariantMap tile{{QStringLiteral("name"), QStringLiteral("Clipboard")},
            {QStringLiteral("icon"), QStringLiteral("mavernix-clipboard")}, {QStringLiteral("clipboard"), true}};
        first.insert(std::min<qsizetype>(1, first.size()), tile);
        groups[0] = first;
    }
    choices.set(dock_table, QStringLiteral("groups"), groups);
}
