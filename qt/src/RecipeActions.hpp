#pragma once
#include <QString>

#include <functional>
#include <vector>

#include "CorruptionControl.hpp"
#include "Starters.hpp"

class QWidget;

// What the cockpit, the Workshop's Recipe page and the Corruption menu do to a recipe (#63):
// load one from the library or the starters, new, save, save as, share, import, and the
// rules' editor. One place, so the three agree (and ask before throwing edits away).
// The questions it asks go through hooks, so tests can answer them.
class RecipeActions
{
  public:
    RecipeActions(CorruptionControl &control, QWidget *parent, std::vector<Starter> starters);

    // What happened, for the place that asked: text, and whether it's an error.
    std::function<void(const QString &text, bool error)> say;

    // The questions. Defaults are Qt's standard dialogs on `parent`.
    std::function<bool(const QString &title, const QString &question)> confirm;
    // The recipe has unsaved changes and something would replace it: keep them, throw them
    // away, or don't do it after all. The default is the Mac's Save / Don't Save / Cancel.
    enum class Unsaved
    {
        Save,
        Discard,
        Cancel,
    };
    std::function<Unsaved(const QString &title, const QString &recipe_name)> askUnsaved;
    std::function<std::optional<QString>(const QString &title, const QString &label, const QString &text)> askText;
    // Save: true asks where to write a file, false which file to open; empty: cancelled.
    std::function<QString(bool save, const QString &title, const QString &start, const QString &filter)> askFile;
    // The rule editor: the rule as edited, or nothing if cancelled.
    std::function<std::optional<corrupt::Rule>(const corrupt::Rule &rule, const QString &title)> editor;
    // The game's recipes changed on disk (saved, imported): lists that show them read again.
    std::function<void()> changed;

    const std::vector<Starter> &starters() const { return starter_list; }
    // The game's recipes, read again from the library.
    const std::vector<corrupt::RecipeEntry> &refreshEntries();
    const std::vector<corrupt::RecipeEntry> &entries() const { return entry_list; }

    bool confirmDiscard(const QString &action); // true to go on: nothing to lose, saved, or "Don't Save"
    // "mine:<i>" (an entry) or "starter:<i>". True when it loaded.
    bool choose(const QString &choice);
    void newRecipe();
    bool save();   // true when it was saved
    bool saveAs(); // false when cancelled or it failed
    void share();
    void import();

    void addRule();
    void editRule(int row);
    void duplicateRule(int row);
    void removeRule(int row);

  private:
    CorruptionControl &control;
    QWidget *parent;
    std::vector<Starter> starter_list;
    std::vector<corrupt::RecipeEntry> entry_list;
    void tell(const QString &text, bool error = false);
    void listsChanged();
    QWidget *dialogParent() const; // the window in front (the Workshop or the main one)
};
