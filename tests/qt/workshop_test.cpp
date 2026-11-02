#include "doctest/doctest.h"
#include "FunctionsPage.hpp"
#include "MemoryViewer.hpp"
#include "RecipeActions.hpp"
#include "UiKit.hpp"
#include "Workshop.hpp"
#include "WorkshopPages.hpp"
#include "fake_control.h"
#include "fake_functions.h"

#include <QApplication>
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
        child<QCheckBox>(page, "remote")->click();
        CHECK(f.control.calls.back() == "remote on");
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
}
