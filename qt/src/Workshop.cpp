#include "Workshop.hpp"
#include "FunctionsPage.hpp"
#include "MemoryViewer.hpp"
#include "SurvivorsPanel.hpp"
#include "UiKit.hpp"
#include "WorkshopPages.hpp"

#include <QHBoxLayout>
#include <QListWidget>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTimer>

namespace
{
// A page that can grow taller than the window scrolls; the wide ones (Functions, Memory)
// fill it and scroll inside.
QWidget *scrolling(QWidget *page, QWidget *parent)
{
    auto scroll = new QScrollArea(parent);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(page);
    return scroll;
}
} // namespace

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
    sidebar->setFixedWidth(widest + 18 + 48);
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
    stack->addWidget(memory);
    stack->addWidget(rom);
    stack->addWidget(scrolling(safety, stack));
    stack->addWidget(scrolling(sharing, stack));
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

void Workshop::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    refresh();
    timer->start();
}

void Workshop::hideEvent(QHideEvent *event)
{
    timer->stop();
    QWidget::hideEvent(event);
}
