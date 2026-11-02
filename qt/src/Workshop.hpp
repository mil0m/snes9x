#pragma once
#include <QWidget>

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

    void refresh(); // the current page, from the session (twice a second while shown)

  protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

  private:
    CorruptionControl &control;
    QListWidget *sidebar;
    QStackedWidget *stack;
    QTimer *timer;
    RecipePage *recipe;
    FunctionsPage *functions;
    MemoryViewer *memory;
    RomPage *rom;
    SafetyPage *safety;
    SharingPage *sharing;
};
