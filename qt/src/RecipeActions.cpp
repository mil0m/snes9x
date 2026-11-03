#include "RecipeActions.hpp"
#include "RuleEditor.hpp"
#include "corrupt/rule_text.h"

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QRegularExpression>
#include <QWidget>

using namespace corrupt;

namespace
{
QString qs(const std::string &s)
{
    return QString::fromStdString(s);
}
} // namespace

RecipeActions::RecipeActions(CorruptionControl &control, QWidget *parent, std::vector<Starter> starters)
    : control(control), parent(parent), starter_list(std::move(starters))
{
    confirm = [this](const QString &title, const QString &question) {
        return QMessageBox::question(dialogParent(), title, question) == QMessageBox::Yes;
    };
    askUnsaved = [this](const QString &title, const QString &name) {
        QMessageBox box(QMessageBox::Warning, title,
                        QObject::tr("Save the changes to \"%1\" first?").arg(name),
                        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, dialogParent());
        box.setInformativeText(QObject::tr("If you don't save them, they're gone."));
        box.setDefaultButton(QMessageBox::Save);
        box.setEscapeButton(QMessageBox::Cancel);
        switch (box.exec())
        {
        case QMessageBox::Save: return Unsaved::Save;
        case QMessageBox::Discard: return Unsaved::Discard;
        default: return Unsaved::Cancel;
        }
    };
    askText = [this](const QString &title, const QString &label, const QString &text) -> std::optional<QString> {
        bool ok = false;
        auto value = QInputDialog::getText(dialogParent(), title, label, QLineEdit::Normal, text, &ok);
        if (!ok)
            return std::nullopt;
        return value;
    };
    askFile = [this](bool save, const QString &title, const QString &start, const QString &filter) {
        return save ? QFileDialog::getSaveFileName(dialogParent(), title, start, filter)
                    : QFileDialog::getOpenFileName(dialogParent(), title, start, filter);
    };
    editor = [this](const Rule &rule, const QString &title) -> std::optional<Rule> {
        auto status = this->control.status();
        SizedMemory sizes;
        sizes.sizes = status.sizes;
        RuleEditor dialog(rule, this->control.regions(), sizes, dialogParent());
        dialog.setAllRules(status.intensity);
        dialog.setWindowTitle(title);
        if (dialog.exec() != QDialog::Accepted)
            return std::nullopt;
        return dialog.rule();
    };
}

QWidget *RecipeActions::dialogParent() const
{
    auto active = QApplication::activeWindow();
    return active ? active : parent;
}

void RecipeActions::listsChanged()
{
    refreshEntries();
    if (changed)
        changed();
}

void RecipeActions::tell(const QString &text, bool error)
{
    if (say)
        say(text, error);
}

const std::vector<RecipeEntry> &RecipeActions::refreshEntries()
{
    entry_list = control.status().game_loaded ? control.recipes() : std::vector<RecipeEntry>{};
    return entry_list;
}

bool RecipeActions::confirmDiscard(const QString &action)
{
    auto status = control.status();
    // An Untitled recipe with anything in it (#74) is worth asking about, rules or not.
    bool worth_keeping = status.has_recipe && status.edited && !status.blank &&
                         (!status.recipe_path.empty() || status.untitled || !status.rules.empty());
    if (!worth_keeping)
        return true;
    switch (askUnsaved(action, qs(status.engine.recipe_name)))
    {
    case Unsaved::Save: return save(); // a new recipe asks for its name; cancelling that cancels this
    case Unsaved::Discard: return true;
    case Unsaved::Cancel: return false;
    }
    return false;
}

bool RecipeActions::choose(const QString &choice)
{
    auto status = control.status();
    if (!status.game_loaded)
    {
        tell(QObject::tr("Load a game first"), true);
        return false;
    }
    bool mine = choice.startsWith("mine:");
    bool starter = choice.startsWith("starter:");
    size_t index = size_t(choice.section(':', 1).toInt());
    if ((!mine && !starter) || (mine && index >= entry_list.size()) || (starter && index >= starter_list.size()))
        return false;
    if (!confirmDiscard(QObject::tr("Load recipe")))
        return false;

    std::vector<std::string> problems;
    QString loaded;
    bool other_version = false;
    if (mine)
    {
        auto &entry = entry_list[index];
        if (!entry.recipe)
        {
            tell(QObject::tr("Can't load %1:\n%2")
                     .arg(qs(entry.path.filename().string()), qs(entry.errors.empty() ? "" : entry.errors[0])),
                 true);
            return false;
        }
        problems = control.loadRecipe(*entry.recipe, entry.path);
        loaded = qs(entry.name);
        other_version = entry.sha_mismatch;
    }
    else
    {
        Recipe recipe = starter_list[index].recipe;
        recipe.game = status.game;
        // A starter's ROM range means "the whole game", up to its size: fit it to this one.
        for (auto &rule : recipe.rules)
            if (rule.range.target == Target::ROM && !rule.rect && status.rom_size && rule.range.last >= status.rom_size)
                rule.range.last = uint32_t(status.rom_size - 1);
        problems = control.loadRecipe(recipe, {});
        loaded = qs(recipe.name);
    }
    if (!problems.empty())
    {
        QStringList lines;
        for (auto &p : problems)
            lines << qs(p);
        tell(QObject::tr("This recipe doesn't fit this game:\n") + lines.join("\n"), true);
        return false;
    }
    tell(other_version ? QObject::tr("Loaded %1. It was made for another version of this ROM, so addresses may differ.")
                             .arg(loaded)
         : starter     ? QObject::tr("Loaded the starter %1. Press Corrupt to try it; Save keeps your own copy.").arg(loaded)
                       : QObject::tr("Loaded %1. Press Corrupt to start.").arg(loaded));
    return true;
}

void RecipeActions::newRecipe()
{
    if (!control.status().game_loaded)
        return tell(QObject::tr("Load a game first"), true);
    if (!confirmDiscard(QObject::tr("New recipe")))
        return;
    control.newRecipe({}); // Untitled: the name comes at Save (#74)
    tell(QObject::tr("New Untitled recipe. Add a rule, turn a function off or paint, and Save to give it a name."));
}

bool RecipeActions::save()
{
    auto status = control.status();
    if (!status.has_recipe)
    {
        tell(QObject::tr("Load a recipe first"), true);
        return false;
    }
    if (status.recipe_path.empty())
        return saveAs(); // a new recipe or a starter: it needs a name first
    auto result = control.save();
    if (!result.error.empty())
    {
        tell(QObject::tr("Couldn't save: %1").arg(qs(result.error)), true);
        return false;
    }
    listsChanged();
    tell(QObject::tr("Saved %1").arg(qs(result.path.filename().string())));
    return true;
}

bool RecipeActions::saveAs()
{
    auto status = control.status();
    if (!status.has_recipe)
    {
        tell(QObject::tr("Load a recipe first"), true);
        return false;
    }
    auto name = askText(QObject::tr("Save recipe"), QObject::tr("Name:"),
                        status.untitled ? QObject::tr("My recipe") : qs(status.engine.recipe_name));
    if (!name || name->trimmed().isEmpty())
        return false;
    auto result = control.saveAs(name->trimmed().toStdString(), status.notes);
    if (!result.error.empty())
    {
        tell(QObject::tr("Couldn't save: %1").arg(qs(result.error)), true);
        return false;
    }
    listsChanged();
    tell(QObject::tr("Saved %1").arg(qs(result.path.filename().string())));
    return true;
}

void RecipeActions::share()
{
    auto status = control.status();
    if (!status.has_recipe)
        return tell(QObject::tr("Load a recipe first"), true);
    QString name = qs(status.engine.recipe_name).trimmed().replace(QRegularExpression("[^A-Za-z0-9 _-]"), "_");
    auto path = askFile(true, QObject::tr("Share recipe"), QDir::home().filePath(name + ".s9xrecipe"),
                        QObject::tr("Shared recipe (*.s9xrecipe)"));
    if (path.isEmpty())
        return;
    auto err = control.shareRecipe(path.toStdString());
    if (!err.empty())
        return tell(qs(err), true);
    tell(QObject::tr("Saved %1. Send it to a friend: they choose Import in the Workshop's Recipe page.")
             .arg(QDir::toNativeSeparators(path)));
}

void RecipeActions::import()
{
    if (!control.status().game_loaded)
        return tell(QObject::tr("Load a game first"), true);
    if (!confirmDiscard(QObject::tr("Import recipe")))
        return;
    auto path = askFile(false, QObject::tr("Import a shared recipe"), QDir::homePath(),
                        QObject::tr("Shared recipe (*.s9xrecipe)"));
    if (path.isEmpty())
        return;
    std::string name;
    auto err = control.importRecipe(path.toStdString(), name);
    listsChanged();
    if (!err.empty())
        return tell(QObject::tr("Couldn't import: %1").arg(qs(err)), true);
    tell(QObject::tr("Imported %1 into this game's recipes. Press Corrupt to try it.").arg(qs(name)));
}

void RecipeActions::addRule()
{
    if (!control.status().has_recipe)
        return tell(QObject::tr("Load a recipe or start a new one first"), true);
    auto rule = editor(default_rule(), QObject::tr("Add Rule"));
    if (!rule)
        return;
    if (auto err = control.addRule(*rule); !err.empty())
        tell(QObject::tr("Couldn't add the rule: %1").arg(qs(err)), true);
}

void RecipeActions::editRule(int row)
{
    auto status = control.status();
    if (row < 0 || row >= int(status.rules.size()))
        return;
    auto rule = editor(status.rules[size_t(row)], QObject::tr("Edit Rule"));
    if (!rule)
        return;
    if (auto err = control.replaceRule(size_t(row), *rule); !err.empty())
        tell(QObject::tr("Couldn't change the rule: %1").arg(qs(err)), true);
}

void RecipeActions::duplicateRule(int row)
{
    auto status = control.status();
    if (row < 0 || row >= int(status.rules.size()))
        return;
    Rule copy = status.rules[size_t(row)];
    copy.name += " copy";
    if (auto err = control.addRule(copy); !err.empty())
        tell(QObject::tr("Couldn't duplicate the rule: %1").arg(qs(err)), true);
}

void RecipeActions::removeRule(int row)
{
    auto status = control.status();
    if (row < 0 || row >= int(status.rules.size()))
        return;
    auto name = qs(status.rules[size_t(row)].name);
    if (!confirm(QObject::tr("Remove rule"), QObject::tr("Remove the rule \"%1\"?").arg(name)))
        return;
    if (auto err = control.removeRule(size_t(row)); !err.empty())
        tell(QObject::tr("Couldn't remove the rule: %1").arg(qs(err)), true);
}
