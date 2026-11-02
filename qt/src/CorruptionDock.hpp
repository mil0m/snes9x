#pragma once
#include <QDockWidget>

#include <memory>

#include "WorkshopPage.hpp"

class Cockpit;
class EmuApplication;
class GuideWindow;
class RecipeActions;
class RemoteControl;
class SessionControl;
class Workshop;
struct FunctionSource;
struct MemorySource;
struct SurvivorSource;

// The corruption panel (#63): the cockpit for playing, and the way to the Workshop (one
// window for everything else), the guide and the remote server. The Corruption menu calls
// the actions here.
class CorruptionDock : public QDockWidget
{
    Q_OBJECT

  public:
    CorruptionDock(EmuApplication &app, QWidget *parent);
    ~CorruptionDock() override;

    void openGuide(const QString &section); // a "## heading" of the guide, or empty for the top
    void openWorkshop(WorkshopPage page);
    RecipeActions &recipeActions() { return *actions; }
    void refreshRecipes();

  private:
    void say(const QString &text, bool error);
    QString toggleKey() const; // the Corrupt/Stop shortcut, or empty
    // SNES9X_UI_TOUR=<dir>: screenshots of the cockpit and every Workshop page, for UX work
    // (see docs/corruption/README.md, "Developing").
    void tour(const QString &dir);

    EmuApplication &app;
    RemoteControl *remote = nullptr;
    std::unique_ptr<SessionControl> control;
    std::unique_ptr<RecipeActions> actions;
    std::unique_ptr<FunctionSource> function_source;
    std::unique_ptr<SurvivorSource> survivor_source;
    std::unique_ptr<MemorySource> memory_source;
    Cockpit *cockpit = nullptr;
    Workshop *workshop = nullptr;
    GuideWindow *guide = nullptr;
};
