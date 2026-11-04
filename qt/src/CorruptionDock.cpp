#include "CorruptionDock.hpp"
#include "Cockpit.hpp"
#include "CorruptionSession.hpp"
#include "EmuApplication.hpp"
#include "EmuConfig.hpp"
#include "FunctionsPage.hpp"
#include "GuideWindow.hpp"
#include "MemoryViewer.hpp"
#include "RecipeActions.hpp"
#include "RemoteControl.hpp"
#include "RuleEditor.hpp"
#include "SessionControl.hpp"
#include "Starters.hpp"
#include "SurvivorsPanel.hpp"
#include "UiKit.hpp"
#include "Workshop.hpp"
#include "WorkshopPages.hpp"
#include "corrupt/rule_text.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTableWidget>
#include <QTimer>

#include <functional>

using namespace corrupt;

namespace
{

QString qs(const std::string &s)
{
    return QString::fromStdString(s);
}

// The memory viewer's window onto the running game.
struct SessionMemorySource : MemorySource
{
    CorruptionSession &s = CorruptionSession::get();
    void watch(Target t) override { s.watchMemory(t); }
    void stop() override { s.stopWatching(); }
    void clearHits() override { s.clearHits(); }
    MemorySnapshot snapshot() override { return s.memorySnapshot(); }
    std::vector<CopyRun> findCopies(Target of) override { return s.findCopies(of); }
    std::vector<MapArea> memoryMap(Target t) override { return s.memoryMap(t); }
    std::vector<uint8_t> palette() override { return s.palette(); }
    std::array<size_t, kTargetCount> sizes() override { return s.status().sizes; }
    Regions regions() override { return s.regions(); }
    std::string addRule(const Rule &rule) override { return s.addRule(rule); }
    std::string paint(Target t, const std::vector<uint32_t> &offsets, uint8_t value) override
    {
        return s.paint(t, offsets, value);
    }
    std::string setBytes(Target t, const std::vector<std::pair<uint32_t, uint8_t>> &bytes) override
    {
        return s.setBytes(t, bytes);
    }
    std::string saveRegion(const std::string &name, const AddressRange &range) override { return s.saveRegion(name, range); }
    std::string renameRegion(const std::string &from, const std::string &to) override { return s.renameRegion(from, to); }
    std::string deleteRegion(const std::string &name) override { return s.deleteRegion(name); }
    std::vector<std::string> regionUsers(const std::string &name) override { return s.regionUsers(name); }
    void endStroke() override { s.endStroke(); }
    std::string applyPaint() override { return s.applyPaint(); }
    bool undoPaint() override { return s.undoPaint(); }
    bool redoPaint() override { return s.redoPaint(); }
    void goToPaint(size_t count) override { s.goToPaint(count); }
    void discardDraft() override { s.discardDraft(); }
    std::vector<PaintHistory::Row> paintRows() override { return s.paintRows(); }
    bool canUndoPaint() override { return s.canUndoPaint(); }
    bool canRedoPaint() override { return s.canRedoPaint(); }
    size_t draftSize() override { return s.draftSize(); }
    bool paintWatching() override { return s.paintWatching(); }
    std::string paintNews() override { return s.paintNews(); }
};

struct SessionSurvivors : SurvivorSource
{
    CorruptionSession &s = CorruptionSession::get();
    std::vector<Survivor> survivors() override { return s.survivors(); }
    std::vector<Pin> pins() override { return s.pins(); }
    std::string pin(const std::vector<uint32_t> &offsets, const std::string &note) override { return s.pin(offsets, note); }
    void unpin(const std::vector<uint32_t> &offsets) override { s.unpin(offsets); }
};

struct SessionFunctions : FunctionSource
{
    CorruptionSession &s = CorruptionSession::get();
    std::vector<FunctionRow> functions() override { return s.functions(); }
    bool running() override { return s.corruptionOn(); }
    std::string flip(uint32_t entry) override { return s.flipFunction(entry); }
    std::string keep(uint32_t entry, int hotkey, bool start_off, const std::string &note, uint32_t every,
                     uint32_t for_frames) override
    {
        return s.keepSwitch(entry, hotkey, start_off, note, every, for_frames);
    }
    void forget(uint32_t entry) override { s.forgetSwitch(entry); }
    std::string protect(uint32_t entry, bool on, bool deep) override { return s.protectFunction(entry, on, deep); }
    std::string setName(uint32_t entry, const std::string &name) override { return s.setFunctionName(entry, name); }
    std::vector<ChangeOption> changeOptions(uint32_t entry) override { return s.changeOptions(entry); }
    std::vector<FeedReading> changeFeeds(uint32_t entry) override { return s.changeFeeds(entry); }
    std::vector<uint8_t> romNow(uint32_t at, size_t n) override { return s.romBytes(at, n, false); }
    std::vector<uint8_t> romClean(uint32_t at, size_t n) override { return s.romBytes(at, n, true); }
    std::optional<std::vector<uint8_t>> redirectBytes(const ChangeOption &o, uint32_t entry) override
    {
        return s.redirectBytes(o, entry);
    }
    std::string applyChange(const Change &c, bool on) override { return s.applyChange(c, on); }
    std::string keepChange(const Change &c) override { return s.keepChange(c); }
    void forgetChange(uint32_t at) override { s.forgetChange(at); }
    std::string tryChange(const Change &c) override { return s.tryChange(c); }
    std::optional<TriedChange> lastTry() override { return s.lastTry(); }
};

} // namespace

CorruptionDock::CorruptionDock(EmuApplication &app, QWidget *parent) : QDockWidget(tr("Corruption"), parent), app(app)
{
    setObjectName("CorruptionDock");
    remote = new RemoteControl(this);
    control = std::make_unique<SessionControl>(app, *remote);
    actions = std::make_unique<RecipeActions>(*control, this, bundled_starters());
    actions->say = [this](const QString &text, bool error) { say(text, error); };
    actions->changed = [this] { refreshRecipes(); };
    function_source = std::make_unique<SessionFunctions>();
    survivor_source = std::make_unique<SessionSurvivors>();
    memory_source = std::make_unique<SessionMemorySource>();
    // Apply Paint takes its restore point at once while the emulation thread is idle (#66).
    CorruptionSession::get().is_paused = [&app] { return app.isPaused(); };

    // The cockpit scrolls when the window is short, instead of holding the window tall.
    auto scroll = new QScrollArea(this);
    scroll->setObjectName("panel_scroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    cockpit = new Cockpit(*control, *actions, scroll);
    cockpit->setToggleKey(toggleKey());
    cockpit->setKeepKey(keepKey());
    cockpit->openWorkshop = [this](WorkshopPage page) { openWorkshop(page); };
    cockpit->openWorkshopAgain = [this] { openWorkshopAgain(); };
    cockpit->openGuide = [this] { openGuide({}); };
    scroll->setWidget(cockpit);
    // A scroll area doesn't pass its contents' minimum width on: without this the dock
    // could narrow until the buttons are cut off at the right.
    scroll->setMinimumWidth(cockpit->minimumSizeHint().width() + scroll->verticalScrollBar()->sizeHint().width());
    setWidget(scroll);

    connect(&remote->server(), &RemoteServer::clientsChanged, this, [this] {
        if (workshop && workshop->isVisible())
            workshop->refresh();
    });
    if (app.config->corruption_remote_enabled)
        control->setRemote(true);

    if (auto dir = qgetenv("SNES9X_UI_TOUR"); !dir.isEmpty())
        QTimer::singleShot(4000, this, [this, dir] { tour(QString::fromLocal8Bit(dir)); });
}

CorruptionDock::~CorruptionDock() = default;

QString CorruptionDock::toggleKey() const
{
    for (int i = 0; i < EmuConfig::allowed_bindings; i++)
    {
        auto b = app.config->binding.shortcuts[EmuConfig::eCorruptionToggle * EmuConfig::allowed_bindings + i];
        if (b.type != EmuBinding::None)
            return qs(b.to_string());
    }
    return {};
}

QString CorruptionDock::keepKey() const
{
    for (int i = 0; i < EmuConfig::allowed_bindings; i++)
    {
        auto b = app.config->binding.shortcuts[EmuConfig::eCorruptionKeep * EmuConfig::allowed_bindings + i];
        if (b.type != EmuBinding::None)
            return qs(b.to_string());
    }
    return {};
}

void CorruptionDock::say(const QString &text, bool error)
{
    cockpit->showMessage(text, error);
    if (workshop && workshop->isVisible() && workshop->page() == WorkshopPage::Recipe)
        workshop->recipePage()->say(text, error);
}

void CorruptionDock::refreshRecipes()
{
    cockpit->refreshRecipes();
}

void CorruptionDock::openWorkshop(WorkshopPage page)
{
    if (!workshop)
    {
        workshop = new Workshop(*control, *actions, *function_source, *survivor_source, *memory_source, this);
        workshop->sharingPage()->libraryChanged = [this] { refreshRecipes(); };
        workshop->sharingPage()->lookForRecipes = [this] { refreshRecipes(); };
        // As big as it was last time, never smaller than its pages need.
        auto &config = *app.config;
        QSize size(std::max(config.corruption_workshop_width, workshop->width()),
                   std::max(config.corruption_workshop_height, 480));
        workshop->resize(size);
        workshop->onClosed = [this](WorkshopPage page, QSize size) {
            app.config->corruption_workshop_page = int(page);
            app.config->corruption_workshop_width = size.width();
            app.config->corruption_workshop_height = size.height();
        };
    }
    workshop->showPage(page);
}

void CorruptionDock::openWorkshopAgain()
{
    int page = app.config->corruption_workshop_page;
    openWorkshop(page >= 0 && page <= int(WorkshopPage::Moments) ? WorkshopPage(page) : WorkshopPage::Recipe);
}

void CorruptionDock::openGuide(const QString &section)
{
    if (!guide)
        guide = new GuideWindow(this);
    guide->showSection(section);
    guide->show();
    guide->raise();
    guide->activateWindow();
}

void CorruptionDock::tour(const QString &dir)
{
    QDir().mkpath(dir);
    auto shot = [dir](QWidget *w, const QString &name) {
        QApplication::processEvents();
        w->grab().save(QString("%1/%2.png").arg(dir, name));
    };
    auto choose = [](QWidget *root, const char *name, const QString &data) {
        if (auto c = root->findChild<QComboBox *>(name))
            if (int i = c->findData(data); i >= 0)
                c->setCurrentIndex(i);
    };
    auto load = [](const char *resource) {
        QFile file(resource);
        if (file.open(QIODevice::ReadOnly))
            if (auto parsed = parse_recipe(file.readAll().toStdString(), {}); parsed.recipe)
            {
                CorruptionSession::get().loadRecipe(*parsed.recipe);
                CorruptionSession::get().setActive(true);
            }
    };
    auto log = [dir](const QString &line) {
        QFile file(dir + "/flows.txt");
        if (file.open(QIODevice::Append | QIODevice::Text))
            file.write((line + "\n").toUtf8());
    };
    // Each step waits for the game to run a little, so the pictures show live data.
    auto steps = std::make_shared<std::vector<std::function<void()>>>();
    auto next = std::make_shared<std::function<void()>>();
    *next = [steps, next] {
        if (steps->empty())
            return;
        auto step = steps->front();
        steps->erase(steps->begin());
        step();
        QTimer::singleShot(1500, *next);
    };
    steps->push_back([=, this] {
        setVisible(true);
        shot(window(), "01-window");
        shot(cockpit, "02-cockpit-no-recipe");
        // Always a recipe (#74): the game started with a blank Untitled one, and a rule
        // added straight away lands in it (no "make a recipe first").
        auto describe = [](const char *when) {
            auto st = CorruptionSession::get().status();
            return QString("untitled %1: \"%2\" untitled=%3 blank=%4 edited=%5 rules=%6")
                .arg(when, qs(st.engine.recipe_name))
                .arg(int(st.untitled))
                .arg(int(st.blank))
                .arg(int(st.edited))
                .arg(st.rules.size());
        };
        log(describe("at load"));
        openWorkshop(WorkshopPage::Recipe);
        workshop->refresh();
        shot(workshop, "02a-workshop-untitled");
        auto added = CorruptionSession::get().addRule(default_rule());
        log(describe("after Add Rule") + (added.empty() ? "" : " error " + qs(added)));
        workshop->refresh();
        cockpit->refresh();
        shot(workshop, "02b-workshop-untitled-rule");
        shot(cockpit, "02c-cockpit-untitled-rule");
        workshop->hide();
    });
    steps->push_back([=] { load(":/corruption-examples/low-wram-roulette.json"); });
    steps->push_back([=, this] {
        cockpit->refresh();
        shot(cockpit, "03-cockpit-running");
        auto status = CorruptionSession::get().status();
        if (!status.rules.empty())
        {
            SizedMemory sizes;
            sizes.sizes = status.sizes;
            RuleEditor editor(status.rules[0], CorruptionSession::get().regions(), sizes, this);
            editor.show();
            editor.adjustSize();
            shot(&editor, "04-rule-editor");
        }
        // Share and import, end to end.
        auto share = CorruptionSession::get().shareRecipe((dir + "/tour.s9xrecipe").toStdString());
        std::string name;
        auto import = CorruptionSession::get().importRecipe((dir + "/tour.s9xrecipe").toStdString(), name);
        log(QString("share: %1\nimport: %2 as %3")
                .arg(qs(share.empty() ? "ok" : share), qs(import.empty() ? "ok" : import), qs(name)));
        refreshRecipes();
        CorruptionSession::get().setActive(true);
        openWorkshop(WorkshopPage::Recipe);
    });
    steps->push_back([=, this] {
        workshop->refresh();
        shot(workshop, "05-workshop-recipe");
        openWorkshop(WorkshopPage::Memory);
    });
    steps->push_back([=, this] { shot(workshop, "06-workshop-memory-wram"); });
    // A live ROM recipe for a while: the instant revert catches what derails (#51).
    steps->push_back([=] { load(":/corruption-examples/rom-code-drip.json"); });
    for (int i = 0; i < 8; i++)
        steps->push_back([] {}); // 12 s of play
    steps->push_back([=, this] {
        cockpit->refresh();
        shot(cockpit, "07-cockpit-code-drip");
        auto status = CorruptionSession::get().status();
        log(QString("code drip: %1 changed, %2 used, %3 put back at once, %4 crashes")
                .arg(status.rom_changed)
                .arg(status.rom_used)
                .arg(status.instant_reverts)
                .arg(status.crashes));
        openWorkshop(WorkshopPage::Rom);
    });
    steps->push_back([=, this] { shot(workshop, "08-workshop-rom"); });
    steps->push_back([=] { CorruptionSession::get().backToNormal(); });
    steps->push_back([=, this] {
        cockpit->refresh();
        shot(cockpit, "09-cockpit-back-to-normal");
        openWorkshop(WorkshopPage::Memory);
        choose(workshop->memoryPage(), "target", "rom");
        choose(workshop->memoryPage(), "mode", "pixels");
        choose(workshop->memoryPage(), "overlay", "code");
    });
    steps->push_back([=, this] { shot(workshop, "10-workshop-memory-rom-code"); });
    // The Workshop as narrow as it goes (#68): nothing cut off.
    steps->push_back([=, this] {
        workshop->resize(workshop->minimumSizeHint());
    });
    steps->push_back([=, this] {
        shot(workshop, "10a-workshop-memory-narrow");
        log(QString("narrowest Workshop: %1 x %2").arg(workshop->width()).arg(workshop->height()));
        workshop->resize(std::max(1000, workshop->minimumSizeHint().width()), 720);
    });
    // Paint (#66): a stroke of STP over the first 1 KB of the ROM, drafted, then applied: the
    // game runs into it, the watch rolls back and the stroke is back in the draft.
    steps->push_back([=, this] {
        auto memory_page = workshop->memoryPage();
        if (auto paint = memory_page->findChild<QPushButton *>("paint"); paint && !paint->isChecked())
            paint->click();
        std::vector<uint32_t> offsets;
        for (uint32_t o = 0; o < 0x400; o++)
            offsets.push_back(o);
        auto &session = CorruptionSession::get();
        session.paint(Target::ROM, offsets, 0xDB);
        session.endStroke();
        memory_page->refresh();
    });
    steps->push_back([=, this] {
        shot(workshop, "10b-workshop-memory-paint-draft");
        if (auto apply = workshop->memoryPage()->findChild<QPushButton *>("apply_paint"))
            apply->click();
    });
    steps->push_back([] {}); // the frames after Apply
    steps->push_back([=, this] {
        workshop->memoryPage()->refresh();
        shot(workshop, "10c-workshop-memory-paint-taken-back");
        auto status = CorruptionSession::get().status();
        log(QString("paint: %1 / %2; draft %3, ROM changed %4")
                .arg(QString::fromStdString(status.message), QString::fromStdString(status.message_detail))
                .arg(CorruptionSession::get().draftSize())
                .arg(status.rom_changed));
        cockpit->refresh();
        shot(cockpit, "10d-cockpit-paint-taken-back");
    });
    steps->push_back([=, this] {
        choose(workshop->memoryPage(), "target", "vram");
        choose(workshop->memoryPage(), "mode", "tiles");
    });
    steps->push_back([=, this] {
        shot(workshop, "11-workshop-memory-vram-tiles");
        openWorkshop(WorkshopPage::Functions);
    });
    steps->push_back([=, this] {
        auto page = workshop->functionsPage();
        page->refresh();
        shot(workshop, "12-workshop-functions");
        // The busiest one selected and protected.
        if (auto table = page->findChild<QTableWidget *>("functions"); table && table->rowCount())
            table->selectRow(0);
        if (auto protect = page->findChild<QCheckBox *>("protect"); protect && protect->isEnabled())
            protect->setChecked(true);
    });
    steps->push_back([=, this] {
        auto page = workshop->functionsPage();
        shot(workshop, "13-workshop-functions-selected");
        if (auto change = page->findChild<QPushButton *>("change"); change && change->isEnabled())
            change->click();
    });
    steps->push_back([=, this] {
        if (auto editor = workshop->functionsPage()->findChild<QDialog *>("function_editor"))
        {
            if (auto lines = editor->findChild<QTableWidget *>("lines"); lines && lines->rowCount())
                lines->selectRow(0);
            QApplication::processEvents();
            shot(editor, "14-function-editor");
            if (QFile audit(dir + "/audit-editor.txt"); audit.open(QIODevice::WriteOnly | QIODevice::Text))
                for (auto &issue : ui::audit(editor))
                    audit.write(QString("change it: %1\n").arg(issue).toUtf8());
        }
        // Trying ahead (#78) needs corruption on: a gentle recipe, then the first line.
        load(":/corruption-examples/gentle-wram-noise.json");
    });
    steps->push_back([=, this] {
        if (auto editor = workshop->functionsPage()->findChild<QDialog *>("function_editor"))
            if (auto try_button = editor->findChild<QPushButton *>("try"); try_button && try_button->isEnabled())
                try_button->click(); // the answer comes between frames
    });
    steps->push_back([=, this] {
        if (auto editor = workshop->functionsPage()->findChild<QDialog *>("function_editor"))
        {
            QApplication::processEvents();
            shot(editor, "14a-function-editor-tried");
            if (auto line = static_cast<ui::StatusLine *>(editor->findChild<QWidget *>("try_line")); line)
                if (QFile out(dir + "/try.txt"); out.open(QIODevice::WriteOnly | QIODevice::Text))
                    out.write((line->isVisible() ? line->text() : QString("(hidden)")).toUtf8());
            editor->close();
        }
        CorruptionSession::get().backToNormal();
        if (auto show = workshop->functionsPage()->findChild<QPushButton *>("show"); show && show->isEnabled())
            show->click();
    });
    steps->push_back([=, this] {
        shot(workshop, "15-workshop-memory-functions");
        openWorkshop(WorkshopPage::Safety);
    });
    steps->push_back([=, this] {
        shot(workshop, "16-workshop-safety");
        openWorkshop(WorkshopPage::Sharing);
    });
    steps->push_back([=, this] {
        shot(workshop, "17-workshop-sharing");
        // Keep a golden moment (#47): from the cockpit, as a player would.
        if (auto keep = cockpit->findChild<QPushButton *>("keep_moment"); keep && keep->isEnabled())
            keep->click();
    });
    steps->push_back([=, this] {
        if (QFile out(dir + "/keep.txt"); out.open(QIODevice::WriteOnly | QIODevice::Text))
            out.write(qs(CorruptionSession::get().status().keep_news).toUtf8());
        openWorkshop(WorkshopPage::Moments);
        workshop->refresh();
    });
    steps->push_back([=, this] {
        shot(workshop, "17a-workshop-moments");
        if (auto play = workshop->momentsPage()->findChild<QPushButton *>("play_moment"); play && play->isEnabled())
            play->click();
    });
    steps->push_back([=, this] {
        cockpit->refresh();
        shot(cockpit, "17b-cockpit-moment-playing");
        log(QString("moment playing=%1 at %2 of %3")
                .arg(int(CorruptionSession::get().status().moment_playing))
                .arg(CorruptionSession::get().status().moment_at)
                .arg(CorruptionSession::get().status().moment_seconds));
        openWorkshop(WorkshopPage::Recipe);
    });
    steps->push_back([=, this] {
        shot(workshop, "18-workshop-recipe-at-the-end");
        workshop->close();
        openGuide({});
    });
    steps->push_back([=, this] {
        shot(guide, "19-guide");
        openGuide(tr("Getting back to normal"));
    });
    steps->push_back([=, this] {
        shot(guide, "20-guide-section");
        guide->close();
        cockpit->refresh();
        shot(cockpit, "21-cockpit-at-the-end");
        // What a screen reader would miss, window by window (empty when all is well).
        QFile audit(dir + "/audit.txt");
        if (audit.open(QIODevice::WriteOnly | QIODevice::Text))
            for (auto [name, w] : std::initializer_list<std::pair<const char *, QWidget *>>{
                     { "cockpit", cockpit }, { "workshop", workshop } })
                if (w)
                    for (auto &issue : ui::audit(w))
                        audit.write(QString("%1: %2\n").arg(name, issue).toUtf8());
        QFile done(dir + "/done");
        done.open(QIODevice::WriteOnly);
    });
    (*next)();
}
