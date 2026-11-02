#include "doctest/doctest.h"
#include "ButtonsBar.hpp"
#include "Cockpit.hpp"
#include "RecipeActions.hpp"
#include "UiKit.hpp"
#include "fake_control.h"

#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
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

std::vector<Starter> starters()
{
    Starter gentle;
    gentle.resource = ":/x/gentle.json";
    gentle.recipe.name = "Gentle noise";
    Rule all_rom;
    all_rom.name = "everywhere";
    all_rom.range = { Target::ROM, 0, 0x7FFFFF }; // "the whole game"
    gentle.recipe.rules.push_back(all_rom);
    return { gentle };
}

struct Fixture
{
    FakeControl control;
    QWidget parent;
    RecipeActions actions{ control, &parent, starters() };
    std::unique_ptr<Cockpit> cockpit;
    std::vector<WorkshopPage> opened;
    std::vector<std::pair<QString, bool>> said;

    Fixture()
    {
        actions.say = [this](const QString &t, bool e) { said.push_back({ t, e }); };
        actions.confirm = [](const QString &, const QString &) { return true; };
        build();
    }
    void build()
    {
        cockpit = std::make_unique<Cockpit>(control, actions);
        cockpit->openWorkshop = [this](WorkshopPage p) { opened.push_back(p); };
        cockpit->setToggleKey("F7");
        cockpit->resize(360, 700);
        cockpit->refresh();
    }
    void shot(const char *name)
    {
        if (auto dir = qgetenv("CORRUPT_SHOTS_DIR"); !dir.isEmpty())
        {
            QDir().mkpath(QString::fromLocal8Bit(dir));
            cockpit->show();
            cockpit->grab().save(QString("%1/%2.png").arg(QString::fromLocal8Bit(dir), name));
        }
    }
};

} // namespace

TEST_SUITE("cockpit")
{
    TEST_CASE("with no recipe: where to start, and nothing to press yet")
    {
        Fixture f;
        auto &c = *f.cockpit;
        CHECK(child<QLabel>(c, "game")->text().contains("POCKY"));
        CHECK_FALSE(child<QLabel>(c, "start_here")->isHidden());
        CHECK(child<QLabel>(c, "start_here")->text().contains("F7"));
        CHECK_FALSE(child<QPushButton>(c, "corrupt")->isEnabled());
        CHECK(child<QWidget>(c, "rules_section")->isHidden());
        CHECK(child<QWidget>(c, "buttons_section")->isHidden());
        CHECK(ui::audit(&c).isEmpty());
        f.shot("cockpit-no-recipe");
    }

    TEST_CASE("running: Stop, Back to Normal, Chaos and the dice act on the session")
    {
        Fixture f;
        f.control.running();
        f.cockpit->refresh();
        auto &c = *f.cockpit;
        CHECK(child<QLabel>(c, "start_here")->isHidden());
        auto corrupt = child<QPushButton>(c, "corrupt");
        CHECK(corrupt->text() == "Stop (F7)");
        corrupt->click();
        CHECK(f.control.calls.back() == "off");
        CHECK(corrupt->text() == "Corrupt (F7)");
        CHECK_FALSE(child<QPushButton>(c, "back_to_normal")->isEnabled()); // nothing to undo yet
        f.control.s.can_go_back = true;
        f.cockpit->refresh();
        child<QPushButton>(c, "back_to_normal")->click();
        CHECK(f.control.calls.back() == "back to normal");
        child<QPushButton>(c, "reroll")->click();
        CHECK(f.control.calls.back() == "reroll");
        CHECK(child<QLineEdit>(c, "seed")->text() == "1008");
        child<QPushButton>(c, "mark_crash")->click();
        CHECK(f.control.calls.back() == "mark crash");
    }

    TEST_CASE("rules: on and strength only, and unticking one turns it off")
    {
        Fixture f;
        f.control.running(3);
        f.cockpit->refresh();
        auto table = child<QTableWidget>(*f.cockpit, "rules");
        REQUIRE(table->rowCount() == 3);
        CHECK(table->columnCount() == 3);
        CHECK(table->item(1, 1)->text() == "rule 2");
        CHECK(table->item(1, 1)->toolTip().contains("wram")); // what it does, on hover
        table->item(1, 0)->setCheckState(Qt::Unchecked);
        CHECK(f.control.calls.back() == "rule 1 off");
    }

    TEST_CASE("the recipe's buttons show, named, and a press reaches the session")
    {
        Fixture f;
        f.control.running();
        f.control.s.buttons = { { 2, "gravity off", "gravity off", true, false }, { 5, "Burst", "Burst", false, false } };
        f.cockpit->refresh();
        CHECK_FALSE(child<QWidget>(*f.cockpit, "buttons_section")->isHidden());
        child<QPushButton>(*f.cockpit, "button_2")->click();
        CHECK(f.control.calls.back() == "button 2");
        CHECK(child<QPushButton>(*f.cockpit, "button_5")->text() == "Burst");
    }

    TEST_CASE("the recipe list: the game's recipes, then the starters; choosing one loads it")
    {
        Fixture f;
        RecipeEntry mine;
        mine.path = "/tmp/library/recipes/moon-jump.json";
        mine.name = "Moon jump";
        mine.recipe = Recipe{};
        mine.recipe->name = "Moon jump";
        f.control.entries = { mine };
        f.build();
        auto combo = child<QComboBox>(*f.cockpit, "recipe");
        int moon = combo->findData("mine:0");
        int gentle = combo->findData("starter:0");
        REQUIRE(moon > 0);
        REQUIRE(gentle > moon);
        CHECK(combo->itemText(moon) == "Moon jump");
        CHECK(combo->itemText(gentle) == "Starter: Gentle noise");
        combo->activated(gentle);
        CHECK(f.control.calls.back() == "load Gentle noise");
        CHECK(f.control.last_loaded.game.title == "POCKY & ROCKY");
        CHECK(f.control.last_loaded.rules[0].range.last == 0xFFFFF); // fitted to this ROM
        combo->activated(moon);
        CHECK(f.control.calls.back() == "load Moon jump from moon-jump.json");
    }

    TEST_CASE("one line each about the guard and the ROM, linking to their Workshop pages")
    {
        Fixture f;
        f.control.running();
        f.control.s.crashes = 2;
        f.control.s.rom_changed = 120;
        f.control.s.functions_off = 3;
        f.cockpit->refresh();
        auto guard = child<QLabel>(*f.cockpit, "guard_line");
        CHECK(guard->text().contains("2 crashes caught"));
        auto rom = child<QLabel>(*f.cockpit, "rom_line");
        CHECK(rom->text().contains("120 bytes changed"));
        CHECK(rom->text().contains("3 functions off"));
        guard->linkActivated("safety");
        rom->linkActivated("rom");
        child<QLabel>(*f.cockpit, "rules_link")->linkActivated("recipe");
        child<QPushButton>(*f.cockpit, "workshop")->click();
        CHECK(f.opened == std::vector<WorkshopPage>{ WorkshopPage::Safety, WorkshopPage::Rom, WorkshopPage::Recipe,
                                                     WorkshopPage::Recipe });

        f.control.s.detection_enabled = false;
        f.cockpit->refresh();
        CHECK(guard->text().contains("Crash guard off"));
    }

    TEST_CASE("it fits: three rules and four buttons need no scrolling in a short window")
    {
        Fixture f;
        f.control.running(3);
        f.control.s.buttons = { { 1, "gravity off", "", true, true },
                                { 2, "jump +3", "", true, false },
                                { 3, "Burst", "", false, false },
                                { 4, "scroll changed", "", true, false } };
        f.control.s.rom_changed = 12;
        f.cockpit->refresh();
        f.cockpit->adjustSize();
        CHECK(f.cockpit->sizeHint().height() <= 640);
        CHECK(ui::audit(f.cockpit.get()).isEmpty());
        f.shot("cockpit-running");
    }
}
