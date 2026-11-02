#include "Workshop.hpp"

#include <QHideEvent>
#include "FunctionsPage.hpp"
#include "MemoryViewer.hpp"
#include "SurvivorsPanel.hpp"
#include "UiKit.hpp"
#include "WorkshopPages.hpp"

#include <QHBoxLayout>
#include <QListWidget>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTimer>

namespace
{
// A text page at a readable width, left-aligned; the window's spare width stays empty.
QWidget *capped(QWidget *page, QWidget *parent)
{
    auto box = new QWidget(parent);
    auto layout = new QHBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(page, 1);
    layout->addStretch(0);
    page->setMaximumWidth(Workshop::readableWidth(page));
    return box;
}

// A page that can grow taller than the window scrolls; the wide ones (Functions, Memory)
// fill it and scroll inside.
// A scroll area hides its content's minimum width (it would let the window cut a page off
// at the right): it gets the page's, plus room for its scroll bar.
void fitWidth(QScrollArea *scroll, QWidget *page)
{
    scroll->setMinimumWidth(page->minimumSizeHint().width() + scroll->verticalScrollBar()->sizeHint().width());
}

// The Memory page: its picture keeps a usable height and the page scrolls in a short
// window, instead of the view shrinking to a sliver under the sections below it.
QWidget *scrollingTall(QWidget *page, QWidget *parent)
{
    auto scroll = new QScrollArea(parent);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(page);
    fitWidth(scroll, page);
    return scroll;
}

QWidget *scrolling(QWidget *page, QWidget *parent)
{
    auto scroll = new QScrollArea(parent);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(capped(page, scroll));
    fitWidth(scroll, scroll->widget());
    return scroll;
}

// The sidebar's rows: the icon and name as usual, and the page's note right-aligned in the
// quiet colour (the warning colour for a warning), so names stay where they are.
class NoteDelegate : public QStyledItemDelegate
{
  public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyledItemDelegate::paint(painter, option, index);
        QString note = index.data(Workshop::NoteRole).toString();
        if (note.isEmpty())
            return;
        bool selected = option.state & QStyle::State_Selected;
        bool warning = index.data(Workshop::NoteWarningRole).toBool();
        QColor colour = selected  ? option.palette.color(QPalette::HighlightedText)
                        : warning ? ui::toneColor(ui::Tone::Warning, option.palette)
                                  : ui::toneColor(ui::Tone::Secondary, option.palette);
        painter->save();
        painter->setPen(colour);
        painter->setFont(option.font);
        QRect r = option.rect.adjusted(0, 0, -10, 0);
        painter->drawText(r, Qt::AlignRight | Qt::AlignVCenter, note);
        painter->restore();
    }
};
} // namespace

int Workshop::readableWidth(const QWidget *page)
{
    return std::max(100 * page->fontMetrics().averageCharWidth(), page->minimumSizeHint().width());
}

Workshop::Workshop(CorruptionControl &control, RecipeActions &actions, FunctionSource &function_source,
                   SurvivorSource &survivor_source, MemorySource &memory_source, QWidget *parent)
    : QWidget(parent, Qt::Window), control(control)
{
    setObjectName("workshop");
    setWindowTitle(tr("Corruption Workshop"));
    auto layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    sidebar = new QListWidget(this);
    sidebar->setObjectName("workshop_pages");
    sidebar->setAccessibleName(tr("Workshop pages"));
    sidebar->setIconSize(QSize(18, 18));
    sidebar->setFrameShape(QFrame::NoFrame);
    struct Entry
    {
        const char *icon;
        QString name;
        QString tip;
    };
    const Entry entries[] = {
        { "list-details", tr("Recipe"), tr("Its rules, what else it keeps, saving and sharing it") },
        { "power", tr("Functions"), tr("The game's functions: turn off, change, protect, name") },
        { "grid-dots", tr("Memory"), tr("Look inside the game's memory, paint it, make rules from it") },
        { "cpu", tr("ROM"), tr("What changed in the game itself, what's learned, survivors and pins") },
        { "shield", tr("Safety"), tr("The crash guard and your game's save backups") },
        { "users", tr("Sharing"), tr("The library and who's in it, and remote control") },
    };
    int widest = 0;
    for (auto &e : entries)
    {
        auto item = new QListWidgetItem(ui::icon(e.icon), e.name, sidebar);
        item->setToolTip(e.tip);
        widest = std::max(widest, sidebar->fontMetrics().horizontalAdvance(e.name));
    }
    sidebar->setItemDelegate(new NoteDelegate(sidebar));
    // A name and the longest note it can have ("1,234 off", "1 rule · unsaved" is Recipe's).
    int note = std::max(sidebar->fontMetrics().horizontalAdvance(tr("1,234 off")),
                        sidebar->fontMetrics().horizontalAdvance(tr("12 rules · unsaved")) -
                            (widest - sidebar->fontMetrics().horizontalAdvance(tr("Recipe"))));
    sidebar->setFixedWidth(widest + note + 18 + 48);
    layout->addWidget(sidebar);

    stack = new QStackedWidget(this);
    recipe = new RecipePage(control, actions);
    functions = new FunctionsPage(function_source);
    memory = new MemoryViewer(memory_source);
    rom = new RomPage(control, survivor_source);
    safety = new SafetyPage(control);
    sharing = new SharingPage(control);
    stack->addWidget(scrolling(recipe, stack));
    stack->addWidget(functions);
    stack->addWidget(scrollingTall(memory, stack));
    stack->addWidget(capped(rom, stack));
    stack->addWidget(scrolling(safety, stack));
    stack->addWidget(scrolling(sharing, stack));
    text_pages = { recipe, rom, safety, sharing };
    layout->addWidget(stack, 1);

    functions->onShowInRom = [this](uint32_t first, uint32_t last) {
        showPage(WorkshopPage::Memory);
        memory->showRange(corrupt::Target::ROM, first, last, "functions");
    };
    connect(sidebar, &QListWidget::currentRowChanged, this, [this](int row) {
        stack->setCurrentIndex(row);
        if (WorkshopPage(row) == WorkshopPage::Rom)
            rom->survivors()->refresh();
        refresh();
    });
    sidebar->setCurrentRow(0);

    timer = new QTimer(this);
    timer->setInterval(500);
    connect(timer, &QTimer::timeout, this, &Workshop::refresh);
    // As wide as its widest page needs (Memory's toolbar), or a page is cut off at the right.
    resize(std::max(1000, minimumSizeHint().width()), 720);
}

WorkshopPage Workshop::page() const
{
    return WorkshopPage(stack->currentIndex());
}

void Workshop::showPage(WorkshopPage page)
{
    sidebar->setCurrentRow(int(page));
    show();
    raise();
    activateWindow();
}

void Workshop::refresh()
{
    auto status = control.status();
    refreshNotes(status);
    switch (page())
    {
    case WorkshopPage::Recipe: recipe->refresh(status); break;
    case WorkshopPage::Rom: rom->refresh(status); break;
    case WorkshopPage::Safety: safety->refresh(status); break;
    case WorkshopPage::Sharing: sharing->refresh(status); break;
    case WorkshopPage::Functions:
    case WorkshopPage::Memory: break; // they keep themselves fresh while shown
    }
}

void Workshop::refreshNotes(const PanelStatus &status)
{
    if (ticks++ % 10 == 0) // the library's folder is read every five seconds, not every tick
    {
        auto me = control.me();
        others = 0;
        for (auto &p : control.players())
            others += p.who != me;
    }
    auto notes = sidebar_notes(status, others, control.remote().enabled);
    for (size_t i = 0; i < notes.size() && int(i) < sidebar->count(); i++)
    {
        auto item = sidebar->item(int(i));
        if (item->data(NoteRole).toString() == notes[i].text && item->data(NoteWarningRole).toBool() == notes[i].warning)
            continue;
        item->setData(NoteRole, notes[i].text);
        item->setData(NoteWarningRole, notes[i].warning);
        // A screen reader hears the note with the name.
        item->setData(Qt::AccessibleTextRole,
                      notes[i].text.isEmpty() ? item->text() : tr("%1, %2").arg(item->text(), notes[i].text));
    }
}

void Workshop::capPages()
{
    // Again once shown (the Mac's controls are bigger after polish): no page cut off at the right.
    for (auto scroll : stack->findChildren<QScrollArea *>(QString(), Qt::FindDirectChildrenOnly))
        if (scroll->widget())
            fitWidth(scroll, scroll->widget());
    // Again once shown: the Mac's controls are bigger after polish, and a cap below what a
    // page needs would cut it off.
    for (auto page : text_pages)
        page->setMaximumWidth(readableWidth(page));
}

void Workshop::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    capPages();
    refresh();
    timer->start();
}

void Workshop::hideEvent(QHideEvent *event)
{
    timer->stop();
    if (onClosed && !event->spontaneous()) // not when the app is hidden or minimised
        onClosed(page(), size());
    QWidget::hideEvent(event);
}
