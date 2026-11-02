#include "pane-parts.hpp"

#include <QAbstractButton>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QFontMetrics>
#include <QFrame>
#include <QLabel>
#include <QRadioButton>
#include <QStringList>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

// height of a control's first line, its label sits level with that
static int first_line_height(QWidget* control)
{
    QLayout* rows = control->layout();
    const int natural =
        rows != nullptr && rows->count() > 0 ? rows->itemAt(0)->sizeHint().height() : control->sizeHint().height();
    return qBound(control->minimumHeight(), natural, control->maximumHeight());
}

void keep_to_pitch(QWidget* box)
{
    if (qobject_cast<QCheckBox*>(box) != nullptr || qobject_cast<QRadioButton*>(box) != nullptr)
    {
        box->setFixedHeight(check_pitch_px);
    }
}

PaneForm::PaneForm(QWidget* pane, int axis_px) : QGridLayout(pane), axis(axis_px)
{
    setContentsMargins(0, 0, 0, 0);
    // a margin on each label, since a row without one would lose any spacing
    setHorizontalSpacing(0);
    setVerticalSpacing(0);
    setColumnMinimumWidth(0, axis);
    setColumnStretch(1, 1);
}

void PaneForm::leaveRoom(int height_px)
{
    setRowMinimumHeight(next_row++, height_px);
}

void PaneForm::addRow(const QString& label, QWidget* control)
{
    auto* words = new QLabel;
    words->setText(in_lines_of(label, words->fontMetrics(), axis - label_gap_px));
    words->setAlignment(Qt::AlignRight | Qt::AlignTop);
    addRow(words, control);
}

void PaneForm::addRow(QWidget* label, QWidget* control)
{
    label->setContentsMargins(0, qMax(0, (first_line_height(control) - first_line_height(label)) / 2), label_gap_px, 0);
    addWidget(label, next_row, 0, Qt::AlignRight | Qt::AlignTop);
    addWidget(control, next_row++, 1, Qt::AlignLeft | Qt::AlignTop);
    rows.insert(control, {label, next_row, row_gap_px});
    leaveRoom(row_gap_px);
}

void PaneForm::addCheck(QWidget* box)
{
    keep_to_pitch(box);
    addWidget(box, next_row++, 1, Qt::AlignLeft | Qt::AlignTop);
    // a box with a note needs air before the next one
    const bool plain_box = box->minimumHeight() == check_pitch_px;
    rows.insert(box, {nullptr, next_row, plain_box ? 0 : row_gap_px});
    leaveRoom(plain_box ? 0 : row_gap_px);
}

void PaneForm::addWide(QWidget* whole_row)
{
    addWidget(whole_row, next_row++, 0, 1, 2);
    leaveRoom(row_gap_px);
}

void PaneForm::setRowVisible(QWidget* control, bool visible)
{
    const Row row = rows.value(control);
    control->setVisible(visible);
    if (row.label != nullptr)
    {
        row.label->setVisible(visible);
    }
    setRowMinimumHeight(row.gap_row, visible ? row.gap_px : 0);
}

void PaneForm::addDivider()
{
    auto* line = new QFrame;
    line->setObjectName(QStringLiteral("divider"));
    // a groove of two lines, the style draws its own frames too faintly to see
    line->setStyleSheet(QStringLiteral("QFrame { border: none; border-top: 1px solid palette(mid);"
                                       " border-bottom: 1px solid palette(light); }"));
    line->setFixedHeight(2);
    auto* inset = new QWidget;
    auto* across = new QHBoxLayout(inset);
    across->setContentsMargins(divider_inset_px, 0, divider_inset_px, 0);
    across->addWidget(line);

    leaveRoom(divider_room_px);
    addWidget(inset, next_row++, 0, 1, 2);
    leaveRoom(divider_room_px);
}

QFont scaled_font(const QFont& font, double scale)
{
    // A font says its size in pixels or in points, whichever it was given.
    QFont scaled = font;
    if (scaled.pixelSize() > 0)
    {
        scaled.setPixelSize(qRound(scaled.pixelSize() * scale));
    }
    else
    {
        scaled.setPointSizeF(scaled.pointSizeF() * scale);
    }
    return scaled;
}

// As wide as the panes' other sliders.
static const int stops_slider_width_px = 220;
// Several key steps in a row make one change.
static const int stops_slider_rest_ms = 500;

QSlider* stops_slider(const QList<double>& stops, double current, const std::function<void(double)>& write)
{
    auto* slider = new QSlider(Qt::Horizontal);
    slider->setRange(0, int(stops.size()) - 1);
    slider->setPageStep(1);
    slider->setTickPosition(QSlider::TicksBelow);
    slider->setTickInterval(1);
    slider->setFixedWidth(stops_slider_width_px);
    int nearest = 0;
    for (int index = 1; index < stops.size(); index++)
    {
        if (qAbs(stops[index] - current) < qAbs(stops[nearest] - current))
        {
            nearest = index;
        }
    }
    slider->setValue(nearest);

    auto* rest = new QTimer(slider);
    rest->setSingleShot(true);
    rest->setInterval(stops_slider_rest_ms);
    const auto write_now = [slider, stops, write, rest]() {
        rest->stop();
        write(stops[slider->value()]);
    };
    QObject::connect(rest, &QTimer::timeout, slider, write_now);
    QObject::connect(slider, &QSlider::valueChanged, slider, [slider, rest]() {
        if (!slider->isSliderDown())
        {
            rest->start();
        }
    });
    QObject::connect(slider, &QSlider::sliderReleased, slider, write_now);
    return slider;
}

QWidget* with_end_words(QSlider* slider, const QString& low, const QString& high)
{
    auto* box = new QWidget;
    auto* rows = new QVBoxLayout(box);
    rows->setContentsMargins(0, 0, 0, 0);
    rows->setSpacing(0);
    rows->addWidget(slider);

    auto* words = new QHBoxLayout;
    auto* low_word = new QLabel(low);
    auto* high_word = new QLabel(high);
    // The words are smaller than the pane's other text.
    const QFont small_font = scaled_font(low_word->font(), 0.8);
    low_word->setFont(small_font);
    high_word->setFont(small_font);
    words->addWidget(low_word);
    words->addStretch();
    words->addWidget(high_word);
    rows->addLayout(words);
    // A slider of a fixed width keeps its words at its own ends, not at the
    // ends of a wider column.
    if (slider->minimumWidth() == slider->maximumWidth())
    {
        box->setFixedWidth(slider->maximumWidth());
    }
    return box;
}

void follow_slider(QSlider* slider, int every_ms, const std::function<void(int)>& apply)
{
    auto* wait = new QTimer(slider);
    wait->setSingleShot(true);
    wait->setInterval(every_ms);
    // The value is read when the wait is over, so the last step is never lost.
    QObject::connect(wait, &QTimer::timeout, slider, [slider, apply]() { apply(slider->value()); });
    QObject::connect(slider, &QSlider::valueChanged, slider, [wait]() {
        if (!wait->isActive())
        {
            wait->start();
        }
    });
}

// (A wrapping label asks its layout for far more height than it fills, and
// the window, which takes its page's height, comes out twice too high.)
QString in_lines_of(const QString& text, const QFontMetrics& metrics, int widest)
{
    QStringList lines;
    QString line;
    for (const QString& word : text.split(QLatin1Char(' ')))
    {
        const QString longer = line.isEmpty() ? word : line + QLatin1Char(' ') + word;
        if (!line.isEmpty() && metrics.horizontalAdvance(longer) > widest)
        {
            lines << line;
            line = word;
        }
        else
        {
            line = longer;
        }
    }
    lines << line;
    return lines.join(QLatin1Char('\n'));
}

QWidget* with_note(QCheckBox* box, const QString& note)
{
    // The note begins under the checkbox's words, not under its box.
    const int box_width = 22;
    return with_note_under(box, note, box_width);
}

QWidget* with_note_under(QWidget* control, const QString& note, int indent_px)
{
    auto* group = new QWidget;
    auto* rows = new QVBoxLayout(group);
    rows->setContentsMargins(0, 0, 0, 0);
    rows->setSpacing(2);
    keep_to_pitch(control);
    // The control keeps its own width; the note's line may be wider.
    rows->addWidget(control, 0, Qt::AlignLeft);

    if (!note.isEmpty())
    {
        auto* note_label = new QLabel(note);
        const QFont small_font = scaled_font(note_label->font(), 0.85);
        note_label->setFont(small_font);
        note_label->setEnabled(false);
        note_label->setContentsMargins(indent_px, 0, 0, 0);
        // A long note goes on in a second line rather than out of the
        // window: a note usually stands in a form's right column.
        const int widest_note = 300;
        note_label->setText(in_lines_of(note, note_label->fontMetrics(), widest_note));
        rows->addWidget(note_label);
    }

    return group;
}
