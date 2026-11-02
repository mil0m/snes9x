#include "Cockpit.hpp"
#include "ButtonsBar.hpp"
#include "IntensitySlider.hpp"
#include "RecipeActions.hpp"
#include "UiKit.hpp"
#include "corrupt/rule_text.h"

#include <QComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

using namespace corrupt;

namespace
{

QString qs(const std::string &s)
{
    return QString::fromStdString(s);
}

QString plural(qulonglong n, const QString &one, const QString &many)
{
    return n == 1 ? one : many.arg(QLocale().toString(n));
}

QPushButton *button(const QString &text, QWidget *parent, const char *icon = nullptr)
{
    auto b = new QPushButton(text, parent);
    b->setAutoDefault(false);
    if (icon)
        b->setIcon(ui::icon(icon));
    return b;
}

QString link(const QString &page, const QString &words)
{
    return QString("<a href=\"%1\">%2</a>").arg(page, words.toHtmlEscaped());
}

std::optional<WorkshopPage> pageFor(const QString &name)
{
    if (name == "recipe")
        return WorkshopPage::Recipe;
    if (name == "functions")
        return WorkshopPage::Functions;
    if (name == "memory")
        return WorkshopPage::Memory;
    if (name == "rom")
        return WorkshopPage::Rom;
    if (name == "safety")
        return WorkshopPage::Safety;
    if (name == "sharing")
        return WorkshopPage::Sharing;
    return std::nullopt;
}

} // namespace

Cockpit::Cockpit(CorruptionControl &control, RecipeActions &actions, QWidget *parent)
    : QWidget(parent), control(control), actions(actions)
{
    setObjectName("cockpit");
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    auto followLink = [this](const QString &name) {
        if (auto page = pageFor(name); page && openWorkshop)
            openWorkshop(*page);
    };

    // The game, the Workshop and the guide.
    auto top = new QHBoxLayout;
    game_label = new QLabel(tr("No game loaded"), this);
    game_label->setObjectName("game");
    game_label->setWordWrap(true);
    QFont title_font = game_label->font();
    title_font.setBold(true);
    title_font.setPointSizeF(title_font.pointSizeF() * 1.15);
    game_label->setFont(title_font);
    top->addWidget(game_label, 1);
    auto workshop = button(tr("Workshop"), this, "tool");
    workshop->setObjectName("workshop");
    workshop->setToolTip(tr("Everything else: rules, the game's functions, memory, the ROM, safety and sharing"));
    top->addWidget(workshop, 0, Qt::AlignTop);
    auto guide = new QPushButton(this);
    guide->setObjectName("guide");
    guide->setAutoDefault(false);
    guide->setIcon(ui::icon("help-circle"));
    guide->setToolTip(tr("The guide: how everything here works, in plain words"));
    guide->setAccessibleName(tr("Guide"));
    top->addWidget(guide, 0, Qt::AlignTop);
    layout->addLayout(top);

    // What's going on, in one bar: the accent colour while corrupting.
    state_label = new QLabel(this);
    state_label->setObjectName("state");
    state_label->setWordWrap(true);
    state_label->setAutoFillBackground(true);
    state_label->setMargin(6);
    QFont state_font = state_label->font();
    state_font.setBold(true);
    state_label->setFont(state_font);
    layout->addWidget(state_label);
    connect(workshop, &QPushButton::clicked, this, [this] {
        if (openWorkshopAgain)
            openWorkshopAgain();
    });
    connect(guide, &QPushButton::clicked, this, [this] {
        if (openGuide)
            openGuide();
    });

    // While no recipe is loaded: where to start.
    start_card = new QLabel(this);
    start_card->setObjectName("start_here");
    start_card->setWordWrap(true);
    start_card->setFrameShape(QFrame::StyledPanel);
    start_card->setMargin(8);
    layout->addWidget(start_card);

    // The recipe.
    auto recipe_row = new QHBoxLayout;
    recipe_combo = new QComboBox(this);
    recipe_combo->setObjectName("recipe");
    recipe_combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    recipe_combo->setMinimumContentsLength(12);
    recipe_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    recipe_combo->setToolTip(tr("Your recipes for this game, then the starters that come with the app. Choosing "
                                "one loads it."));
    recipe_combo->setAccessibleName(tr("Recipe"));
    recipe_row->addLayout(ui::row(recipe_combo), 1);
    save_button = button(tr("Save"), this, "device-floppy");
    save_button->setObjectName("save");
    save_button->setToolTip(tr("Save the recipe in the library (with its start moment, so this run can be replayed)"));
    recipe_row->addWidget(save_button);
    layout->addLayout(recipe_row);
    connect(recipe_combo, &QComboBox::activated, this, &Cockpit::recipeChosen);
    connect(save_button, &QPushButton::clicked, this, [this] { this->actions.save(); });

    // News from the session: a few words that fade. The area keeps one line's height when
    // empty, so Corrupt doesn't jump under the mouse when a message comes or goes.
    auto message_area = new QWidget(this);
    message_area->setObjectName("message_area");
    auto message_layout = new QVBoxLayout(message_area);
    message_layout->setContentsMargins(0, 0, 0, 0);
    message_line = new ui::StatusLine(message_area);
    message_line->setObjectName("message");
    message_layout->addWidget(message_line);
    message_area->setMinimumHeight(fontMetrics().height());
    layout->addWidget(message_area);
    fade_timer = new QTimer(this);
    fade_timer->setSingleShot(true);
    connect(fade_timer, &QTimer::timeout, this, [this] {
        message_line->setMessage({});
        message_line->setToolTip({});
        showing_error = false;
    });

    // Corrupt (the message area above already leaves a line of space).
    auto run_row = new QHBoxLayout;
    corrupt_button = button(tr("Corrupt"), this, "bolt");
    corrupt_button->setObjectName("corrupt");
    back_button = button(tr("Back to Normal"), this, "arrow-back-up");
    back_button->setObjectName("back_to_normal");
    back_button->setToolTip(tr("Turn corruption off, put the ROM back to clean, and take the game back to the "
                               "moment the run started, so everything it changed is gone."));
    run_row->addWidget(corrupt_button, 1);
    run_row->addWidget(back_button, 1);
    layout->addLayout(run_row);
    moment_note = new QLabel(this);
    moment_note->setObjectName("moment_note");
    moment_note->setWordWrap(true);
    ui::setTone(moment_note, ui::Tone::Secondary);
    layout->addWidget(moment_note);
    run_controls = new QWidget(this);
    run_controls->setObjectName("run_controls");
    auto run_form = ui::form(run_controls);
    run_form->setContentsMargins(0, 0, 0, 0);
    chaos_slider = new IntensitySlider(this);
    chaos_slider->setObjectName("chaos");
    chaos_slider->setToolTip(tr("How hard the whole recipe hits: turns every rule up or down together."));
    chaos_slider->setAccessibleName(tr("Chaos"));
    chaos_slider->onChanged = [this](double v) { this->control.setIntensity(v); };
    run_form->addRow(tr("Chaos"), chaos_slider);
    auto seed_row = new QHBoxLayout;
    seed_edit = new QLineEdit(this);
    seed_edit->setObjectName("seed");
    seed_edit->setFrame(false); // a quiet number; click it to type another
    seed_edit->setToolTip(tr("The dice. The same recipe with the same seed, from the same start, does the same "
                             "thing every time. Click and type one, then press Enter to use it."));
    reroll_button = button(tr("Re-roll"), this, "dice-5");
    reroll_button->setObjectName("reroll");
    reroll_button->setToolTip(tr("New dice: same recipe, different corruption, starting over."));
    seed_row->addWidget(seed_edit, 1);
    seed_row->addWidget(reroll_button);
    auto seed_label = new QLabel(tr("Seed"), this);
    seed_label->setBuddy(seed_edit);
    run_form->addRow(seed_label, seed_row);
    layout->addWidget(run_controls);
    connect(corrupt_button, &QPushButton::clicked, this, [this] {
        this->control.setActive(!this->control.status().engine.active);
        refresh();
    });
    connect(back_button, &QPushButton::clicked, this, [this] { this->control.backToNormal(); });
    connect(reroll_button, &QPushButton::clicked, this,
            [this] { seed_edit->setText(QString::number(this->control.reroll())); });
    connect(seed_edit, &QLineEdit::returnPressed, this, [this] {
        bool ok = false;
        auto seed = seed_edit->text().trimmed().toULongLong(&ok, 0);
        if (ok)
            this->control.setSeed(seed);
        else
            showMessage(tr("The seed must be a number. Type digits, or press Re-roll for a new one."), true);
    });

    // The recipe's buttons.
    buttons_box = new QWidget(this);
    buttons_box->setObjectName("buttons_section");
    auto buttons_layout = new QVBoxLayout(buttons_box);
    buttons_layout->setContentsMargins(0, ui::sectionGap(this) / 2, 0, 0);
    buttons_layout->addWidget(ui::heading(tr("Buttons"), buttons_box));
    buttons_bar = new ButtonsBar(buttons_box);
    buttons_bar->onPress = [this](int key) { this->control.pressButton(key); };
    buttons_layout->addWidget(buttons_bar);
    layout->addWidget(buttons_box);

    // The rules: on, name, strength. The rest is in the Workshop.
    rules_box = new QWidget(this);
    rules_box->setObjectName("rules_section");
    auto rules_layout = new QVBoxLayout(rules_box);
    rules_layout->setContentsMargins(0, ui::sectionGap(this) / 2, 0, 0);
    auto rules_head = new QHBoxLayout;
    rules_head->addWidget(ui::heading(tr("Rules"), rules_box));
    rules_head->addStretch(1);
    auto rules_link = new QLabel(link("recipe", tr("Add or change rules")), rules_box);
    rules_link->setObjectName("rules_link");
    connect(rules_link, &QLabel::linkActivated, this, followLink);
    rules_head->addWidget(rules_link);
    rules_layout->addLayout(rules_head);
    rules_table = new QTableWidget(0, kColumns, rules_box);
    rules_table->setObjectName("rules");
    rules_table->setHorizontalHeaderLabels({ tr("On"), tr("Rule"), tr("Strength") });
    rules_table->horizontalHeaderItem(kStrength)->setToolTip(tr("How hard this rule hits, on top of Chaos"));
    rules_table->verticalHeader()->setVisible(false);
    auto header = rules_table->horizontalHeader();
    header->setSectionResizeMode(kOn, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(kName, QHeaderView::Stretch);
    header->setSectionResizeMode(kStrength, QHeaderView::Fixed);
    header->setHighlightSections(false);
    rules_table->setColumnWidth(kStrength, fontMetrics().horizontalAdvance("100%") + 72);
    rules_table->setTextElideMode(Qt::ElideRight);
    rules_table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    rules_table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    rules_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    rules_table->setSelectionMode(QAbstractItemView::SingleSelection);
    rules_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    rules_table->setToolTip(tr("Double-click a rule to change it"));
    rules_table->setAccessibleName(tr("Rules"));
    rules_layout->addWidget(rules_table);
    rules_empty = new QLabel(tr("No rules yet: add one in the Workshop, or pick a starter above."), rules_box);
    rules_empty->setObjectName("rules_empty");
    rules_empty->setWordWrap(true);
    ui::setTone(rules_empty, ui::Tone::Secondary);
    rules_layout->addWidget(rules_empty);
    layout->addWidget(rules_box);
    connect(rules_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int column) {
        if (column != kOn && column != kStrength)
            this->actions.editRule(row);
    });
    connect(rules_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (item->column() == kOn)
            this->control.setRuleEnabled(size_t(item->row()), item->checkState() == Qt::Checked);
    });

    // One line each about the crash guard and the ROM.
    layout->addSpacing(ui::sectionGap(this) / 2);
    auto line = [this, &followLink](const char *name, QLabel *&icon) {
        icon = new QLabel(this);
        icon->setAlignment(Qt::AlignTop);
        auto label = new QLabel(this);
        label->setObjectName(name);
        label->setWordWrap(true);
        label->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
        connect(label, &QLabel::linkActivated, this, followLink);
        return label;
    };
    auto guard_row = new QHBoxLayout;
    guard_label = line("guard_line", guard_icon);
    guard_row->addWidget(guard_icon, 0, Qt::AlignTop);
    guard_row->addWidget(guard_label, 1);
    auto mark = mark_button = button(tr("It Crashed"), this, "history");
    mark->setObjectName("mark_crash");
    mark->setToolTip(tr("For a crash the guard missed: go back about two seconds and blame what changed."));
    guard_row->addWidget(mark, 0, Qt::AlignTop);
    layout->addLayout(guard_row);
    connect(mark, &QPushButton::clicked, this, [this] { this->control.markCrash(); });
    auto rom_row = new QHBoxLayout;
    rom_label = line("rom_line", rom_icon);
    rom_row->addWidget(rom_icon, 0, Qt::AlignTop);
    rom_row->addWidget(rom_label, 1);
    layout->addLayout(rom_row);
    layout->addStretch(1);

    setToggleKey({});
    timer = new QTimer(this);
    timer->setInterval(250);
    connect(timer, &QTimer::timeout, this, &Cockpit::refresh);
    timer->start();
    refresh();
}

void Cockpit::setToggleKey(const QString &key)
{
    toggle_key = key;
    shown_active = -1; // the button's words again
    start_card->setText(tr("<b>Start here.</b> 1. Pick a recipe below: the <i>starters</i> come with the app. "
                           "2. Press <b>Corrupt</b>%1 and watch the game. 3. <b>Back to Normal</b> undoes "
                           "everything. The <b>?</b> explains the rest.")
                            .arg(key.isEmpty() ? QString() : QString(" (%1)").arg(key)));
}

void Cockpit::showMessage(const QString &text, bool error)
{
    message_line->setMessage(text, error ? ui::Tone::Error : ui::Tone::Normal);
    message_line->setToolTip({});
    showing_error = error; // the session's news waits until it fades
    startFade();
}

void Cockpit::startFade()
{
    fade_timer->start(fade_ms);
}

void Cockpit::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)
        shown_state.clear(); // the bar's colours again
}

void Cockpit::showState(const QString &state, const QString &text)
{
    state_label->setText(text);
    if (state == shown_state)
        return;
    shown_state = state;
    state_label->setProperty("state", state);
    QPalette p = palette();
    if (state == "corrupting")
    {
        p.setColor(QPalette::Window, p.color(QPalette::Highlight));
        p.setColor(QPalette::WindowText, p.color(QPalette::HighlightedText));
    }
    else
    {
        // A tint of the text (or the warning colour) over the window: a bar in any style,
        // light or dark, where the Mac's alternate base is as white as the window.
        auto mix = [](QColor a, QColor b, double t) {
            return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
                                    a.blueF() + (b.blueF() - a.blueF()) * t);
        };
        QColor window = p.color(QPalette::Window);
        QColor tint = state == "stopped" ? ui::toneColor(ui::Tone::Warning, p) : p.color(QPalette::WindowText);
        p.setColor(QPalette::Window, mix(window, tint, state == "stopped" ? 0.18 : 0.07));
    }
    state_label->setPalette(p);
}

void Cockpit::setLink(QLabel *label, QLabel *icon, const char *icon_name, const QString &page, const QString &text,
                      const QString &where, ui::Tone tone)
{
    // The whole line is the link, in the text's own colour, with a chevron: no word repeated.
    QColor colour = ui::toneColor(tone, palette());
    label->setText(QString("<a href=\"%1\" style=\"color:%2; text-decoration:none\">%3&nbsp;›</a>")
                       .arg(page, colour.name(), text.toHtmlEscaped()));
    label->setAccessibleName(tr("%1: %2").arg(where, text));
    label->setToolTip(tr("Open %1 in the Workshop").arg(where));
    int size = label->fontMetrics().height();
    icon->setPixmap(ui::pixmap(icon_name, colour, size, devicePixelRatioF()));
}

void Cockpit::refresh()
{
    auto status = control.status();

    std::string game_key = status.game_loaded ? status.game.sha256 : "";
    if (game_key != shown_game)
    {
        shown_game = game_key;
        QString title = status.rom_name.empty() ? qs(status.game.title) : qs(status.rom_name);
        game_label->setText(status.game_loaded ? title : tr("No game loaded"));
        game_label->setToolTip(status.game_loaded
                                   ? tr("Its folder in the library: %1")
                                         .arg(qs(game_folder_name(status.game.title, status.game.sha256)))
                                   : QString());
        refreshRecipes();
    }
    if (status.library_generation != shown_library_generation)
    {
        shown_library_generation = status.library_generation; // someone sharing the library saved one (#62)
        refreshRecipes();
    }
    if (status.recipe_generation != shown_generation || status.rules_generation != shown_rules_generation)
    {
        shown_generation = status.recipe_generation;
        shown_rules_generation = status.rules_generation;
        rebuildRules(status);
    }
    if (status.recipe_generation != shown_choice_generation || status.recipe_path != shown_recipe_path)
    {
        shown_choice_generation = status.recipe_generation;
        shown_recipe_path = status.recipe_path;
        syncRecipeChoice(status);
    }
    start_card->setVisible(status.game_loaded && !status.has_recipe);

    save_button->setEnabled(status.has_recipe && (status.edited || status.new_moment || status.recipe_path.empty()));
    run_controls->setVisible(status.has_recipe); // hidden rather than greyed out
    mark_button->setVisible(status.has_recipe);
    chaos_slider->refresh(status.intensity);
    for (size_t i = 0; i < rule_sliders.size() && i < status.rules.size(); i++)
        rule_sliders[i]->refresh(status.rules[i].intensity); // a crash-guard dampen moves it
    {
        // The checkboxes follow the engine too (the guard can turn a rule off).
        QSignalBlocker block(rules_table);
        for (int i = 0; i < rules_table->rowCount() && i < int(status.rules.size()); i++)
            if (auto on = rules_table->item(i, kOn))
                on->setCheckState(status.rules[size_t(i)].enabled ? Qt::Checked : Qt::Unchecked);
    }
    buttons_bar->setButtons(status.buttons);
    buttons_box->setVisible(!status.buttons.empty());
    rules_box->setVisible(status.has_recipe);
    rules_empty->setVisible(status.rules.empty());

    bool on = status.engine.active;
    corrupt_button->setEnabled(status.has_recipe);
    if (shown_active != int(on) + 2 * int(status.has_recipe))
    {
        shown_active = int(on) + 2 * int(status.has_recipe);
        QString key = toggle_key.isEmpty() ? QString() : QString(" (%1)").arg(toggle_key);
        corrupt_button->setText((on ? tr("Stop") : tr("Corrupt")) + key);
        corrupt_button->setIcon(ui::icon(on ? "player-stop" : "bolt"));
        corrupt_button->setDefault(!on && status.has_recipe); // the obvious next step looks it
        corrupt_button->setToolTip(!status.has_recipe ? tr("Pick a recipe first")
                                   : on ? tr("Stop corrupting. What it already changed stays; Back to Normal undoes it.")
                                        : tr("Start the recipe."));
    }
    back_button->setEnabled(status.can_go_back || status.engine.active);
    if (!seed_edit->hasFocus())
        seed_edit->setText(status.has_recipe ? QString::number(status.engine.seed) : QString());

    // What's happening, in one bar.
    if (!status.game_loaded)
        showState("idle", tr("Load a game to start"));
    else if (!status.has_recipe)
        showState("idle", tr("Pick a recipe to start"));
    else if (status.starting)
        showState("corrupting", tr("Starting…"));
    else if (on)
    {
        auto seconds = status.engine.frame / 60;
        showState("corrupting", tr("Corrupting · %1:%2 · %3")
                                    .arg(seconds / 60)
                                    .arg(seconds % 60, 2, 10, QChar('0'))
                                    .arg(plural(status.engine.firings, tr("1 change"), tr("%1 changes"))));
    }
    else if (status.back_to_normal)
        showState("normal", tr("Not corrupting · back to normal"));
    else if (status.can_go_back)
        showState("stopped", tr("Stopped · its changes stay until Back to Normal"));
    else
        showState("idle", tr("Ready · press Corrupt to start"));

    QString moment; // starting from a saved moment is news (the message line), not a note
    if (status.has_recipe && on && status.new_moment)
        moment = tr("This run starts from a new moment: Save keeps it, so the run can be replayed.");
    moment_note->setText(moment);
    moment_note->setVisible(!moment.isEmpty());

    // The crash guard, in one line.
    QString guard;
    if (!status.detection_enabled)
        guard = tr("Crash guard off: crashes aren't caught");
    else if (status.crashes == 0)
        guard = tr("Crash guard on · no crashes yet");
    else
        guard = tr("Crash guard on · %1").arg(plural(status.crashes, tr("1 crash caught"), tr("%1 crashes caught")));
    setLink(guard_label, guard_icon, "shield", "safety", guard, tr("Safety"),
            status.detection_enabled ? ui::Tone::Normal : ui::Tone::Warning);
    if (!status.last_crash.empty())
        guard_label->setToolTip(tr("Last: %1").arg(qs(status.last_crash)));

    // The ROM, in one line.
    QStringList rom;
    if (status.game_loaded)
    {
        rom << (status.rom_changed == 0 ? tr("ROM clean")
                                        : tr("ROM: %1").arg(plural(status.rom_changed, tr("1 byte changed"),
                                                                   tr("%1 bytes changed"))));
        if (status.functions_off)
            rom << plural(status.functions_off, tr("1 function off"), tr("%1 functions off"));
        if (status.pins)
            rom << plural(status.pins, tr("1 pinned"), tr("%1 pinned"));
    }
    if (!rom.isEmpty())
        setLink(rom_label, rom_icon, "cpu", "rom", rom.join(" · "), tr("The ROM"), ui::Tone::Normal);
    rom_label->setVisible(!rom.isEmpty());
    rom_icon->setVisible(!rom.isEmpty());

    if (status.message != shown_message && !showing_error)
    {
        shown_message = status.message;
        message_line->setMessage(qs(status.message));
        message_line->setToolTip(qs(status.message_detail));
        startFade();
    }
}

void Cockpit::refreshRecipes()
{
    auto status = control.status();
    auto &entries = actions.refreshEntries();
    QSignalBlocker block(recipe_combo);
    recipe_combo->clear();
    recipe_combo->addItem(tr("Choose a recipe…"), QString("none"));
    for (size_t i = 0; i < entries.size(); i++)
    {
        auto &e = entries[i];
        QString label = qs(e.name);
        if (!e.recipe)
            label += tr("  (broken)");
        else if (e.sha_mismatch)
            label += tr("  (made for another version of this ROM)");
        recipe_combo->addItem(label, QString("mine:%1").arg(i));
        if (e.recipe && !e.recipe->notes.empty())
            recipe_combo->setItemData(recipe_combo->count() - 1, qs(e.recipe->notes), Qt::ToolTipRole);
    }
    recipe_combo->insertSeparator(recipe_combo->count());
    auto &starters = actions.starters();
    for (size_t i = 0; i < starters.size(); i++)
    {
        recipe_combo->addItem(tr("Starter: %1").arg(qs(starters[i].recipe.name)), QString("starter:%1").arg(i));
        recipe_combo->setItemData(recipe_combo->count() - 1, qs(starters[i].recipe.notes), Qt::ToolTipRole);
    }
    syncRecipeChoice(status);
}

void Cockpit::syncRecipeChoice(const PanelStatus &status)
{
    QSignalBlocker block(recipe_combo);
    // A recipe that's loaded but isn't in the list (new, or a starter) gets a line of its own.
    for (int i = recipe_combo->count() - 1; i >= 0; i--)
        if (recipe_combo->itemData(i).toString() == "loaded")
            recipe_combo->removeItem(i);
    if (!status.has_recipe)
        return recipe_combo->setCurrentIndex(0);
    auto &entries = actions.entries();
    for (size_t i = 0; i < entries.size(); i++)
        if (!status.recipe_path.empty() && entries[i].path == status.recipe_path)
            return recipe_combo->setCurrentIndex(recipe_combo->findData(QString("mine:%1").arg(i)));
    recipe_combo->insertItem(1, tr("%1 (new)").arg(qs(status.engine.recipe_name)), QString("loaded"));
    recipe_combo->setCurrentIndex(1);
}

void Cockpit::recipeChosen(int index)
{
    auto choice = recipe_combo->itemData(index).toString();
    if (choice.startsWith("mine:") || choice.startsWith("starter:"))
        actions.choose(choice);
    syncRecipeChoice(control.status());
}

void Cockpit::rebuildRules(const PanelStatus &status)
{
    QSignalBlocker block(rules_table);
    rule_sliders.clear(); // the table owns and deletes them
    rules_table->setRowCount(0);
    rules_table->setRowCount(int(status.rules.size()));
    for (int i = 0; i < int(status.rules.size()); i++)
    {
        auto &r = status.rules[size_t(i)];
        auto on = new QTableWidgetItem;
        on->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        on->setCheckState(r.enabled ? Qt::Checked : Qt::Unchecked);
        rules_table->setItem(i, kOn, on);
        auto name = new QTableWidgetItem(qs(r.name));
        name->setToolTip(qs(describe_rule(r)));
        rules_table->setItem(i, kName, name);
        auto slider = new IntensitySlider;
        slider->setIntensity(r.intensity);
        slider->setAccessibleName(tr("Strength of %1").arg(qs(r.name)));
        slider->onChanged = [this, i](double v) { control.setRuleIntensity(size_t(i), v); };
        rules_table->setCellWidget(i, kStrength, slider);
        rule_sliders.push_back(slider);
    }
    fitRulesTable();
    rules_table->setVisible(!status.rules.empty());
}

void Cockpit::fitRulesTable()
{
    // Exactly as tall as its rows: no blank strip that looks like an empty rule.
    rules_table->resizeRowsToContents();
    int height = rules_table->horizontalHeader()->sizeHint().height() + 2 * rules_table->frameWidth();
    for (int r = 0; r < rules_table->rowCount(); r++)
        height += rules_table->rowHeight(r);
    rules_table->setFixedHeight(height);
}
