#include "program-picker.hpp"

#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

#include "choices.hpp"
#include "installed-programs.hpp"

static const int icon_size_px = 24;
static const int chooser_width_px = 360;
static const int chooser_height_px = 420;
static const int IdRole = Qt::UserRole;

// arrow keys typed in the search move through the list under it, so the keys alone pick a program
class KeysToList : public QObject
{
  public:
    KeysToList(QListWidget* list, QObject* parent) : QObject(parent), list(list) {}

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::KeyPress)
        {
            const int key = static_cast<QKeyEvent*>(event)->key();
            if (key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_PageUp || key == Qt::Key_PageDown)
            {
                QCoreApplication::sendEvent(list, event);
                return true;
            }
        }
        return QObject::eventFilter(watched, event);
    }

  private:
    QListWidget* list;
};

QString choose_program(QWidget* parent, const QString& heading, const QString& search_word)
{
    QDialog chooser(parent);
    chooser.setWindowTitle(heading);
    chooser.resize(chooser_width_px, chooser_height_px);

    auto* search = new QLineEdit;
    search->setPlaceholderText(search_word);
    search->setClearButtonEnabled(true);
    auto* programs = new QListWidget;
    programs->setIconSize(QSize(icon_size_px, icon_size_px));
    for (const InstalledProgram& program : installed_programs())
    {
        auto* item = new QListWidgetItem(program.icon, program.name, programs);
        item->setData(IdRole, program.id);
    }
    auto* answer = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    answer->button(QDialogButtonBox::Ok)->setEnabled(false);

    auto* rows = new QVBoxLayout(&chooser);
    rows->addWidget(search);
    rows->addWidget(programs);
    rows->addWidget(answer);

    search->installEventFilter(new KeysToList(programs, &chooser));
    // the first program found is chosen so Return takes it, and one the search hides never is
    QObject::connect(search, &QLineEdit::textChanged, &chooser, [programs](const QString& text) {
        QListWidgetItem* first_found = nullptr;
        for (int row = 0; row < programs->count(); row++)
        {
            QListWidgetItem* item = programs->item(row);
            item->setHidden(!item->text().contains(text, Qt::CaseInsensitive));
            if (first_found == nullptr && !item->isHidden())
            {
                first_found = item;
            }
        }
        if (programs->currentItem() == nullptr || programs->currentItem()->isHidden())
        {
            programs->setCurrentItem(first_found);
        }
    });
    QObject::connect(programs, &QListWidget::currentItemChanged, &chooser,
        [answer](QListWidgetItem* current) { answer->button(QDialogButtonBox::Ok)->setEnabled(current != nullptr); });
    QObject::connect(programs, &QListWidget::itemActivated, &chooser, &QDialog::accept);
    QObject::connect(answer, &QDialogButtonBox::accepted, &chooser, &QDialog::accept);
    QObject::connect(answer, &QDialogButtonBox::rejected, &chooser, &QDialog::reject);

    if (chooser.exec() != QDialog::Accepted || programs->currentItem() == nullptr)
    {
        return {};
    }
    return programs->currentItem()->data(IdRole).toString();
}

ProgramList::ProgramList(Choices& choices, const QString& table, const QString& key, picker_t pick, QWidget* parent) :
    QWidget(parent), choices(choices), table(table), key(key), pick(std::move(pick))
{
    const QVariantMap words = choices.configTable(QStringLiteral("settings")).value(QStringLiteral("words")).toMap();
    const auto word = [&words](const char* name, const char* fallback) {
        return words.value(QLatin1String(name), QString::fromUtf8(fallback)).toString();
    };
    list = new QListWidget;
    list->setIconSize(QSize(icon_size_px, icon_size_px));
    auto* add = new QPushButton(word("programs_add", "Add…"));
    add->setObjectName(QStringLiteral("add"));
    remove = new QPushButton(word("programs_remove", "Remove"));
    remove->setObjectName(QStringLiteral("remove"));

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addStretch();
    auto* rows = new QVBoxLayout(this);
    rows->setContentsMargins(0, 0, 0, 0);
    rows->addWidget(list);
    rows->addLayout(buttons);

    connect(add, &QPushButton::clicked, this, [this] {
        const QString id = this->pick(this);
        QStringList ids = kept();
        if (!id.isEmpty() && !ids.contains(id))
        {
            keep(ids << id);
        }
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        if (QListWidgetItem* current = list->currentItem())
        {
            QStringList ids = kept();
            ids.removeAll(current->data(IdRole).toString());
            keep(ids);
        }
    });
    connect(list, &QListWidget::currentItemChanged, this,
        [this](QListWidgetItem* current) { remove->setEnabled(current != nullptr); });
    fill();
}

QStringList ProgramList::kept() const
{
    return choices.value(table, key).toStringList();
}

void ProgramList::keep(const QStringList& ids)
{
    choices.set(table, key, ids);
    fill();
}

// a program no longer installed shows by its entry's name, so it can still be taken off
void ProgramList::fill()
{
    const QList<InstalledProgram> installed = installed_programs();
    list->clear();
    for (const QString& id : kept())
    {
        const auto found = std::find_if(installed.begin(), installed.end(),
            [&id](const InstalledProgram& program) { return program.id == id; });
        const QString bare = id.endsWith(QLatin1String(".desktop")) ? id.chopped(8) : id;
        auto* item = found != installed.end() ? new QListWidgetItem(found->icon, found->name, list)
                                              : new QListWidgetItem(bare, list);
        item->setData(IdRole, id);
    }
    if (list->count() > 0)
    {
        list->setCurrentRow(0);
    }
    remove->setEnabled(list->currentItem() != nullptr);
}
