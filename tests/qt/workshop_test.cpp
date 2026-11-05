#include "doctest/doctest.h"
#include "FunctionsPage.hpp"
#include "IntensitySlider.hpp"
#include "MemoryViewer.hpp"
#include "RecipeActions.hpp"
#include "UiKit.hpp"
#include "ExploreRunner.hpp"
#include "Workshop.hpp"
#include "WorkshopPages.hpp"
#include "fake_control.h"
#include "fake_functions.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <fstream>
#include <algorithm>
#include <optional>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>

using namespace corrupt;

namespace
{

template <typename W>
W *child(QWidget &w, const char *name)
{
    auto c = w.findChild<W *>(name);
    REQUIRE_MESSAGE(c, name);
    return c;
}

struct Fixture
{
    FakeControl control;
    FakeFunctions functions;
    EmptySurvivors survivors;
    EmptyMemory memory;
    QWidget parent;
    RecipeActions actions{ control, &parent, {} };
    Workshop workshop{ control, actions, functions, survivors, memory };

    Fixture()
    {
        actions.confirm = [](const QString &, const QString &) { return true; };
        workshop.resize(std::max(1000, workshop.minimumSizeHint().width()), 720);
    }
    void shot(const char *name)
    {
        if (auto dir = qgetenv("CORRUPT_SHOTS_DIR"); !dir.isEmpty())
        {
            QDir().mkpath(QString::fromLocal8Bit(dir));
            workshop.show();
            workshop.refresh();
            QApplication::processEvents(); // the tables' rows fit their final widths
            workshop.refresh();
            QApplication::processEvents();
            workshop.grab().save(QString("%1/%2.png").arg(QString::fromLocal8Bit(dir), name));
        }
    }
};

} // namespace

TEST_SUITE("workshop")
{
    TEST_CASE("eight pages in the sidebar; showPage switches to one")
    {
        Fixture f;
        auto sidebar = child<QListWidget>(f.workshop, "workshop_pages");
        REQUIRE(sidebar->count() == 8);
        QStringList names;
        for (int i = 0; i < sidebar->count(); i++)
            names << sidebar->item(i)->text();
        CHECK(names == QStringList{ "Recipe", "Functions", "Memory", "ROM", "Safety", "Sharing", "Moments", "Explored" });
        CHECK(f.workshop.page() == WorkshopPage::Recipe);
        f.workshop.showPage(WorkshopPage::Safety);
        CHECK(f.workshop.page() == WorkshopPage::Safety);
        CHECK(sidebar->currentRow() == int(WorkshopPage::Safety));
        sidebar->setCurrentRow(int(WorkshopPage::Functions));
        CHECK(f.workshop.page() == WorkshopPage::Functions);
        f.workshop.close();
    }

    TEST_CASE("Functions' Show in ROM opens the Memory page")
    {
        Fixture f;
        f.workshop.showPage(WorkshopPage::Functions);
        REQUIRE(f.workshop.functionsPage()->onShowInRom);
        f.workshop.functionsPage()->onShowInRom(0x19EE, 0x1A0F);
        CHECK(f.workshop.page() == WorkshopPage::Memory);
        f.workshop.close();
    }

    TEST_CASE("Recipe: the rules in full, and what else the recipe keeps, with Remove")
    {
        Fixture f;
        f.control.running(2);
        f.control.s.rule_crashes = { 0, 4 };
        f.control.s.kept = { { KeptItem::Kind::Switch, 0x8000, "gravity off", "button 2 · starts off" },
                             { KeptItem::Kind::Pins, 0, "12 pinned bytes", "ROM changes kept from the survivors" } };
        auto &page = *f.workshop.recipePage();
        page.refresh(f.control.status());
        CHECK(child<QLabel>(page, "recipe_name")->text() == "Gravity mess");
        auto rules = child<QTableWidget>(page, "rules");
        REQUIRE(rules->rowCount() == 2);
        CHECK(rules->columnCount() == 5);
        CHECK(rules->item(1, 4)->text() == "4");
        auto kept = child<QTableWidget>(page, "kept");
        REQUIRE(kept->rowCount() == 2);
        CHECK(kept->item(0, 0)->text() == "gravity off");
        CHECK(kept->item(1, 1)->text() == "ROM changes kept from the survivors");
        auto remove = child<QPushButton>(page, "remove_kept");
        CHECK_FALSE(remove->isEnabled());
        kept->selectRow(0);
        CHECK(remove->isEnabled());
        remove->click();
        CHECK(f.control.calls.back() == "remove kept gravity off");
        f.shot("workshop-recipe");
    }

    TEST_CASE("Recipe: each rule's Strength slider says what it means, times all rules (#73)")
    {
        Fixture f;
        f.control.running(1);
        f.control.s.rules[0].op = { .type = OpType::NopOut };
        f.control.s.rules[0].range = { Target::ROM, 0, 0xFFFF };
        f.control.s.rules[0].count = 4;
        auto &page = *f.workshop.recipePage();
        page.refresh(f.control.status());
        auto slider = dynamic_cast<IntensitySlider *>(child<QTableWidget>(page, "rules")->cellWidget(0, 3));
        REQUIRE(slider);
        CHECK(slider->toolTip() == "At 100%: changes 4 instructions each time it fires.");
        CHECK(child<QTableWidget>(page, "rules")->horizontalHeaderItem(3)->toolTip().contains("all rules"));
        f.control.s.intensity = 0.5;
        page.refresh(f.control.status());
        CHECK(slider->toolTip() == "At 50% (100% × 50% for all rules): changes 2 of its 4 instructions each time it fires.");
    }

    TEST_CASE("Recipe: Add Rule goes through the editor; New asks for a name; Remember runs")
    {
        Fixture f;
        f.control.running(1);
        auto &page = *f.workshop.recipePage();
        page.refresh(f.control.status());
        f.actions.editor = [](const Rule &rule, const QString &) -> std::optional<Rule> {
            Rule r = rule;
            r.name = "sparkle";
            return r;
        };
        child<QPushButton>(page, "add_rule")->click();
        CHECK(f.control.calls.back() == "add rule sparkle");
        child<QPushButton>(page, "new_recipe")->click();
        CHECK(f.control.calls.back() == "new "); // Untitled: no name asked (#74)
        child<QCheckBox>(page, "moments")->setChecked(!child<QCheckBox>(page, "moments")->isChecked());
        CHECK(f.control.calls.back().rfind("moments", 0) == 0);
        auto rules = child<QTableWidget>(page, "rules");
        rules->selectRow(0);
        child<QPushButton>(page, "remove_rule")->click();
        CHECK(f.control.calls.back() == "remove rule 0");
    }

    TEST_CASE("ROM: what changed and what's learned; Export and Restore Clean")
    {
        Fixture f;
        f.control.running();
        f.control.s.rom_changed = 120;
        f.control.s.rom_used = 30;
        f.control.s.functions = 60;
        f.control.s.code.seen = true;
        f.control.s.code.code = 3132;
        auto &page = *f.workshop.romPage();
        page.refresh(f.control.status());
        CHECK(child<QLabel>(page, "rom_changed")->text().contains("120 bytes changed, 30 of them used"));
        CHECK(child<QLabel>(page, "rom_learned")->text().contains("60 functions known"));
        page.askSavePath = [](const QString &, const QString &) { return QString("/tmp/pocky.sfc"); };
        child<QPushButton>(page, "export_rom")->click();
        CHECK(f.control.calls.back() == "export pocky.sfc");
        child<QPushButton>(page, "restore_rom")->click();
        CHECK(f.control.calls.back() == "restore clean rom");
        CHECK_FALSE(child<QPushButton>(page, "clear_paint")->isEnabled());
    }

    TEST_CASE("Safety: the guard's switches follow the session and change it")
    {
        Fixture f;
        f.control.s.instant_enabled = false;
        f.control.s.crashes = 1;
        f.control.s.last_crash = "frozen at $80:1234";
        f.control.backup_list = { { "/tmp/backups/goof_20261102_auto.srm", {} } };
        auto &page = *f.workshop.safetyPage();
        page.refresh(f.control.status());
        CHECK(child<QCheckBox>(page, "detect")->isChecked());
        CHECK_FALSE(child<QCheckBox>(page, "instant_revert")->isChecked());
        CHECK(child<QLabel>(page, "crash_status")->text().contains("frozen at $80:1234"));
        child<QCheckBox>(page, "instant_revert")->click();
        CHECK(f.control.calls.back() == "instant on");
        child<QCheckBox>(page, "test_first")->click();
        CHECK(f.control.calls.back() == "test first off");
        auto policy = child<QComboBox>(page, "policy");
        policy->activated(policy->findData("disable"));
        CHECK(f.control.calls.back() == "policy disable");
        auto backups = child<QListWidget>(page, "backups");
        REQUIRE(backups->count() == 1);
        CHECK(backups->item(0)->text().contains("while playing"));
        backups->setCurrentRow(0);
        page.confirm = [](const QString &, const QString &) { return true; };
        child<QPushButton>(page, "restore_backup")->click();
        CHECK(f.control.calls.back() == "restore goof_20261102_auto.srm");
    }

    TEST_CASE("Safety: each switch says what it does, what off means, and what it has done (#76)")
    {
        PanelStatus s;
        s.game_loaded = s.has_recipe = true;
        // Catch crashes: what it watches for, what it does, what off means.
        auto guard = guard_note(s);
        CHECK(guard.contains("freezing or going black while you press buttons"));
        CHECK(guard.contains("goes back a few seconds"));
        CHECK(guard.contains("Back to Normal"));

        // Test ahead: nothing to do without ROM rules; the recipe's seconds; the counts.
        CHECK(ahead_note(s).contains("plays 3 seconds ahead"));
        CHECK(ahead_note(s).contains("no ROM rules, so there's nothing to test"));
        Rule rom_rule;
        rom_rule.range.target = Target::ROM;
        s.rules = { rom_rule };
        s.test_first_seconds = 5;
        CHECK(ahead_note(s).contains("plays 5 seconds ahead"));
        CHECK(ahead_note(s).contains("Nothing tested yet."));
        s.tested_first = 12;
        s.tested_rerolled = 2;
        s.tested_skipped = 1;
        CHECK(ahead_note(s).contains("So far: 12 tested, 2 picked again, 1 skipped."));
        s.test_first_seconds = 0;
        CHECK(ahead_note(s).contains("This recipe turns it off."));

        // Undo at once: nothing to watch on a clean ROM without ROM rules; the recipe; the count.
        PanelStatus clean;
        CHECK(instant_note(clean).contains("isn't code"));
        CHECK(instant_note(clean).contains("costs a little speed"));
        CHECK(instant_note(clean).contains("The ROM isn't changed, so there's nothing to watch now."));
        clean.rom_changed = 3; // painted bytes count as changed
        CHECK(instant_note(clean).contains("Nothing put back yet."));
        clean.instant_reverts = 4;
        CHECK(instant_note(clean).contains("So far: 4 put back."));
        clean.instant_revert = false;
        CHECK(instant_note(clean).contains("This recipe turns it off."));

        // After a crash: every choice in a sentence, and the ROM bytes either way.
        CHECK(policy_note(PolicyKind::Reroll).contains("new random values"));
        CHECK(policy_note(PolicyKind::Skip).contains("without that rule's changes"));
        CHECK(policy_note(PolicyKind::Dampen).contains("halved"));
        CHECK(policy_note(PolicyKind::Disable).contains("turned off in the recipe"));
        CHECK(policy_note(PolicyKind::Escalate).contains("fifth turns it off"));
        for (auto p : { PolicyKind::Reroll, PolicyKind::Skip, PolicyKind::Dampen, PolicyKind::Disable,
                        PolicyKind::Escalate })
            CHECK(policy_note(p).contains("are put back either way"));
    }

    TEST_CASE("Safety: the notes sit under their switches, named plainly, and follow the status (#76)")
    {
        Fixture f;
        Rule rom_rule;
        rom_rule.range.target = Target::ROM;
        f.control.s.rules = { rom_rule };
        f.control.s.tested_first = 2;
        f.control.s.policy = PolicyKind::Disable;
        auto &page = *f.workshop.safetyPage();
        page.refresh(f.control.status());
        CHECK(child<QCheckBox>(page, "test_first")->text() == "Test ROM changes before they reach the game");
        CHECK(child<QCheckBox>(page, "instant_revert")->text() == "Undo a ROM change the moment it derails the game");
        CHECK(child<QLabel>(page, "test_first_note")->text().contains("So far: 2 tested."));
        CHECK(child<QCheckBox>(page, "test_first")->accessibleDescription() ==
              child<QLabel>(page, "test_first_note")->text());
        CHECK(child<QLabel>(page, "detect_note")->text() == guard_note(f.control.status()));
        CHECK(child<QLabel>(page, "instant_note")->text() == instant_note(f.control.status()));
        CHECK(child<QLabel>(page, "policy_note")->text() == policy_note(PolicyKind::Disable));
        CHECK(ui::audit(&page).isEmpty());
    }

    TEST_CASE("Sharing: the library, who's in it, conflicts, and remote control")
    {
        Fixture f;
        auto now = std::filesystem::file_time_type::clock::now();
        f.control.people = { { "erik", now - std::chrono::minutes(5), 4 }, { "mikael", now, 6 } };
        f.control.conflict_list = { "calls.erik.sync-conflict-20261102-201512-ABCDEFG.json" };
        auto &page = *f.workshop.sharingPage();
        page.refresh(f.control.status());
        auto players = child<QListWidget>(page, "players");
        REQUIRE(players->count() == 2);
        CHECK(players->item(0)->text() == "erik · last change 5 minutes ago");
        CHECK(players->item(1)->text().startsWith("mikael (you)"));
        CHECK_FALSE(child<QWidget>(page, "conflicts")->isHidden());
        bool changed = false;
        page.libraryChanged = [&] { changed = true; };
        page.askFolder = [](const QString &) { return QString("/tmp/shared"); };
        child<QPushButton>(page, "choose_library")->click();
        CHECK(f.control.calls.back() == "library /tmp/shared");
        CHECK(changed);
        // Off: only the switch and what it means; the address and the link come with it on.
        CHECK(child<QWidget>(page, "remote_details")->isHidden());
        child<QCheckBox>(page, "remote")->click();
        CHECK(f.control.calls.back() == "remote on");
        f.control.remote_state.enabled = true;
        page.refresh(f.control.status());
        CHECK_FALSE(child<QWidget>(page, "remote_details")->isHidden());
        auto address = child<QLineEdit>(page, "remote_address");
        address->setText("https://example.invalid");
        address->editingFinished();
        CHECK(f.control.calls.back() == "remote address https://example.invalid");
        f.control.conflict_list.clear();
        page.refresh(f.control.status());
    }

    TEST_CASE("nothing a screen reader would miss, on any page")
    {
        Fixture f;
        f.control.running(2);
        f.control.s.kept = { { KeptItem::Kind::Switch, 0x8000, "gravity off", "" } };
        f.control.moment_list = { { "/tmp/library/POCKY/moments/a.s9xmoment", "Gravity mess, 4 Nov 21.13", "Gravity mess",
                                    "mikael", "2026-11-04 21:13:05", "", 14.2, {} } };
        for (auto page : { WorkshopPage::Recipe, WorkshopPage::Functions, WorkshopPage::Memory, WorkshopPage::Rom,
                           WorkshopPage::Safety, WorkshopPage::Sharing, WorkshopPage::Moments,
                           WorkshopPage::Explored })
        {
            f.workshop.showPage(page);
            f.workshop.refresh();
        }
        CHECK(ui::audit(&f.workshop).isEmpty());
        f.workshop.close();
    }

    TEST_CASE("no Refresh buttons: every page keeps itself fresh")
    {
        Fixture f;
        CHECK(f.workshop.findChildren<QPushButton *>("refresh").isEmpty());
    }

    TEST_CASE("closing it tells the dock which page it was on and how big it was, to open it so again")
    {
        Fixture f;
        std::optional<std::pair<WorkshopPage, QSize>> told;
        f.workshop.onClosed = [&](WorkshopPage page, QSize size) { told = { page, size }; };
        f.workshop.showPage(WorkshopPage::Safety);
        f.workshop.resize(1500, 850);
        f.workshop.close();
        REQUIRE(told);
        CHECK(told->first == WorkshopPage::Safety);
        CHECK(told->second == QSize(1500, 850));
    }

    TEST_CASE("Functions: shown again, it re-sorts busiest first and picks up new functions")
    {
        Fixture f;
        f.workshop.showPage(WorkshopPage::Functions);
        auto table = child<QTableWidget>(*f.workshop.functionsPage(), "functions");
        int before = table->rowCount();
        f.functions.add_busy(0xC000);
        f.workshop.showPage(WorkshopPage::Safety);
        f.workshop.showPage(WorkshopPage::Functions);
        REQUIRE(table->rowCount() == before + 1);
        CHECK(table->item(0, 0)->text().contains("C000")); // the busiest now is first
        f.workshop.close();
    }

    TEST_CASE("sidebar notes: a few words per page, only when there's something to say")
    {
        PanelStatus s;
        s.game_loaded = true;
        auto notes = sidebar_notes(s, 0, false);
        for (auto &n : notes)
            CHECK(n.text.isEmpty()); // a fresh game: nothing to say anywhere
        s.has_recipe = true;
        s.rules.resize(3);
        s.functions_off = 2;
        s.patched = 40;
        s.rom_changed = 14;
        s.crashes = 1;
        notes = sidebar_notes(s, 2, false);
        CHECK(notes[size_t(WorkshopPage::Recipe)].text == "3 rules");
        CHECK(notes[size_t(WorkshopPage::Functions)].text == "2 off");
        CHECK(notes[size_t(WorkshopPage::Memory)].text == "40 painted");
        CHECK(notes[size_t(WorkshopPage::Rom)].text == "14 changed");
        CHECK(notes[size_t(WorkshopPage::Safety)].text == "1 crash");
        CHECK(notes[size_t(WorkshopPage::Safety)].warning);
        CHECK_FALSE(notes[size_t(WorkshopPage::Rom)].warning);
        CHECK(notes[size_t(WorkshopPage::Sharing)].text == "2 here");
        s.rules.resize(1);
        s.edited = true;
        s.recipe_path = "/tmp/library/recipes/x.json";
        s.crashes = 0;
        s.detection_enabled = false;
        notes = sidebar_notes(s, 0, true);
        CHECK(notes[size_t(WorkshopPage::Recipe)].text == "1 rule · unsaved");
        CHECK(notes[size_t(WorkshopPage::Safety)].text == "guard off");
        CHECK(notes[size_t(WorkshopPage::Safety)].warning);
        CHECK(notes[size_t(WorkshopPage::Sharing)].text == "remote on");
    }

    TEST_CASE("the sidebar shows the notes, and they follow the session whichever page is open")
    {
        Fixture f;
        f.control.running(3);
        f.control.s.functions_off = 2;
        f.workshop.showPage(WorkshopPage::Memory);
        f.workshop.refresh();
        auto sidebar = child<QListWidget>(f.workshop, "workshop_pages");
        CHECK(sidebar->item(int(WorkshopPage::Functions))->data(Workshop::NoteRole).toString() == "2 off");
        CHECK(sidebar->item(int(WorkshopPage::Recipe))->data(Workshop::NoteRole).toString() == "3 rules");
        CHECK(sidebar->item(int(WorkshopPage::Functions))->text() == "Functions"); // the name stays as it was
        CHECK(sidebar->item(int(WorkshopPage::Functions))->data(Qt::AccessibleTextRole).toString().contains("2 off"));
        f.control.s.functions_off = 0;
        f.workshop.refresh();
        CHECK(sidebar->item(int(WorkshopPage::Functions))->data(Workshop::NoteRole).toString().isEmpty());
        // Wide enough for a name and its longest note.
        auto fm = sidebar->fontMetrics();
        CHECK(sidebar->width() >= fm.horizontalAdvance("Functions") + fm.horizontalAdvance("1,234 off") + 18);
        f.workshop.close();
    }

    TEST_CASE("a wide window: text pages keep a readable width, Functions and Memory fill it")
    {
        Fixture f;
        f.control.running(3);
        f.workshop.resize(1600, 800);
        f.workshop.show();
        for (auto page : { WorkshopPage::Recipe, WorkshopPage::Rom, WorkshopPage::Safety, WorkshopPage::Sharing })
        {
            f.workshop.showPage(page);
            f.workshop.refresh();
            QApplication::processEvents();
            auto content = f.workshop.page() == WorkshopPage::Recipe ? static_cast<QWidget *>(f.workshop.recipePage())
                           : page == WorkshopPage::Rom              ? static_cast<QWidget *>(f.workshop.romPage())
                           : page == WorkshopPage::Safety           ? static_cast<QWidget *>(f.workshop.safetyPage())
                                                                    : static_cast<QWidget *>(f.workshop.sharingPage());
            INFO("page " << int(page));
            CHECK(content->width() <= Workshop::readableWidth(content));
            CHECK(content->width() < 1100);
        }
        f.workshop.showPage(WorkshopPage::Memory);
        QApplication::processEvents();
        CHECK(f.workshop.memoryPage()->width() > 1200);
        f.workshop.close();
    }

    TEST_CASE("always a recipe (#74): the Workshop names the one it changes, on every page, and its state")
    {
        Fixture f; // a blank Untitled
        f.workshop.show();
        f.workshop.refresh();
        auto name = child<QLabel>(f.workshop, "workshop_recipe");
        auto state = child<QLabel>(f.workshop, "workshop_recipe_state");
        CHECK(name->text() == "Untitled");
        CHECK(state->text() == "Empty · not saved");
        CHECK(f.workshop.windowTitle() == "Untitled[*] — Corruption Workshop");
        CHECK_FALSE(f.workshop.isWindowModified()); // nothing in it to lose
        CHECK(child<QLabel>(*f.workshop.recipePage(), "recipe_notes")->text().contains("lands in this recipe"));
        CHECK(child<QPushButton>(*f.workshop.recipePage(), "add_rule")->isEnabled()); // no "make a recipe first"
        CHECK_FALSE(child<QPushButton>(*f.workshop.recipePage(), "save")->isEnabled()); // nothing in it to keep
        CHECK(sidebar_notes(f.control.status(), 0, false)[0].text.isEmpty()); // not "0 rules"

        f.workshop.showPage(WorkshopPage::Functions); // the header stays whatever the page
        f.control.s.blank = false; // a function turned off
        f.control.s.edited = true;
        f.workshop.refresh();
        CHECK(name->text() == "Untitled");
        CHECK(state->text() == "Not saved");
        CHECK(f.workshop.isWindowModified());
        CHECK(sidebar_notes(f.control.status(), 0, false)[0].text == "0 rules · unsaved");

        f.control.running(1); // a saved recipe of the library
        f.control.s.edited = true;
        f.workshop.refresh();
        CHECK(name->text() == "Gravity mess");
        CHECK(state->text() == "Changed since saved");
        CHECK(f.workshop.windowTitle() == "Gravity mess[*] — Corruption Workshop");
        f.control.s.edited = false;
        CHECK(recipe_state(f.control.status()) == "Saved");
        f.control.s.recipe_path.clear(); // a starter
        CHECK(recipe_state(f.control.status()) == "Starter · not yours yet");
        f.control.s.game_loaded = false;
        CHECK(recipe_state(f.control.status()) == "No game loaded");
    }

    TEST_CASE("each page says what of it is saved in the recipe (#74)")
    {
        Fixture f;
        CHECK(child<QLabel>(*f.workshop.findChild<QWidget *>("rom_page"), "rom_scope")->text().contains("saved in the recipe"));
        CHECK(child<QLabel>(*f.workshop.findChild<QWidget *>("safety_page"), "safety_scope")->text().contains("not saved in a recipe"));
        auto labels = f.workshop.findChildren<QLabel *>();
        CHECK(std::any_of(labels.begin(), labels.end(),
                          [](QLabel *l) { return l->text().contains("names are shared with everyone"); }));
        CHECK(std::any_of(labels.begin(), labels.end(),
                          [](QLabel *l) { return l->text().contains("Nothing on this page is part of a recipe"); }));
    }

    TEST_CASE("an Untitled recipe with something in it asks before New; a blank one doesn't; Save asks its name (#74)")
    {
        Fixture f;
        auto &page = *f.workshop.recipePage();
        page.refresh(f.control.status());
        int asked = 0;
        f.actions.askUnsaved = [&](const QString &, const QString &) {
            asked++;
            return RecipeActions::Unsaved::Cancel;
        };
        f.control.s.edited = true; // a re-roll alone: still blank
        child<QPushButton>(page, "new_recipe")->click();
        CHECK(asked == 0);
        CHECK(f.control.calls.back() == "new ");

        f.control.s.blank = false; // paint, no rules
        size_t before = f.control.calls.size();
        child<QPushButton>(page, "new_recipe")->click();
        CHECK(asked == 1);
        CHECK(f.control.calls.size() == before); // cancelled

        QString offered;
        f.actions.askText = [&](const QString &, const QString &, const QString &text) -> std::optional<QString> {
            offered = text;
            return QString("Floaty");
        };
        page.refresh(f.control.status());
        child<QPushButton>(page, "save")->click();
        CHECK(offered == "My recipe"); // not "Untitled"
        CHECK(f.control.calls.back() == "save as Floaty");
    }

    TEST_CASE("New Recipe with unsaved changes: Save, Don't Save, or Cancel and nothing happens")
    {
        Fixture f;
        f.control.running(1);
        f.control.s.engine.recipe_name = "Testing1";
        f.control.s.edited = true;
        auto &page = *f.workshop.recipePage();
        page.refresh(f.control.status());
        auto new_button = child<QPushButton>(page, "new_recipe");
        CHECK(new_button->text() == "New Recipe"); // no dialog of its own any more (#74)
        QString asked;
        int named = 0;
        f.actions.askText = [&](const QString &, const QString &, const QString &) -> std::optional<QString> {
            named++;
            return QString("Fresh");
        };
        f.actions.askUnsaved = [&](const QString &, const QString &name) {
            asked = name;
            return RecipeActions::Unsaved::Cancel;
        };
        size_t before = f.control.calls.size();
        new_button->click();
        CHECK(asked == "Testing1");
        CHECK(named == 0); // never asked for a name (#74)
        CHECK(f.control.calls.size() == before);

        f.actions.askUnsaved = [](const QString &, const QString &) { return RecipeActions::Unsaved::Discard; };
        new_button->click();
        CHECK(f.control.calls.back() == "new ");
        CHECK(std::find(f.control.calls.begin() + long(before), f.control.calls.end(), "save") == f.control.calls.end());

        f.control.s.edited = true;
        f.actions.askUnsaved = [&](const QString &, const QString &) {
            return RecipeActions::Unsaved::Save;
        };
        before = f.control.calls.size();
        new_button->click();
        REQUIRE(f.control.calls.size() >= before + 2);
        CHECK(f.control.calls[before] == "save"); // kept first, then the new one
        CHECK(f.control.calls.back() == "new ");
    }
}


TEST_CASE("the Workshop can be made narrow: no page needs more than about 900 px")
{
    Fixture f;
    f.control.running(3);
    f.workshop.show();
    for (auto page : { WorkshopPage::Recipe, WorkshopPage::Functions, WorkshopPage::Memory, WorkshopPage::Rom,
                       WorkshopPage::Safety, WorkshopPage::Sharing, WorkshopPage::Moments,
                           WorkshopPage::Explored })
    {
        f.workshop.showPage(page);
        QApplication::processEvents();
        INFO("page " << int(page));
        CHECK(f.workshop.minimumSizeHint().width() <= 900); // was 1,107 (Memory's toolbar and list)
        CHECK(f.workshop.minimumSizeHint().height() <= 520);
        // At its smallest, nothing is cut off at the right: every page gets what it needs.
        f.workshop.resize(f.workshop.minimumSizeHint());
        QApplication::processEvents();
        for (QWidget *content : { static_cast<QWidget *>(f.workshop.recipePage()), static_cast<QWidget *>(f.workshop.functionsPage()),
                                  static_cast<QWidget *>(f.workshop.memoryPage()), static_cast<QWidget *>(f.workshop.romPage()),
                                  static_cast<QWidget *>(f.workshop.safetyPage()), static_cast<QWidget *>(f.workshop.sharingPage()),
                                  static_cast<QWidget *>(f.workshop.momentsPage()), static_cast<QWidget *>(f.workshop.exploredPage()) })
            if (content->isVisible())
                CHECK(content->width() >= content->minimumSizeHint().width());
    }
    f.workshop.close();
}

TEST_CASE("Moments (#47): the kept moments, newest first; play, rename, delete; an empty list says what to do")
{
    Fixture f;
    f.control.running(2);
    f.workshop.showPage(WorkshopPage::Moments);
    f.workshop.refresh();
    auto &page = *f.workshop.momentsPage();
    auto table = child<QTableWidget>(page, "moments");
    auto empty = child<QLabel>(page, "moments_empty");
    CHECK(!table->isVisibleTo(&page));
    CHECK(empty->isVisibleTo(&page));
    CHECK(empty->text().contains("Keep This Moment"));
    CHECK(!child<QPushButton>(page, "play_moment")->isVisibleTo(&page));
    f.shot("workshop-moments-empty");

    f.control.moment_list = {
        { "/tmp/library/POCKY/moments/the-hud-eats-max.s9xmoment", "The HUD eats Max", "Gravity mess", "erik",
          "2026-11-04 21:20:00", "", 18.6, {} },
        { "/tmp/library/POCKY/moments/gravity-mess-4-nov-21-13.s9xmoment", "Gravity mess, 4 Nov 21.13", "Gravity mess",
          "mikael", "2026-11-04 21:13:05", "the crash guard rolled back", 4.6, {} },
    };
    f.control.s.keeps = 1; // a Keep reads the list again
    f.workshop.refresh();
    REQUIRE(table->isVisibleTo(&page));
    CHECK(!empty->isVisibleTo(&page));
    REQUIRE(table->rowCount() == 2);
    CHECK(table->item(0, 0)->text() == "The HUD eats Max");
    CHECK(table->item(0, 1)->text() == "18.6 s");
    CHECK(table->item(1, 2)->text() == "mikael · 2026-11-04 21:13");
    CHECK(table->currentRow() == 0); // the newest is picked
    auto details = child<QLabel>(page, "moment_details");
    CHECK(details->text() == "From the recipe Gravity mess, kept by erik.");
    table->selectRow(1);
    CHECK(details->text().contains("It starts where the crash guard rolled back"));
    f.shot("workshop-moments");

    child<QPushButton>(page, "play_moment")->click();
    CHECK(f.control.calls.back() == "play gravity-mess-4-nov-21-13.s9xmoment");
    CHECK(child<QWidget>(page, "moments_message")->isVisibleTo(&page));

    // Rename: the name cell is edited in place.
    table->selectRow(0);
    child<QPushButton>(page, "rename_moment")->click();
    table->item(0, 0)->setText("Max walks on the ceiling");
    CHECK(f.control.calls.back() == "rename the-hud-eats-max.s9xmoment to Max walks on the ceiling");
    CHECK(table->item(0, 0)->text() == "Max walks on the ceiling");

    page.confirm = [](const QString &, const QString &) { return false; };
    child<QPushButton>(page, "delete_moment")->click();
    CHECK(f.control.calls.back().rfind("rename", 0) == 0); // asked, and no
    page.confirm = [](const QString &, const QString &) { return true; };
    child<QPushButton>(page, "delete_moment")->click();
    CHECK(f.control.calls.back() == "delete the-hud-eats-max.s9xmoment");
    CHECK(table->rowCount() == 1);
    CHECK(ui::audit(&page).isEmpty());

    // While one plays, the sidebar says so.
    f.control.s.moment_playing = true;
    CHECK(sidebar_notes(f.control.status(), 0, false)[size_t(WorkshopPage::Moments)].text == "playing");
    f.workshop.close();
}

TEST_CASE("Explored (#46): an exploration's candidates, best first, with their pictures; play, keep, delete; the explorer running")
{
    Fixture f;
    f.control.running(2);
    f.workshop.showPage(WorkshopPage::Explored);
    f.workshop.refresh();
    auto &page = *f.workshop.exploredPage();
    auto empty = child<QLabel>(page, "explored_empty");
    CHECK(empty->isVisibleTo(&page));
    CHECK(empty->text().contains("Explore"));
    CHECK(!child<QTableWidget>(page, "candidates")->isVisibleTo(&page));
    f.shot("workshop-explored-empty");

    // An exploration on disk: candidates.json's contents, a strip, a gallery.
    QTemporaryDir tmp;
    std::filesystem::path dir = tmp.path().toStdString() + "/The HUD eats Max 05 Nov 21.13";
    std::filesystem::create_directories(dir);
    Exploration e;
    e.name = "The HUD eats Max 05 Nov 21.13";
    e.moment = "The HUD eats Max";
    e.made_by = "mikael";
    e.made_at = "2026-11-05 21:13:00";
    e.window = 1200;
    e.variants = 1800;
    e.alive = 1650;
    e.visible = 310;
    e.finished = true;
    for (uint32_t i = 0; i < 3; i++)
    {
        Candidate c;
        c.rank = i + 1;
        c.id = 40 + i;
        c.family = i == 0 ? Family::Kill : Family::Code;
        c.what = i == 0 ? "rom: turn busy functions off x1, once at frame 121" : "rom: flip branches x1, every 60 frames";
        c.recipe.name = "Explored " + std::string(family_name(c.family)) + " " + std::to_string(c.id + 1);
        c.score.score = 1.5 - 0.3 * i;
        c.score.different = 0.8;
        c.score.detail = 0.9;
        c.score.motion = 1.0;
        c.score.new_code = 300;
        c.moment_file = std::to_string(c.id + 1) + " " + family_name(c.family) + ".s9xmoment";
        c.strip_file = std::to_string(c.id + 1) + " " + family_name(c.family) + ".png";
        c.exact = true;
        QImage strip(1556, 224, QImage::Format_RGB32);
        strip.fill(QColor(40 + 60 * int(i), 120, 200));
        strip.save(QString::fromStdString((dir / c.strip_file).string()));
        e.candidates.push_back(c);
    }
    std::ofstream(dir / "gallery.html") << "<html></html>";
    f.control.exploration_list = { { dir, e } };
    page.reload();
    auto table = child<QTableWidget>(page, "candidates");
    REQUIRE(table->isVisibleTo(&page));
    CHECK(!empty->isVisibleTo(&page));
    REQUIRE(table->rowCount() == 3);
    CHECK(table->item(0, 0)->text() == "1");
    CHECK(table->item(0, 1)->text() == "kill");
    CHECK(table->item(1, 3)->text() == "1.20");
    CHECK(table->item(0, 3)->toolTip().contains("300 bytes of code"));
    CHECK(child<QLabel>(page, "exploration_summary")->text().contains("1,800 variants"));
    CHECK(table->currentRow() == 0); // the best is picked
    auto strip = child<QLabel>(page, "candidate_strip");
    CHECK(strip->isVisibleTo(&page));
    CHECK(!strip->pixmap().isNull());
    CHECK(child<QLabel>(page, "candidate_details")->text().contains("plays back exactly"));
    CHECK(child<QPushButton>(page, "open_gallery")->isEnabled());
    f.shot("workshop-explored");

    table->selectRow(1);
    child<QPushButton>(page, "play_candidate")->click();
    CHECK(f.control.calls.back() == "play 42 code.s9xmoment");
    child<QPushButton>(page, "keep_candidate")->click();
    CHECK(f.control.calls.back() == "keep explored The HUD eats Max 05 Nov 21.13 42 code.s9xmoment from The HUD eats Max");
    CHECK(child<QWidget>(page, "explored_message")->isVisibleTo(&page));

    page.confirm = [](const QString &, const QString &) { return false; };
    child<QPushButton>(page, "delete_exploration")->click();
    CHECK(f.control.calls.back().rfind("keep explored", 0) == 0); // asked, and no
    page.confirm = [](const QString &, const QString &) { return true; };
    child<QPushButton>(page, "delete_exploration")->click();
    CHECK(f.control.calls.back() == "delete exploration The HUD eats Max 05 Nov 21.13");
    CHECK(empty->isVisibleTo(&page));
    CHECK(ui::audit(&page).isEmpty());

    // The explorer running: a stand-in that prints what snes9x-explore prints.
    auto script = tmp.path() + "/fake-explore.sh";
    {
        QFile out(script);
        REQUIRE(out.open(QIODevice::WriteOnly));
        out.write("#!/bin/sh\necho \"folder /tmp/x\"\necho \"progress 120 101 14 1.31\"\nsleep 2\necho \"done     120 variants\"\n");
    }
    QFile::setPermissions(script, QFile::permissions(script) | QFileDevice::ExeOwner);
    auto &runner = *f.workshop.exploreRunner();
    runner.setTool(script);
    CHECK(runner.start("/tmp/pocky.sfc", "/tmp/m.s9xmoment", "The HUD eats Max", "/tmp/library", "mikael", 60).isEmpty());
    CHECK(!runner.start("/tmp/pocky.sfc", "/tmp/m.s9xmoment", "again", "/tmp/library", "mikael", 60).isEmpty()); // one at a time
    QElapsedTimer t;
    t.start();
    while (runner.state().tried == 0 && t.elapsed() < 3000)
        QApplication::processEvents(QEventLoop::AllEvents, 50);
    CHECK(runner.state().tried == 120);
    CHECK(runner.state().visible == 14);
    CHECK(runner.state().folder == "/tmp/x");
    f.workshop.refresh();
    auto running = child<QLabel>(page, "explore_running_text");
    CHECK(running->isVisibleTo(&page));
    CHECK(running->text().contains("120 tried"));
    CHECK(child<QPushButton>(page, "explore_stop")->isVisibleTo(&page));
    CHECK(sidebar_notes(f.control.status(), 0, false)[size_t(WorkshopPage::Explored)].text.isEmpty()); // the Workshop adds it
    auto sidebar = child<QListWidget>(f.workshop, "workshop_pages");
    CHECK(sidebar->item(int(WorkshopPage::Explored))->data(Workshop::NoteRole).toString() == "exploring");
    f.shot("workshop-explored-running");
    while (runner.state().running && t.elapsed() < 6000)
        QApplication::processEvents(QEventLoop::AllEvents, 50);
    CHECK(!runner.state().running);
    CHECK(runner.state().done == "120 variants");
    f.workshop.close();
}

TEST_CASE("Moments: Explore starts the explorer on the picked moment (#45)")
{
    Fixture f;
    f.control.running(2);
    f.control.moment_list = { { "/tmp/library/POCKY/moments/a.s9xmoment", "The HUD eats Max", "", "mikael",
                                "2026-11-04 21:13:05", "", 14.2, {} } };
    f.workshop.showPage(WorkshopPage::Moments);
    f.workshop.refresh();
    auto &page = *f.workshop.momentsPage();
    auto explore = child<QPushButton>(page, "explore_moment");
    CHECK(explore->isVisibleTo(&page));
    CHECK(explore->isEnabled());
    CHECK(explore->toolTip().contains("4,000 tries an hour"));
    f.workshop.close();
}
