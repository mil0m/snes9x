#include "doctest/doctest.h"
#include "FunctionsPage.hpp"
#include "IntensitySlider.hpp"
#include "MemoryViewer.hpp"
#include "RecipeActions.hpp"
#include "UiKit.hpp"
#include "Workshop.hpp"
#include "WorkshopPages.hpp"
#include "fake_control.h"
#include "fake_functions.h"

#include <QApplication>
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
    TEST_CASE("six pages in the sidebar; showPage switches to one")
    {
        Fixture f;
        auto sidebar = child<QListWidget>(f.workshop, "workshop_pages");
        REQUIRE(sidebar->count() == 6);
        QStringList names;
        for (int i = 0; i < sidebar->count(); i++)
            names << sidebar->item(i)->text();
        CHECK(names == QStringList{ "Recipe", "Functions", "Memory", "ROM", "Safety", "Sharing" });
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
        f.actions.askText = [](const QString &, const QString &, const QString &) -> std::optional<QString> {
            return QString("Fresh");
        };
        child<QPushButton>(page, "new_recipe")->click();
        CHECK(f.control.calls.back() == "new Fresh");
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
        for (auto page : { WorkshopPage::Recipe, WorkshopPage::Functions, WorkshopPage::Memory, WorkshopPage::Rom,
                           WorkshopPage::Safety, WorkshopPage::Sharing })
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

    TEST_CASE("New Recipe with unsaved changes: Save, Don't Save, or Cancel and nothing happens")
    {
        Fixture f;
        f.control.running(1);
        f.control.s.engine.recipe_name = "Testing1";
        f.control.s.edited = true;
        auto &page = *f.workshop.recipePage();
        page.refresh(f.control.status());
        auto new_button = child<QPushButton>(page, "new_recipe");
        CHECK(new_button->text() == "New Recipe…");
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
        CHECK(named == 0); // cancelled: not even asked for a name
        CHECK(f.control.calls.size() == before);

        f.actions.askUnsaved = [](const QString &, const QString &) { return RecipeActions::Unsaved::Discard; };
        new_button->click();
        CHECK(f.control.calls.back() == "new Fresh");
        CHECK(std::find(f.control.calls.begin() + long(before), f.control.calls.end(), "save") == f.control.calls.end());

        f.control.s.edited = true;
        f.actions.askUnsaved = [&](const QString &, const QString &) {
            return RecipeActions::Unsaved::Save;
        };
        before = f.control.calls.size();
        new_button->click();
        REQUIRE(f.control.calls.size() >= before + 2);
        CHECK(f.control.calls[before] == "save"); // kept first, then the new one
        CHECK(f.control.calls.back() == "new Fresh");
    }
}


TEST_CASE("the Workshop can be made narrow: no page needs more than about 900 px")
{
    Fixture f;
    f.control.running(3);
    f.workshop.show();
    for (auto page : { WorkshopPage::Recipe, WorkshopPage::Functions, WorkshopPage::Memory, WorkshopPage::Rom,
                       WorkshopPage::Safety, WorkshopPage::Sharing })
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
                                  static_cast<QWidget *>(f.workshop.safetyPage()), static_cast<QWidget *>(f.workshop.sharingPage()) })
            if (content->isVisible())
                CHECK(content->width() >= content->minimumSizeHint().width());
    }
    f.workshop.close();
}
