#include "startup-pane.hpp"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "choices.hpp"
#include "pane-parts.hpp"
#include "program-picker.hpp"
#include "startup-entries.hpp"

// Short enough that a problem and the note under the buttons fit too.
static const int list_height_px = 200;
static const int icon_size_px = 24;

// The item's file name.
static const int FileNameRole = Qt::UserRole;
static const int UsersOwnRole = Qt::UserRole + 1;

// left and right move between an item's own tick and its Hide tick
class StartupList : public QTreeWidget
{
  protected:
    QModelIndex moveCursor(CursorAction action, Qt::KeyboardModifiers modifiers) override
    {
        const QModelIndex now = currentIndex();
        if (now.isValid() && (action == MoveLeft || action == MoveRight))
        {
            return now.siblingAtColumn(action == MoveRight ? 1 : 0);
        }
        return QTreeWidget::moveCursor(action, modifiers);
    }
};

StartupPane::StartupPane(Choices& choices, QWidget* parent) : QWidget(parent)
{
    words = choices.configTable(QStringLiteral("settings")).value(QStringLiteral("words")).toMap();
    auto* rows = new QVBoxLayout(this);
    // Its parts are as wide as the pane may be; a margin would push them out of the window.
    rows->setContentsMargins(0, 0, 0, 0);
    rows->setSpacing(pane_spacing_px);

    auto* intro = new QLabel(word(
        "startup_intro", "These programs open by themselves when you log in. Untick one to keep it from opening."));
    intro->setWordWrap(true);
    intro->setFixedWidth(intro_width_px);
    rows->addWidget(intro);

    list = new StartupList;
    list->setFixedSize(intro_width_px, list_height_px);
    list->setIconSize(QSize(icon_size_px, icon_size_px));
    list->setColumnCount(2);
    list->setHeaderLabels({word("startup_item", "Item"), word("startup_hide", "Hide")});
    list->setRootIsDecorated(false);
    list->setUniformRowHeights(true);
    list->setTextElideMode(Qt::ElideRight);
    list->header()->setStretchLastSection(false);
    list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    list->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    list->header()->setSectionsMovable(false);
    rows->addWidget(list);

    auto* hint = new QLabel(word("startup_hide_hint", "To hide a program when you log in, tick Hide next to it."));
    hint->setWordWrap(true);
    hint->setFixedWidth(intro_width_px);
    rows->addWidget(hint);

    // In the empty list, instead of nothing.
    empty_label = new QLabel(word("startup_none", "No programs open at login."));
    empty_label->setAlignment(Qt::AlignCenter);
    empty_label->setEnabled(false);
    auto* inside_list = new QVBoxLayout(list->viewport());
    inside_list->addWidget(empty_label);

    auto* add = new QPushButton(word("startup_add", "Add…"));
    remove = new QPushButton(word("startup_remove", "Remove"));
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addStretch();
    rows->addLayout(buttons);

    problem_label = new QLabel;
    problem_label->setWordWrap(true);
    problem_label->setFixedWidth(intro_width_px);
    problem_label->hide();
    rows->addWidget(problem_label);

    // Said when nothing in the list would start.
    QString note;
    const QVariant users_autostart = choices.value(QStringLiteral("session"), QStringLiteral("users_autostart"));
    const bool switched_off = users_autostart.isValid() && !users_autostart.toBool();
    if (switched_off)
    {
        note = word("startup_switched_off", "Starting these programs at login is turned off.");
    }
    else if (QStandardPaths::findExecutable(QStringLiteral("dex")).isEmpty()
             // Fedora's name for it.
             && QStandardPaths::findExecutable(QStringLiteral("dex-autostart")).isEmpty())
    {
        note = word("startup_no_dex",
            "These programs don't open yet: the helper program “dex” is missing. Install the package dex, "
            "and they open from the next login.");
    }
    if (!note.isEmpty())
    {
        // As wide as the list above it; with its width fixed, a wrapping
        // label asks for the height it fills.
        auto* note_label = new QLabel(note);
        note_label->setWordWrap(true);
        note_label->setFixedWidth(intro_width_px);
        rows->addWidget(note_label);
    }
    // Turned off in the config: a button turns it back on, rather than a file to edit.
    if (switched_off)
    {
        auto* turn_on = new QPushButton(word("startup_turn_on", "Open Them at Login"));
        connect(turn_on, &QPushButton::clicked, this, [&choices, turn_on]() {
            choices.set(QStringLiteral("session"), QStringLiteral("users_autostart"), true);
            turn_on->setEnabled(false);
        });
        rows->addWidget(turn_on, 0, Qt::AlignLeft);
    }

    connect(list, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
        const QString file_name = item->data(0, FileNameRole).toString();
        const bool ticked = item->checkState(column) == Qt::Checked;
        showProblem(column == 0 ? switch_startup_entry(file_name, ticked) : set_startup_entry_hide(file_name, ticked));
        // The box shows what the folders now say, also when the change
        // failed; read after this signal, as reading clears the list.
        QTimer::singleShot(0, this, [this, file_name]() { fill(file_name); });
    });
    connect(list, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* current) {
        // One only the system has is switched off instead.
        remove->setEnabled(current != nullptr && current->data(0, UsersOwnRole).toBool());
    });
    connect(add, &QPushButton::clicked, this, &StartupPane::addProgram);
    connect(remove, &QPushButton::clicked, this, [this]() {
        const QTreeWidgetItem* current = list->currentItem();
        if (current != nullptr)
        {
            showProblem(remove_startup_entry(current->data(0, FileNameRole).toString()));
            fill();
        }
    });

    fill();
}

void StartupPane::fill(const QString& chosen)
{
    const QSignalBlocker quiet(list);
    list->clear();
    for (const StartupEntry& entry : startup_entries())
    {
        auto* item = new QTreeWidgetItem(list, {entry.name});
        item->setIcon(0, entry.icon);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, entry.on ? Qt::Checked : Qt::Unchecked);
        item->setCheckState(1, entry.hide ? Qt::Checked : Qt::Unchecked);
        item->setData(0, FileNameRole, entry.file_name);
        item->setData(0, UsersOwnRole, entry.users_own);
        if (entry.file_name == chosen)
        {
            list->setCurrentItem(item);
        }
    }
    empty_label->setVisible(list->topLevelItemCount() == 0);
    const QTreeWidgetItem* current = list->currentItem();
    remove->setEnabled(current != nullptr && current->data(0, UsersOwnRole).toBool());
}

void StartupPane::addProgram()
{
    const QString id =
        choose_program(this, word("startup_add_heading", "Open at Login"), word("startup_search", "Search"));
    if (id.isEmpty())
    {
        return;
    }
    showProblem(add_startup_program(id));
    fill(id);
}

QString StartupPane::word(const char* key, const char* fallback) const
{
    return words.value(QLatin1String(key), QString::fromUtf8(fallback)).toString();
}

void StartupPane::showProblem(const QString& problem)
{
    problem_label->setText(problem);
    problem_label->setVisible(!problem.isEmpty());
}
