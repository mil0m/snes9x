#pragma once
#include <QWidget>

#include <functional>
#include <vector>

#include "CorruptionControl.hpp"
#include "WorkshopPage.hpp"

class FunctionsPage;
class MemoryViewer;
class QListWidget;
class QStackedWidget;
class QTimer;
class RecipeActions;
class RecipePage;
class RomPage;
class SafetyPage;
class SharingPage;
struct FunctionSource;
struct MemorySource;
struct SurvivorSource;

// Everything that isn't for the moment of play (#63), in one window: a sidebar of pages
// (Recipe, Functions, Memory, ROM, Safety, Sharing) instead of a window each. Links between
// them switch pages (Functions' Show in ROM opens Memory there).
class Workshop : public QWidget
{
  public:
    Workshop(CorruptionControl &control, RecipeActions &actions, FunctionSource &functions, SurvivorSource &survivors,
             MemorySource &memory, QWidget *parent = nullptr);

    void showPage(WorkshopPage page); // and brings the window forward
    WorkshopPage page() const;

    RecipePage *recipePage() const { return recipe; }
    FunctionsPage *functionsPage() const { return functions; }
    MemoryViewer *memoryPage() const { return memory; }
    RomPage *romPage() const { return rom; }
    SafetyPage *safetyPage() const { return safety; }
    SharingPage *sharingPage() const { return sharing; }

    void refresh(); // the current page and the sidebar's notes, from the session (twice a second while shown)

    // The sidebar's note beside a page's name (#65), and whether it's a warning.
    enum Roles
    {
        NoteRole = Qt::UserRole + 1,
        NoteWarningRole,
    };
    // How wide a text page may get: about 100 characters, never less than it needs.
    static int readableWidth(const QWidget *page);

    // Closed: the page it was on and its size, for the dock to open it so next time.
    std::function<void(WorkshopPage page, QSize size)> onClosed;

  protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

  private:
    void refreshNotes(const PanelStatus &status);
    void capPages();

    CorruptionControl &control;
    std::vector<QWidget *> text_pages; // kept to a readable width
    size_t others = 0;             // other people in the library for this game
    int ticks = 0;
    QListWidget *sidebar;
    class QLabel *recipe_name, *recipe_status; // which recipe it changes, above the pages (#74)
    QStackedWidget *stack;
    QTimer *timer;
    RecipePage *recipe;
    FunctionsPage *functions;
    MemoryViewer *memory;
    RomPage *rom;
    SafetyPage *safety;
    SharingPage *sharing;
};
