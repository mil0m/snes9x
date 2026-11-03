#include "doctest/doctest.h"
#include "ButtonsBar.hpp"
#include "Cockpit.hpp"
#include "IntensitySlider.hpp"
#include "RecipeActions.hpp"
#include "UiKit.hpp"
#include "fake_control.h"

#include <QApplication>
#include <algorithm>
#include <QComboBox>
#include <QElapsedTimer>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
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
    int reopened = 0; // the Workshop button: the page it was on last
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
        cockpit->openWorkshopAgain = [this] { reopened++; };
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

    TEST_CASE("running: Stop, Back to Normal and the dice act on the session")
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
        auto seed = child<QLineEdit>(c, "seed");
        CHECK(seed->text() == "1008");
        CHECK_FALSE(seed->hasFrame()); // a quiet number you can click and type over
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

    TEST_CASE("Strength, all rules: named like each rule's Strength, and every rule slider says what it means (#73)")
    {
        Fixture f;
        f.control.running(2);
        f.control.s.rules[0].op = { .type = OpType::Add, .min = -2, .max = 2 };
        f.control.s.rules[0].count = 8;
        f.cockpit->refresh();
        auto &c = *f.cockpit;
        auto all = child<QWidget>(c, "chaos");
        CHECK(all->accessibleName() == "Strength, all rules");
        auto labels = c.findChildren<QLabel *>();
        CHECK(std::any_of(labels.begin(), labels.end(), [](QLabel *l) { return l->text() == "Strength, all rules"; }));
        CHECK_FALSE(std::any_of(labels.begin(), labels.end(), [](QLabel *l) { return l->text().contains("Chaos"); }));
        CHECK(all->toolTip().contains("50% with this at 50% hits at 25%"));

        auto table = child<QTableWidget>(c, "rules");
        auto rule = dynamic_cast<IntensitySlider *>(table->cellWidget(0, 2));
        REQUIRE(rule);
        CHECK(rule->toolTip() == "At 100%: changes 8 bytes each time it fires, adding -2..2.");
        // Moving the all-rules slider: the rule's words follow at once.
        all->findChild<QSlider *>()->setValue(50);
        CHECK(f.control.calls.back() == "chaos 50");
        CHECK(rule->toolTip().startsWith("At 50% (100% × 50% for all rules): changes 4 of its 8 bytes"));
        // The session moving it (the remote page): the words follow on the next refresh.
        f.control.s.intensity = 0.25;
        f.cockpit->refresh();
        CHECK(rule->toolTip().startsWith("At 25% (100% × 25% for all rules): changes 2 of its 8 bytes"));
        // Moving the rule's own slider.
        rule->findChild<QSlider *>()->setValue(50);
        CHECK(rule->toolTip().startsWith("At 12.5% (50% × 25% for all rules): changes 1 of its 8 bytes"));
        CHECK(rule->findChild<QSlider *>()->accessibleDescription() == rule->toolTip());
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
        CHECK(f.opened == std::vector<WorkshopPage>{ WorkshopPage::Safety, WorkshopPage::Rom, WorkshopPage::Recipe });
        CHECK(f.reopened == 1);
        // Whole lines are the links: no word repeated as a link after them.
        CHECK_FALSE(guard->text().contains(">Safety<"));
        CHECK_FALSE(rom->text().contains(">ROM<"));
        CHECK(guard->accessibleName().contains("Safety"));

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

    TEST_CASE("the state bar says what's going on, in the accent colour while corrupting")
    {
        Fixture f;
        auto state = child<QLabel>(*f.cockpit, "state");
        CHECK(state->text().contains("Pick a recipe"));
        CHECK(state->property("state").toString() == "idle");
        f.control.running();
        f.cockpit->refresh();
        CHECK(state->text() == "Corrupting · 0:10 · 0 changes");
        CHECK(state->property("state").toString() == "corrupting");
        CHECK(state->palette().color(QPalette::Window) == state->palette().color(QPalette::Highlight));
        f.control.s.engine.firings = 1;
        f.control.s.engine.frame = 60 * 75;
        f.cockpit->refresh();
        CHECK(state->text() == "Corrupting · 1:15 · 1 change");
        f.control.s.engine.active = false;
        f.control.s.can_go_back = true;
        f.cockpit->refresh();
        CHECK(state->text().startsWith("Stopped"));
        CHECK(state->text().contains("Back to Normal"));
        CHECK(state->property("state").toString() == "stopped");
        f.control.s.can_go_back = false;
        f.control.s.back_to_normal = true;
        f.cockpit->refresh();
        CHECK(state->text() == "Not corrupting · back to normal");
        CHECK(state->palette().color(QPalette::Window) != f.cockpit->palette().color(QPalette::Window)); // a bar
        f.control.s.game_loaded = false;
        f.cockpit->refresh();
        CHECK(state->text().contains("Load a game"));
        // The old run line's words are gone; only a note about the start moment is left.
        CHECK(f.cockpit->findChild<QLabel *>("run_status") == nullptr);
    }

    TEST_CASE("the start moment note: only when there is one to save or it started from one")
    {
        Fixture f;
        f.control.running();
        f.cockpit->refresh();
        auto note = child<QLabel>(*f.cockpit, "moment_note");
        CHECK(note->isHidden());
        f.control.s.new_moment = true;
        f.cockpit->refresh();
        CHECK_FALSE(note->isHidden());
        CHECK(note->text().contains("Save"));
    }

    TEST_CASE("messages: the short headline, the whole story on hover, and they fade without moving Corrupt")
    {
        Fixture f;
        f.control.running();
        f.cockpit->setMessageFade(30);
        f.cockpit->refresh();
        auto line = static_cast<ui::StatusLine *>(child<QWidget>(*f.cockpit, "message"));
        auto corrupt = child<QPushButton>(*f.cockpit, "corrupt");
        f.cockpit->show();
        QApplication::processEvents();
        int y = corrupt->y();
        f.control.s.message = "Tested ahead: drip would crash, so it picked again";
        f.control.s.message_detail = "Tested ahead: drip crashed the game (jumped into the middle of an instruction at "
                                     "$00:81D1 0.0 s in), so it rolled again";
        f.cockpit->refresh();
        QApplication::processEvents();
        CHECK(line->text() == "Tested ahead: drip would crash, so it picked again");
        CHECK(line->toolTip().contains("$00:81D1"));
        CHECK(corrupt->y() == y);
        QElapsedTimer t;
        t.start();
        while (!line->isHidden() && t.elapsed() < 2000)
            QApplication::processEvents(QEventLoop::AllEvents, 10);
        CHECK(line->isHidden());
        QApplication::processEvents();
        CHECK(corrupt->y() == y);
        f.cockpit->hide();
    }

    TEST_CASE("the title is the game's name from its file; the library folder is on hover")
    {
        Fixture f;
        auto game = child<QLabel>(*f.cockpit, "game");
        CHECK(game->text() == "POCKY & ROCKY"); // no file name: the title in the ROM's header
        CHECK(game->toolTip().contains("[01234567]"));
        f.control.s.rom_name = "Pocky & Rocky (USA)";
        f.control.s.game.sha256 = "fedcba9876543210"; // another game: the label is set again
        f.cockpit->refresh();
        CHECK(game->text() == "Pocky & Rocky (USA)");
        CHECK(game->toolTip().contains("[fedcba98]"));
    }

    TEST_CASE("a recipe that isn't saved yet says (new), short enough to fit")
    {
        Fixture f;
        f.control.running();
        f.control.s.recipe_path.clear();
        f.control.s.recipe_generation++;
        f.cockpit->refresh();
        CHECK(child<QComboBox>(*f.cockpit, "recipe")->currentText() == "Gravity mess (new)");
    }

    TEST_CASE("a game opened paused says how to start it")
    {
        Fixture f;
        f.control.s.paused = true;
        f.cockpit->refresh();
        CHECK(child<QLabel>(*f.cockpit, "state")->text() == "Paused · Emulation > Run starts it");
    }

    TEST_CASE("Back to Normal, or Normal Here: corruption off and the ROM clean, but the game stays where it is")
    {
        Fixture f;
        f.control.running();
        f.cockpit->refresh();
        auto here = child<QPushButton>(*f.cockpit, "normal_here");
        CHECK(here->isEnabled()); // while it runs
        CHECK(here->toolTip().contains("where you are"));
        here->click();
        CHECK(f.control.calls.back() == "normal here");
        f.control.s.engine.active = false;
        f.control.s.can_go_back = false;
        f.control.s.back_to_normal = true;
        f.control.s.normal_here = true;
        f.cockpit->refresh();
        CHECK(child<QLabel>(*f.cockpit, "state")->text() == "Not corrupting · ROM clean, playing on from here");
        CHECK_FALSE(here->isEnabled()); // nothing left to clean
        CHECK(ui::audit(f.cockpit.get()).isEmpty());
    }
}
