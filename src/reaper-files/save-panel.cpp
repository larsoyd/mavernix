#include "save-panel.h"

#include <KFileCustomDialog>
#include <KConfigGroup>
#include <KFilePlacesView>
#include <KFileWidget>
#include <KSharedConfig>
#include <KUrlComboBox>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScreen>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>

namespace
{
// a text wider than two thirds of the screen is cut, the whole of it stays in the tooltip
QString fitted(QWidget* widget, const QString& text)
{
    const QScreen* screen = QGuiApplication::primaryScreen();
    const int widest = screen ? screen->availableGeometry().width() * 2 / 3 : 640;
    const QString cut = widget->fontMetrics().elidedText(text, Qt::ElideRight, widest);
    widget->setToolTip(cut == text ? QString() : QString(text).remove(QLatin1Char('&')));
    return cut;
}

// swell marks a mnemonic with a single ampersand, a label without a buddy would show it
QString plain(const QString& text)
{
    QString out;
    for (int at = 0; at < text.size(); at++)
    {
        if (text[at] == QLatin1Char('&') && at + 1 < text.size())
            at++;
        out += text[at];
    }
    return out;
}
} // namespace

SavePanel::SavePanel(QObject* parent) : QObject(parent) {}

SavePanel::~SavePanel()
{
    delete window;
}

QString SavePanel::escaped(const QString& raw)
{
    QString out;
    for (const QChar c : raw)
        out += c == QLatin1Char('\\') ? QStringLiteral("\\\\")
               : c == QLatin1Char('\t') ? QStringLiteral("\\t")
               : c == QLatin1Char('\n') ? QStringLiteral("\\n")
               : c == QLatin1Char('\r') ? QStringLiteral("\\r")
                                        : QString(c);
    return out;
}

QStringList SavePanel::fields(const QString& line)
{
    QStringList out{QString()};
    for (int at = 0; at < line.size(); at++)
        if (line[at] == QLatin1Char('\t'))
            out.append(QString());
        else if (line[at] == QLatin1Char('\\') && at + 1 < line.size())
        {
            const QChar next = line[++at];
            out.last() += next == QLatin1Char('t') ? QChar('\t') : next == QLatin1Char('n') ? QChar('\n')
                          : next == QLatin1Char('r') ? QChar('\r') : next;
        }
        else
            out.last() += line[at];
    return out;
}

bool SavePanel::feed(const QString& line)
{
    const QStringList field = fields(line);
    const QString& verb = field[0];
    bool ok = true;
    if (window)
    {
        if (verb == QLatin1String("busy") && field.size() == 1)
            window->setEnabled(false);
        else if (verb == QLatin1String("ready") && field.size() == 1)
            window->setEnabled(true);
        else if (verb == QLatin1String("state") && field.size() == 5 && controls.contains(field[1]))
        {
            Control& control = controls[field[1]];
            control.enabled = field[2] == QLatin1String("1");
            control.value = field[3].toInt(&ok);
            control.text = field[4];
            apply(control);
        }
        else
            ok = false;
        return ok;
    }
    if (verb == QLatin1String("title") && field.size() == 2)
        title = field[1];
    else if (verb == QLatin1String("folder") && field.size() == 2)
        folder = field[1];
    else if (verb == QLatin1String("name") && field.size() == 2)
        name = field[1];
    else if (verb == QLatin1String("filter") && field.size() == 3)
        filters.append(KFileFilter(field[1], field[2].split(QLatin1Char(';'), Qt::SkipEmptyParts), {}));
    else if (verb == QLatin1String("current") && field.size() == 2)
        current = field[1].toInt(&ok);
    else if (verb == QLatin1String("control") && field.size() == 10 && !controls.contains(field[1]))
    {
        Control control;
        control.kind = field[2];
        int number[6];
        for (int at = 0; at < 6 && ok; at++)
            number[at] = field[3 + at].toInt(&ok);
        control.rect = QRect(number[0], number[1], number[2], number[3]);
        control.enabled = number[4] == 1;
        control.value = number[5];
        control.text = field[9];
        ok = ok && QStringList{"check", "button", "edit", "label", "list"}.contains(control.kind);
        if (ok)
        {
            controls.insert(field[1], control);
            order.append(field[1]);
        }
    }
    else if (verb == QLatin1String("item") && field.size() == 3 && controls.contains(field[1]) &&
             controls[field[1]].kind == QLatin1String("list"))
        controls[field[1]].items.append(field[2]);
    else if (verb == QLatin1String("show") && field.size() == 1)
        show();
    else
        ok = false;
    return ok;
}

QWidget* SavePanel::build(const QString& id, Control& control)
{
    QWidget* widget = nullptr;
    if (control.kind == QLatin1String("check"))
    {
        auto box = new QCheckBox;
        connect(box, &QCheckBox::clicked, this,
            [this, id](bool on) { Q_EMIT send(QStringLiteral("check\t%1\t%2").arg(id).arg(on ? 1 : 0)); });
        widget = box;
    }
    else if (control.kind == QLatin1String("button"))
    {
        auto button = new QPushButton;
        connect(button, &QPushButton::clicked, this, [this, id] {
            window->setEnabled(false);
            Q_EMIT send(QStringLiteral("click\t") + id);
        });
        widget = button;
    }
    else if (control.kind == QLatin1String("edit"))
    {
        auto edit = new QLineEdit;
        edit->setFixedWidth(std::max(control.rect.width(), edit->fontMetrics().horizontalAdvance(QStringLiteral("0000000"))));
        connect(edit, &QLineEdit::textEdited, this,
            [this, id](const QString& text) { Q_EMIT send(QStringLiteral("text\t%1\t%2").arg(id, escaped(text))); });
        widget = edit;
    }
    else if (control.kind == QLatin1String("list"))
    {
        auto list = new QComboBox;
        list->addItems(control.items);
        connect(list, &QComboBox::activated, this,
            [this, id](int index) { Q_EMIT send(QStringLiteral("select\t%1\t%2").arg(id).arg(index)); });
        widget = list;
    }
    else
        widget = new QLabel;
    widget->setObjectName(id);
    control.widget = widget;
    apply(control);
    return widget;
}

// the panel listens only to what the user does, so setting values here sends nothing back
void SavePanel::apply(Control& control)
{
    QWidget* widget = control.widget;
    if (!widget)
        return;
    widget->setEnabled(control.enabled);
    if (auto box = qobject_cast<QCheckBox*>(widget))
    {
        box->setText(fitted(box, control.text));
        box->setChecked(control.value);
    }
    else if (auto button = qobject_cast<QPushButton*>(widget))
        button->setText(fitted(button, control.text));
    else if (auto edit = qobject_cast<QLineEdit*>(widget))
    {
        if (edit->text() != control.text)
            edit->setText(control.text);
    }
    else if (auto list = qobject_cast<QComboBox*>(widget))
        list->setCurrentIndex(control.value);
    else if (auto label = qobject_cast<QLabel*>(widget))
        label->setText(fitted(label, plain(control.text)));
}

// rows as the template lays them out: a control whose middle falls inside a row's height joins it
QList<QList<QString>> SavePanel::templateRows() const
{
    QList<QString> placed = order;
    std::stable_sort(placed.begin(), placed.end(),
        [this](const QString& a, const QString& b) { return controls[a].rect.top() < controls[b].rect.top(); });
    QList<QList<QString>> lines;
    int bottom = -1;
    for (const QString& id : placed)
    {
        const QRect& rect = controls[id].rect;
        if (lines.isEmpty() || rect.center().y() > bottom)
        {
            lines.append(QList<QString>());
            bottom = rect.bottom();
        }
        lines.last().append(id);
        bottom = std::max(bottom, rect.bottom());
    }
    for (QList<QString>& line : lines)
        std::stable_sort(line.begin(), line.end(),
            [this](const QString& a, const QString& b) { return controls[a].rect.left() < controls[b].rect.left(); });
    return lines;
}

// a row that is wider than the room breaks before the control that would pass it
void SavePanel::lay(int room)
{
    auto rows = static_cast<QVBoxLayout*>(panel->layout());
    while (QLayoutItem* item = rows->takeAt(0))
        delete item;
    const int gap = panel->style()->pixelMetric(QStyle::PM_LayoutHorizontalSpacing);
    for (const QList<QString>& line : templateRows())
    {
        QHBoxLayout* row = nullptr;
        int used = 0;
        for (const QString& id : line)
        {
            const int width = controls[id].widget->sizeHint().width();
            if (!row || (used > 0 && used + gap + width > room))
            {
                if (row)
                    row->addStretch();
                row = new QHBoxLayout;
                rows->addLayout(row);
                used = 0;
            }
            row->addWidget(controls[id].widget);
            used += (used ? gap : 0) + width;
        }
        if (row)
            row->addStretch();
    }
}

void SavePanel::show()
{
    panel = new QWidget;
    new QVBoxLayout(panel);
    panel->layout()->setContentsMargins(0, 0, 0, 0);
    for (const QString& id : order)
        build(id, controls[id]);
    lay(QWIDGETSIZE_MAX);

    window = new KFileCustomDialog(QUrl::fromLocalFile(folder));
    window->setWindowTitle(title);
    window->setOperationMode(KFileWidget::Saving);
    KFileWidget* files = window->fileWidget();
    files->setSupportedSchemes({QStringLiteral("file")});
    files->setConfirmOverwrite(true);
    if (!filters.isEmpty())
        files->setFilters(filters, filters.value(current, filters.first()));
    if (!name.isEmpty())
        files->setSelectedUrl(QUrl::fromLocalFile(QDir(folder).filePath(name)));
    window->setCustomWidget(panel);
    connect(window, &QDialog::finished, this, &SavePanel::finished);
    window->show();
    fit();
    Q_EMIT send(QStringLiteral("shown"));
}

// the window makes room for the rows and the places list, or the rows wrap
void SavePanel::fit()
{
    const QScreen* screen = window->screen();
    const int widest = screen ? screen->availableGeometry().width() * 9 / 10 : window->width();
    auto places = window->fileWidget()->findChild<KFilePlacesView*>();
    QWidget* dock = places ? places->parentWidget() : nullptr;
    const int wanted = KSharedConfig::openConfig()->group(QStringLiteral("KFileDialog Settings")).readEntry("Speedbar Width", 0);
    const int squeezed = dock && dock->isVisible() ? std::max(0, wanted - dock->width()) : 0;
    QWidget* field = window->fileWidget()->locationEdit();
    const int fieldEnd = field->mapTo(window, QPoint(field->width(), 0)).x();
    const int start = panel->mapTo(window, QPoint(0, 0)).x();
    const int room = widest - window->width() - squeezed + fieldEnd - start;
    if (panel->sizeHint().width() > room)
        lay(room);
    panel->setMinimumWidth(std::min(panel->sizeHint().width(), room));
    const int needed = window->width() + start + panel->minimumWidth() - fieldEnd;
    window->setMinimumWidth(std::max(window->minimumWidth(), std::min(needed, widest)));
    const int width = std::min(std::max(needed, window->width()) + squeezed, widest);
    if (width > window->width())
        window->resize(width, window->height());
}

void SavePanel::finished(int result)
{
    if (answered)
        return;
    answered = true;
    if (result != QDialog::Accepted)
    {
        Q_EMIT send(QStringLiteral("cancel"));
        return;
    }
    KFileWidget* files = window->fileWidget();
    const QString glob = files->currentFilter().filePatterns().value(0);
    Q_EMIT send(QStringLiteral("accept\t%1\t%2").arg(escaped(files->selectedUrl().toLocalFile()), escaped(glob)));
}
