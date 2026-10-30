#include "MemoryViewer.hpp"
#include "MemoryView.hpp"
#include "RuleEditor.hpp"
#include "corrupt/rule_text.h"

#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

using namespace corrupt;

namespace
{

QString hex4(uint32_t v)
{
    return QString("0x%1").arg(v, 4, 16, QChar('0')).toUpper().replace("0X", "0x");
}

} // namespace

MemoryViewer::MemoryViewer(MemorySource &source, QWidget *parent)
    : QWidget(parent, Qt::Window), source(source)
{
    setWindowTitle(tr("Memory Viewer"));
    auto layout = new QVBoxLayout(this);

    auto top = new QHBoxLayout;
    target_combo = new QComboBox(this);
    target_combo->setObjectName("target");
    for (auto t : kAllTargets)
        target_combo->addItem(targetLabel(t), QString(target_name(t)));
    overlay_combo = new QComboBox(this);
    overlay_combo->setObjectName("overlay");
    overlay_combo->addItem(tr("Activity: what the game changes"), QString("activity"));
    overlay_combo->addItem(tr("Hits: what the recipe wrote"), QString("hits"));
    overlay_combo->addItem(tr("Plain"), QString("none"));
    goto_edit = new QLineEdit(this);
    goto_edit->setObjectName("goto");
    goto_edit->setPlaceholderText(tr("Go to 0x…"));
    goto_edit->setMaximumWidth(110);
    auto clear = new QPushButton(tr("Clear Hits"), this);
    clear->setObjectName("clear_hits");
    top->addWidget(target_combo);
    top->addWidget(overlay_combo, 1);
    top->addWidget(goto_edit);
    top->addWidget(clear);
    layout->addLayout(top);

    view = new MemoryView(this);
    view->setObjectName("view");
    layout->addWidget(view, 1);

    auto bottom = new QHBoxLayout;
    selection_label = new QLabel(tr("Drag across bytes to select a range"), this);
    selection_label->setObjectName("selection");
    new_rule_button = new QPushButton(tr("New Rule from Selection..."), this);
    new_rule_button->setObjectName("new_rule");
    new_rule_button->setEnabled(false);
    bottom->addWidget(selection_label, 1);
    bottom->addWidget(new_rule_button);
    layout->addLayout(bottom);

    auto copies_box = new QGroupBox(tr("Copies of the sprite table and palette in WRAM"), this);
    auto copies_layout = new QVBoxLayout(copies_box);
    auto copies_hint = new QLabel(tr("Most games rebuild sprites and colours from these copies every frame, "
                                     "so corrupting a copy sticks where corrupting oam or cgram only flashes."),
                                  copies_box);
    copies_hint->setWordWrap(true);
    auto find = new QPushButton(tr("Find Copies"), copies_box);
    find->setObjectName("find_copies");
    copies_list = new QListWidget(copies_box);
    copies_list->setObjectName("copies");
    copies_list->setMaximumHeight(110);
    copies_layout->addWidget(copies_hint);
    copies_layout->addWidget(find);
    copies_layout->addWidget(copies_list);
    layout->addWidget(copies_box);

    connect(target_combo, &QComboBox::currentIndexChanged, this, [this] { targetChanged(); });
    connect(overlay_combo, &QComboBox::currentIndexChanged, this, [this] {
        auto o = overlay_combo->currentData().toString();
        view->setOverlay(o == "hits" ? MemoryView::Overlay::Hits
                         : o == "none" ? MemoryView::Overlay::None
                                       : MemoryView::Overlay::Activity);
    });
    connect(goto_edit, &QLineEdit::returnPressed, this, [this] { goTo(); });
    connect(clear, &QPushButton::clicked, this, [this] { this->source.clearHits(); });
    connect(find, &QPushButton::clicked, this, [this] { findCopies(); });
    connect(copies_list, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        if (!item->data(Qt::UserRole).isValid())
            return; // the "none found" line
        auto first = item->data(Qt::UserRole).toUInt();
        auto last = item->data(Qt::UserRole + 1).toUInt();
        pending_select = std::pair{ first, last };
        int wram = target_combo->findData(QString("wram"));
        if (target_combo->currentIndex() != wram)
            target_combo->setCurrentIndex(wram); // the selection lands with the WRAM snapshot
        else
            refresh();
    });
    connect(copies_list, &QListWidget::itemClicked, copies_list, &QListWidget::itemActivated);
    connect(new_rule_button, &QPushButton::clicked, this, [this] { newRule(); });
    view->onSelectionChanged = [this] { selectionChanged(); };

    connect(&timer, &QTimer::timeout, this, [this] { refresh(); });
    resize(view->sizeHint().width() + 40, 640);
}

Target MemoryViewer::target() const
{
    return parse_target(target_combo->currentData().toString().toStdString()).value_or(Target::WRAM);
}

void MemoryViewer::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    open = true;
    source.watch(target());
    refresh();
    timer.start(100);
}

void MemoryViewer::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    open = false;
    timer.stop();
    source.stop();
}

void MemoryViewer::targetChanged()
{
    if (open)
        source.watch(target());
    refresh();
}

void MemoryViewer::refresh()
{
    auto snap = source.snapshot();
    if (snap.target != target() && !snap.bytes.empty())
        return; // the switch hasn't reached the emulation thread yet
    view->setSnapshot(snap);
    if (pending_select && !snap.bytes.empty())
    {
        view->select(pending_select->first, pending_select->second);
        pending_select.reset();
    }
}

void MemoryViewer::goTo()
{
    auto n = parse_number(goto_edit->text().toStdString());
    if (n && *n >= 0)
        view->scrollTo(uint32_t(*n));
}

void MemoryViewer::selectionChanged()
{
    auto sel = view->selection();
    new_rule_button->setEnabled(sel.has_value());
    if (!sel)
    {
        selection_label->setText(tr("Drag across bytes to select a range"));
        return;
    }
    selection_label->setText(QString("%1 %2..%3 (%4 bytes)")
                                 .arg(target_name(view->snapshot().target), hex4(sel->first), hex4(sel->second))
                                 .arg(sel->second - sel->first + 1));
}

Rule MemoryViewer::ruleFromSelection() const
{
    Rule r = default_rule();
    if (auto sel = view->selection())
    {
        r.range = { view->snapshot().target, sel->first, sel->second };
        r.name = std::string(target_name(r.range.target)) + " " + hex4(sel->first).toStdString();
    }
    return r;
}

void MemoryViewer::newRule()
{
    if (!view->selection())
        return;
    SizedMemory sizes;
    sizes.sizes = source.sizes();
    RuleEditor editor(ruleFromSelection(), source.regions(), sizes, this);
    editor.setWindowTitle(tr("New Rule from Selection"));
    if (editor.exec() != QDialog::Accepted)
        return;
    auto error = source.addRule(editor.rule());
    selection_label->setText(error.empty() ? tr("Added rule \"%1\" to the recipe").arg(QString::fromStdString(editor.rule().name))
                                           : tr("Couldn't add the rule: %1").arg(QString::fromStdString(error)));
}

void MemoryViewer::findCopies()
{
    copies_list->clear();
    for (auto of : { Target::OAM, Target::CGRAM })
        for (auto &run : source.findCopies(of))
        {
            auto text = QString("%1 %2..%3 in wram %4..%5 (%6 bytes)")
                            .arg(target_name(of), hex4(run.needle_offset), hex4(run.needle_offset + run.length - 1),
                                 hex4(run.haystack_offset), hex4(run.haystack_offset + run.length - 1))
                            .arg(run.length);
            auto item = new QListWidgetItem(text, copies_list);
            item->setData(Qt::UserRole, run.haystack_offset);
            item->setData(Qt::UserRole + 1, run.haystack_offset + run.length - 1);
        }
    if (copies_list->count() == 0)
        copies_list->addItem(tr("No copies found. Try again in the middle of gameplay."));
}
