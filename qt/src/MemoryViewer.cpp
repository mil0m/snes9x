#include "MemoryViewer.hpp"
#include "UiKit.hpp"
#include "AreaImage.hpp"
#include "MemoryView.hpp"
#include "PixelView.hpp"
#include "TileView.hpp"
#include "RuleEditor.hpp"
#include "corrupt/rule_text.h"

#include <QAction>
#include <QComboBox>
#include <QKeySequence>
#include <QDir>
#include <QFileDialog>
#include <QFontDatabase>
#include <QInputDialog>
#include <QMessageBox>
#include <QToolButton>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>

using namespace corrupt;

namespace
{

// Accesses a frame, short: 1234, 3.0, 0.05, 0.
QString rateText(float per_frame)
{
    if (per_frame >= 10.0f)
        return QString::number(std::lround(per_frame));
    if (per_frame >= 0.095f)
        return QString::number(per_frame, 'f', 1);
    if (per_frame >= 0.001f)
        return QString::number(per_frame, 'f', 2 + (per_frame < 0.0095f));
    return "0";
}

// Addresses as everywhere else: as many digits as the memory can need.
QString hexa(uint32_t v, Target t)
{
    return QString::fromStdString(hex_address(v, t));
}

} // namespace

MemoryViewer::MemoryViewer(MemorySource &source, QWidget *parent)
    : QWidget(parent, Qt::Window), source(source)
{
    setWindowTitle(tr("Memory Viewer"));
    auto layout = new QVBoxLayout(this);

    auto top = new QHBoxLayout;
    mode_combo = new QComboBox(this);
    mode_combo->setObjectName("mode");
    mode_combo->addItem(tr("Hex"), QString("hex"));
    mode_combo->addItem(tr("Pixels"), QString("pixels"));
    mode_combo->addItem(tr("Tiles"), QString("tiles"));
    mode_combo->setToolTip(tr("Pixels: every byte is one dot coloured by its value, so a whole memory fits on screen.\n"
                              "Tiles: the bytes decoded as SNES graphics, in the game's own colours."));
    zoom_out_button = new QPushButton(this);
    zoom_out_button->setObjectName("zoom_out");
    zoom_out_button->setIcon(ui::icon("zoom-out"));
    zoom_out_button->setToolTip(tr("Zoom out (Ctrl+wheel)"));
    zoom_out_button->setAccessibleName(tr("Zoom out"));
    zoom_in_button = new QPushButton(this);
    zoom_in_button->setObjectName("zoom_in");
    zoom_in_button->setIcon(ui::icon("zoom-in"));
    zoom_in_button->setToolTip(tr("Zoom in (Ctrl+wheel)"));
    zoom_in_button->setAccessibleName(tr("Zoom in"));
    zoom_out_button->setAutoDefault(false);
    zoom_in_button->setAutoDefault(false);
    zoom_label = new QLabel(this);
    zoom_label->setObjectName("zoom");
    row_width = new QSpinBox(this);
    row_width->setObjectName("row_width");
    row_width->setRange(0, PixelView::kMaxRowWidth);
    row_width->setPrefix(tr("Row "));
    row_width->setSuffix(tr(" bytes"));
    row_width->setSpecialValueText(tr("Row: Auto"));
    row_width->setAccessibleName(tr("Row width in bytes"));
    row_width->setToolTip(tr("Bytes per row of dots. Step it one at a time (arrows or wheel) until the picture "
                             "lines up into columns: then a row of dots is a row of the game's data. 0 is Auto."));
    format_combo = new QComboBox(this);
    format_combo->setObjectName("tile_format");
    for (auto f : { TileFormat::Bpp2, TileFormat::Bpp4, TileFormat::Bpp8, TileFormat::Mode7 })
        format_combo->addItem(tile_format_name(f), QString(tile_format_id(f)));
    format_combo->setCurrentIndex(1); // 4 bpp: most backgrounds and every sprite
    format_combo->setAccessibleName(tr("Tile format"));
    format_combo->setToolTip(tr("How the tiles are stored. Clicking a tile area in the Map picks this for you."));
    palette_spin = new QSpinBox(this);
    palette_spin->setObjectName("palette");
    palette_spin->setPrefix(tr("Palette "));
    palette_spin->setSpecialValueText(tr("Palette: Grey"));
    palette_spin->setAccessibleName(tr("Palette row"));
    palette_spin->setToolTip(tr("Which row of the game's palette colours the tiles. Sprites use 8-15 at 4 bpp. "
                                "Grey shows the raw colour numbers."));
    per_row_spin = new QSpinBox(this);
    per_row_spin->setObjectName("tiles_per_row");
    per_row_spin->setRange(1, TileView::kMaxTilesPerRow);
    per_row_spin->setValue(16);
    per_row_spin->setSuffix(tr(" per row"));
    per_row_spin->setAccessibleName(tr("Tiles per row"));
    per_row_spin->setToolTip(tr("Tiles per row. Big sprites and fonts often line up at 16 or 32."));
    target_combo = new QComboBox(this);
    target_combo->setObjectName("target");
    for (auto t : kAllTargets)
        target_combo->addItem(targetLabel(t), QString(target_name(t)));
    overlay_combo = new QComboBox(this);
    overlay_combo->setObjectName("overlay");
    overlay_combo->addItem(tr("Activity: what the game changes"), QString("activity"));
    overlay_combo->addItem(tr("Hits: what the recipe wrote"), QString("hits"));
    overlay_combo->addItem(tr("Danger: where corruption crashed the game"), QString("danger"));
    overlay_combo->addItem(tr("Crashes: how often corruption here crashed it"), QString("crashes"));
    overlay_combo->addItem(tr("Reads: how often the game reads it (WRAM)"), QString("reads"));
    overlay_combo->addItem(tr("Writes: how often the game writes it (WRAM)"), QString("writes"));
    overlay_combo->addItem(tr("Code: what the CPU runs (ROM)"), QString("code"));
    overlay_combo->addItem(tr("Data: what the CPU reads as data (ROM)"), QString("data"));
    overlay_combo->addItem(tr("DMA: what DMA copies to the PPU (ROM)"), QString("dma"));
    overlay_combo->addItem(tr("Changes: what differs from the clean ROM, bright once used"), QString("changes"));
    overlay_combo->addItem(tr("Jumps: the game's tables of routines, for pointer swaps (ROM)"), QString("jumps"));
    overlay_combo->addItem(tr("Live: what the game runs and reads right now (ROM)"), QString("live"));
    overlay_combo->addItem(tr("Functions: the game's functions, guarded ones stronger (ROM)"), QString("functions"));
    overlay_combo->addItem(tr("Plain"), QString("none"));
    goto_edit = new QLineEdit(this);
    goto_edit->setObjectName("goto");
    goto_edit->setPlaceholderText(tr("Go to 0x…"));
    goto_edit->setAccessibleName(tr("Go to address"));
    goto_edit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    // Wide enough for its hint and a ROM address, in either font.
    goto_edit->setMinimumWidth(std::max(goto_edit->fontMetrics().horizontalAdvance("0x000000"),
                                        fontMetrics().horizontalAdvance(goto_edit->placeholderText())) + 24);
    goto_edit->setMaximumWidth(goto_edit->minimumWidth() + 40);
    clear_hits_button = new QPushButton(ui::icon("eraser"), tr("Clear Hits"), this);
    auto clear = clear_hits_button;
    clear->setObjectName("clear_hits");
    clear->setToolTip(tr("Forget what the recipe wrote so far, to see only what it hits from now on"));
    target_combo->setToolTip(tr("Which memory to look at: wram is the game's working memory (positions, health, "
                                "timers), vram graphics, cgram colours, oam sprites, aram sound, sram the battery "
                                "save, rom the game itself"));
    overlay_combo->setToolTip(tr("What the colours on top of the bytes show; the line under the view explains them"));
    goto_edit->setToolTip(tr("Type an offset like 0x1A00 and press Enter"));
    // Each label names its control for screen readers and moves focus there on its mnemonic.
    auto labelled = [&](const QString &text, QWidget *control, int stretch = 0) {
        auto label = new QLabel(text, this);
        label->setBuddy(control);
        top->addWidget(label);
        top->addWidget(control, stretch);
    };
    labelled(tr("&Memory"), target_combo);
    labelled(tr("&Show as"), mode_combo);
    labelled(tr("&Colour by"), overlay_combo, 1);
    top->addWidget(goto_edit);
    top->addWidget(clear);
    layout->addLayout(top);

    // How Pixels and Tiles draw, on a row of its own that Hex doesn't need.
    view_options = new QWidget(this);
    view_options->setObjectName("view_options");
    auto options = new QHBoxLayout(view_options);
    options->setContentsMargins(0, 0, 0, 0);
    options->addWidget(new QLabel(tr("Zoom"), view_options));
    options->addWidget(zoom_out_button);
    options->addWidget(zoom_label);
    options->addWidget(zoom_in_button);
    options->addSpacing(12);
    options->addWidget(row_width);
    paint_button = new QPushButton(tr("Paint Mode"), view_options);
    paint_button->setObjectName("paint");
    paint_button->setCheckable(true);
    paint_button->setToolTip(tr("Drag to paint bytes with the value. On the ROM it's a draft until Apply Paint; "
                                "other memories change as you drag. Painted bytes are kept in the recipe. "
                                "Alt-click picks the value under the mouse; Cmd+Z undoes a stroke."));
    paint_value = new QSpinBox(view_options);
    paint_value->setObjectName("paint_value");
    paint_value->setRange(0, 255);
    paint_value->setDisplayIntegerBase(16);
    paint_value->setPrefix(tr("Value 0x"));
    paint_value->setAccessibleName(tr("Value to paint"));
    paint_value->setToolTip(tr("The value to paint (Alt-click a byte to pick its value)"));
    brush_spin = new QSpinBox(view_options);
    brush_spin->setObjectName("brush");
    brush_spin->setRange(1, 64);
    brush_spin->setPrefix(tr("Brush "));
    brush_spin->setAccessibleName(tr("Brush size"));
    brush_spin->setToolTip(tr("Brush size in bytes, square"));
    export_png_button = new QPushButton(tr("Export PNG…"), view_options);
    export_png_button->setObjectName("export_png");
    export_png_button->setToolTip(tr("Save the selection (or all of this memory) as a greyscale picture, one pixel a "
                                     "byte in the current row width, to edit in any image editor"));
    import_png_button = new QPushButton(tr("Import PNG…"), view_options);
    import_png_button->setObjectName("import_png");
    import_png_button->setToolTip(tr("Put an edited picture back where it was exported from: the pixels that "
                                     "changed become painted bytes in the recipe"));
    options->addSpacing(12);
    options->addWidget(paint_button);
    options->addWidget(paint_value);
    options->addWidget(brush_spin);
    options->addWidget(export_png_button);
    options->addWidget(import_png_button);
    options->addWidget(format_combo);
    options->addWidget(palette_spin);
    options->addWidget(per_row_spin);
    options->addStretch(1);
    layout->addWidget(view_options);

    // While painting: undo and redo, and for the ROM the draft and Apply Paint (#66).
    paint_bar = new QWidget(this);
    paint_bar->setObjectName("paint_bar");
    auto paint_row = new QHBoxLayout(paint_bar);
    paint_row->setContentsMargins(0, 0, 0, 0);
    undo_button = new QPushButton(ui::icon("arrow-back-up"), QString(), paint_bar);
    undo_button->setObjectName("undo_paint");
    undo_button->setToolTip(tr("Undo the last stroke (Cmd+Z)"));
    undo_button->setAccessibleName(tr("Undo stroke"));
    undo_button->setAutoDefault(false);
    redo_button = new QPushButton(ui::icon("arrow-forward-up"), QString(), paint_bar);
    redo_button->setObjectName("redo_paint");
    redo_button->setToolTip(tr("Redo it (Shift+Cmd+Z)"));
    redo_button->setAccessibleName(tr("Redo stroke"));
    redo_button->setAutoDefault(false);
    draft_label = new QLabel(paint_bar);
    draft_label->setObjectName("draft_status");
    draft_label->setWordWrap(true);
    apply_button = new QPushButton(ui::icon("bolt"), tr("Apply Paint"), paint_bar);
    apply_button->setObjectName("apply_paint");
    apply_button->setAutoDefault(false);
    apply_button->setToolTip(tr("Put the drafted bytes into the game together. For ten seconds after, a crash "
                                "takes them back out and returns them to the draft."));
    discard_button = new QPushButton(ui::icon("trash"), tr("Throw Away Draft"), paint_bar);
    discard_button->setObjectName("discard_draft");
    discard_button->setAutoDefault(false);
    discard_button->setToolTip(tr("Forget the drafted strokes (Undo brings them back)"));
    paint_row->addWidget(undo_button);
    paint_row->addWidget(redo_button);
    paint_row->addWidget(draft_label, 1);
    paint_row->addWidget(apply_button);
    paint_row->addWidget(discard_button);
    layout->addWidget(paint_bar);
    paint_bar->hide();
    auto undo_key = new QAction(tr("Undo Stroke"), this);
    undo_key->setObjectName("undo_paint_key");
    undo_key->setShortcut(QKeySequence::Undo);
    undo_key->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    addAction(undo_key);
    auto redo_key = new QAction(tr("Redo Stroke"), this);
    redo_key->setObjectName("redo_paint_key");
    redo_key->setShortcuts(QKeySequence::Redo);
    redo_key->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    addAction(redo_key);
    auto undo = [this] {
        if (this->source.undoPaint())
            refresh();
    };
    auto redo = [this] {
        if (this->source.redoPaint())
            refresh();
    };
    connect(undo_key, &QAction::triggered, this, undo);
    connect(redo_key, &QAction::triggered, this, redo);
    connect(undo_button, &QPushButton::clicked, this, undo);
    connect(redo_button, &QPushButton::clicked, this, redo);
    connect(apply_button, &QPushButton::clicked, this, [this] {
        auto err = this->source.applyPaint();
        if (!err.empty())
            selection_label->setText(tr("Couldn't apply: %1").arg(QString::fromStdString(err)));
        refresh();
    });
    connect(discard_button, &QPushButton::clicked, this, [this] {
        this->source.discardDraft();
        refresh();
    });

    auto middle = new QHBoxLayout;
    map_list = new QListWidget(this);
    map_list->setObjectName("map");
    map_list->setMinimumWidth(190);
    map_list->setMaximumWidth(340);
    // Whole names, wrapped: cutting them in the middle hid the useful part.
    map_list->setWordWrap(true);
    map_list->setTextElideMode(Qt::ElideNone);
    map_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    map_list->setAccessibleName(tr("What's where"));
    map_list->setToolTip(tr("What lives where in this memory. Click an area to select it; italic ones are read "
                            "from the game right now and can move."));
    stack = new QStackedWidget(this);
    view = new MemoryView(stack);
    view->setObjectName("view");
    pixels = new PixelView(stack);
    pixels->setObjectName("pixels");
    tiles = new TileView(stack);
    tiles->setObjectName("tiles");
    stack->addWidget(view);
    stack->addWidget(pixels);
    stack->addWidget(tiles);
    auto map_column = new QVBoxLayout;
    map_column->addWidget(ui::heading(tr("What's where"), this));
    map_column->addWidget(map_list, 1);
    history_box = new QWidget(this);
    history_box->setObjectName("history_box");
    auto history_layout = new QVBoxLayout(history_box);
    history_layout->setContentsMargins(0, 0, 0, 0);
    history_layout->addWidget(ui::heading(tr("Paint History"), history_box));
    history_list = new QListWidget(history_box);
    history_list->setObjectName("paint_history");
    history_list->setAccessibleName(tr("Paint history"));
    history_list->setToolTip(tr("Newest first. Click a stroke to go back to just after it; the ones after it can be "
                                "redone until you paint something new."));
    history_list->setWordWrap(true);
    history_list->setMaximumWidth(340);
    history_layout->addWidget(history_list);
    map_column->addWidget(history_box, 1);
    history_box->hide();
    connect(history_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        this->source.goToPaint(size_t(item->data(Qt::UserRole).toULongLong()) + 1);
        refresh();
    });
    middle->addLayout(map_column);
    middle->addWidget(stack, 1);
    layout->addLayout(middle, 1);
    legend_label = new QLabel(this);
    legend_label->setObjectName("legend");
    legend_label->setWordWrap(true);
    ui::setTone(legend_label, ui::Tone::Secondary);
    layout->addWidget(legend_label);

    auto bottom = new QHBoxLayout;
    selection_label = new QLabel(tr("Drag across bytes to select them (Alt-drag in Pixels: a rectangle)"), this);
    selection_label->setObjectName("selection");
    hover_label = new QLabel(this);
    hover_label->setObjectName("hover");
    hover_label->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    new_rule_button = new QPushButton(ui::icon("plus"), tr("New Rule from Selection…"), this);
    new_rule_button->setToolTip(tr("A rule that corrupts the selected bytes, ready to adjust"));
    new_rule_button->setObjectName("new_rule");
    new_rule_button->setEnabled(false);
    bottom->addWidget(selection_label, 1);
    bottom->addWidget(hover_label);
    bottom->addWidget(new_rule_button);
    layout->addLayout(bottom);

    // Two headed sections side by side, set apart by space rather than boxes.
    auto section = [this](const QString &title) {
        auto box = new QWidget(this);
        auto v = new QVBoxLayout(box);
        v->setContentsMargins(0, 0, 0, 0);
        v->addWidget(ui::heading(title, box));
        return std::pair{ box, v };
    };
    auto [copies_widget, copies_layout] = section(tr("Sprite and palette copies in WRAM"));
    copies_box = copies_widget;
    auto find = new QPushButton(ui::icon("search"), tr("Find Copies in WRAM"), copies_box);
    find->setObjectName("find_copies");
    find->setToolTip(tr("Most games rebuild sprites and colours from a copy in WRAM every frame, so corrupting "
                        "the copy sticks where corrupting oam or cgram only flashes."));
    copies_list = new QListWidget(copies_box);
    copies_list->setObjectName("copies");
    copies_list->setMaximumHeight(110);
    copies_list->setToolTip(tr("After Find Copies: where in WRAM the game keeps its sprite table and palette. Click one "
                               "to select it."));
    copies_layout->addWidget(find);
    copies_layout->addWidget(copies_list);
    // Until a search, a line saying what it's for rather than an empty box.
    auto copies_hint = new QLabel(tr("Not searched yet. Find Copies looks for the game's sprite table and palette "
                                     "in WRAM, the copies worth corrupting."),
                                  copies_box);
    copies_hint->setObjectName("copies_hint");
    copies_hint->setWordWrap(true);
    ui::setTone(copies_hint, ui::Tone::Secondary);
    copies_layout->addWidget(copies_hint);
    copies_layout->addStretch(1);
    copies_list->hide();

    auto [regions_box, regions_layout] = section(tr("Named areas"));
    regions_list = new QListWidget(regions_box);
    regions_list->setObjectName("regions");
    regions_list->setMaximumHeight(110);
    regions_list->setToolTip(tr("Names you gave parts of memory, for rules to aim at (\"enemy stats\"). Select bytes, "
                                "then Name Selection."));
    regions_empty = new QLabel(tr("None yet. Select bytes, then Name Selection to give them a name rules can use."),
                               regions_box);
    regions_empty->setWordWrap(true);
    ui::setTone(regions_empty, ui::Tone::Secondary);
    auto region_buttons = new QHBoxLayout;
    name_button = new QPushButton(ui::icon("tag"), tr("Name Selection…"), regions_box);
    name_button->setObjectName("name_selection");
    name_button->setEnabled(false);
    rename_button = new QPushButton(ui::icon("pencil"), tr("Rename…"), regions_box);
    delete_button = new QPushButton(ui::icon("trash"), tr("Delete…"), regions_box);
    region_buttons->addWidget(name_button); // the three together
    region_buttons->addWidget(rename_button);
    region_buttons->addWidget(delete_button);
    region_buttons->addStretch(1);
    regions_layout->addWidget(regions_empty);
    regions_layout->addWidget(regions_list);
    regions_layout->addStretch(1); // the heading stays at the top, the buttons at the bottom
    regions_layout->addLayout(region_buttons);

    auto lists = new QHBoxLayout;
    lists->setSpacing(ui::sectionGap(this));
    layout->addSpacing(ui::sectionGap(this) / 2);
    lists->addWidget(copies_box, 1);
    lists->addWidget(regions_box, 1);
    layout->addLayout(lists);

    connect(regions_list, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        auto name = item->data(Qt::UserRole).toString().toStdString();
        auto all = this->source.regions();
        auto it = all.find(name);
        if (it == all.end())
            return;
        pending_select = std::pair{ it->second.first, it->second.last };
        int index = target_combo->findData(QString(target_name(it->second.target)));
        if (target_combo->currentIndex() != index)
            target_combo->setCurrentIndex(index);
        else
            refresh();
    });
    connect(regions_list, &QListWidget::itemClicked, regions_list, &QListWidget::itemActivated);
    connect(map_list, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        int row = map_list->row(item);
        if (row < 0 || row >= int(shown_map.size()))
            return;
        auto &area = shown_map[size_t(row)];
        if (mode() == "tiles" && area.tile_format) // decode it the way the game does
        {
            format_combo->setCurrentIndex(format_combo->findData(QString(tile_format_id(*area.tile_format))));
            palette_spin->setValue(area.palette);
        }
        pane()->select(area.range.first, area.range.last);
    });
    connect(map_list, &QListWidget::itemClicked, map_list, &QListWidget::itemActivated);
    connect(name_button, &QPushButton::clicked, this, [this] { askNameSelection(); });
    connect(rename_button, &QPushButton::clicked, this, [this] { askRename(); });
    connect(delete_button, &QPushButton::clicked, this, [this] { askDelete(); });

    connect(target_combo, &QComboBox::currentIndexChanged, this, [this] { targetChanged(); });
    connect(overlay_combo, &QComboBox::currentIndexChanged, this, [this] {
        auto overlay = overlayNamed(overlay_combo->currentData().toString());
        view->setOverlay(overlay);
        pixels->setOverlay(overlay);
        tiles->setOverlay(overlay);
        showLegend();
    });
    connect(goto_edit, &QLineEdit::returnPressed, this, [this] { goTo(); });
    connect(clear, &QPushButton::clicked, this, [this] { this->source.clearHits(); });
    connect(find, &QPushButton::clicked, this, [this] { findCopies(); });
    connect(copies_list, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        if (!item->data(Qt::UserRole).isValid())
            return; // the "none found" line
        auto first = item->data(Qt::UserRole).toUInt();
        auto last = item->data(Qt::UserRole + 1).toUInt();
        pending_select = std::pair{ first, last };
        int wram = target_combo->findData(QString("wram"));
        if (target_combo->currentIndex() != wram)
            target_combo->setCurrentIndex(wram); // the selection lands with the WRAM snapshot
        else
            refresh();
    });
    connect(copies_list, &QListWidget::itemClicked, copies_list, &QListWidget::itemActivated);
    connect(new_rule_button, &QPushButton::clicked, this, [this] { newRule(); });
    view->onSelectionChanged = [this] { selectionChanged(); };
    pixels->onSelectionChanged = [this] { selectionChanged(); };
    pixels->onZoomChanged = [this] { zoomChanged(); };
    pixels->onHover = [this](std::optional<uint32_t> offset) { hovered(offset); };
    pixels->onPaint = [this](const std::vector<uint32_t> &offsets) {
        auto err = this->source.paint(target(), offsets, uint8_t(paint_value->value()));
        if (!err.empty())
            selection_label->setText(tr("Couldn't paint: %1").arg(QString::fromStdString(err)));
    };
    pixels->onPick = [this](uint8_t v) { paint_value->setValue(v); };
    pixels->onStrokeEnd = [this] {
        this->source.endStroke();
        refresh();
    };
    connect(paint_button, &QPushButton::toggled, this, [this](bool on) {
        pixels->setPaintMode(on);
        paint_value->setVisible(on); // the paint tools only while painting
        brush_spin->setVisible(on);
        paintToolsShown();
    });
    connect(export_png_button, &QPushButton::clicked, this, [this] {
        auto path = QFileDialog::getSaveFileName(this, tr("Export PNG"),
                                                 QDir::home().filePath(QString("%1.png").arg(target_name(target()))),
                                                 tr("PNG picture (*.png)"));
        if (!path.isEmpty())
            exportPng(path);
    });
    connect(import_png_button, &QPushButton::clicked, this, [this] {
        auto path = QFileDialog::getOpenFileName(this, tr("Import PNG"), QDir::homePath(), tr("PNG picture (*.png)"));
        if (!path.isEmpty())
            importPng(path);
    });
    connect(paint_value, &QSpinBox::valueChanged, this, [this](int v) { pixels->setPaintValue(uint8_t(v)); });
    connect(brush_spin, &QSpinBox::valueChanged, this, [this](int v) { pixels->setBrush(v); });
    connect(mode_combo, &QComboBox::currentIndexChanged, this, [this] { modeChanged(); });
    connect(row_width, &QSpinBox::valueChanged, this, [this](int bytes) {
        row_widths[size_t(target())] = bytes;
        pixels->setRowWidth(bytes);
    });
    tiles->onSelectionChanged = [this] { selectionChanged(); };
    tiles->onZoomChanged = [this] { zoomChanged(); };
    tiles->onHover = [this](std::optional<uint32_t> tile) { tileHovered(tile); };
    connect(format_combo, &QComboBox::currentIndexChanged, this, [this] {
        auto f = parse_tile_format(format_combo->currentData().toString().toStdString()).value_or(TileFormat::Bpp4);
        tiles->setFormat(f);
        palette_spin->setRange(TileView::kGrey, palette_count(f) - 1);
        tiles->setPaletteRow(palette_spin->value());
    });
    palette_spin->setRange(TileView::kGrey, palette_count(tiles->format()) - 1);
    palette_spin->setValue(0);
    connect(palette_spin, &QSpinBox::valueChanged, this, [this](int row) { tiles->setPaletteRow(row); });
    connect(per_row_spin, &QSpinBox::valueChanged, this, [this](int n) { tiles->setTilesPerRow(n); });
    connect(zoom_in_button, &QPushButton::clicked, this, [this] {
        if (mode() == "tiles")
            tiles->zoomIn();
        else
            pixels->zoomIn();
    });
    connect(zoom_out_button, &QPushButton::clicked, this, [this] {
        if (mode() == "tiles")
            tiles->zoomOut();
        else
            pixels->zoomOut();
    });
    modeChanged();
    zoomChanged();

    connect(&timer, &QTimer::timeout, this, [this] {
        // A ROM is megabytes and only changes when a rule writes it: twice a second is plenty.
        if (pane()->snapshot().bytes.size() > 0x100000 && ++slow_ticks % 5)
            return;
        refresh();
    });
    resize(std::max(900, view->sizeHint().width() + 260), 700);
}

Target MemoryViewer::target() const
{
    return parse_target(target_combo->currentData().toString().toStdString()).value_or(Target::WRAM);
}

QString MemoryViewer::mode() const
{
    return mode_combo->currentData().toString();
}

MemoryPane *MemoryViewer::pane() const
{
    if (stack->currentWidget() == pixels)
        return pixels;
    if (stack->currentWidget() == tiles)
        return tiles;
    return view;
}

void MemoryViewer::modeChanged()
{
    QString m = mode();
    MemoryPane *from = pane();
    QWidget *to_widget = m == "pixels" ? static_cast<QWidget *>(pixels) : m == "tiles" ? static_cast<QWidget *>(tiles) : view;
    MemoryPane *to = m == "pixels" ? static_cast<MemoryPane *>(pixels) : m == "tiles" ? static_cast<MemoryPane *>(tiles) : view;
    view_options->setVisible(m != "hex");
    hover_label->setVisible(m != "hex");
    row_width->setVisible(m == "pixels");
    for (QWidget *w : { static_cast<QWidget *>(paint_button), static_cast<QWidget *>(export_png_button),
                        static_cast<QWidget *>(import_png_button) })
        w->setVisible(m == "pixels");
    paint_value->setVisible(m == "pixels" && paint_button->isChecked());
    brush_spin->setVisible(m == "pixels" && paint_button->isChecked());
    if (m != "pixels" && paint_button->isChecked())
        paint_button->click(); // painting only works in the pixel view
    for (QWidget *w : { static_cast<QWidget *>(format_combo), static_cast<QWidget *>(palette_spin),
                        static_cast<QWidget *>(per_row_spin) })
        w->setVisible(m == "tiles");
    hover_label->clear();
    if (legend_label)
        showLegend();
    if (to != from)
    {
        // The other view picks up where this one is: same bytes, same place, same selection.
        auto sel = from->selection();
        uint32_t place = from->firstVisibleOffset();
        to->setSnapshot(from->snapshot());
        if (to == tiles)
            tiles->setCgram(source.palette());
        stack->setCurrentWidget(to_widget);
        to->scrollTo(place);
        if (sel)
            to->select(sel->first, sel->second);
        else
            to->clearSelection();
    }
    zoomChanged();
}

void MemoryViewer::zoomChanged()
{
    bool on_tiles = mode() == "tiles";
    int zoom = on_tiles ? tiles->zoom() : pixels->zoom();
    zoom_label->setText(tr("%1×").arg(zoom));
    zoom_out_button->setEnabled(zoom > (on_tiles ? TileView::kMinZoom : PixelView::kMinZoom));
    zoom_in_button->setEnabled(zoom < (on_tiles ? TileView::kMaxZoom : PixelView::kMaxZoom));
}

void MemoryViewer::showRange(Target t, uint32_t first, uint32_t last, const QString &overlay)
{
    if (mode() == "tiles") // bytes, not graphics: pixels show code and its colours best
        mode_combo->setCurrentIndex(mode_combo->findData(QString("pixels")));
    pending_select = std::pair{ first, last };
    int index = target_combo->findData(QString(target_name(t)));
    if (target_combo->currentIndex() != index)
        target_combo->setCurrentIndex(index);
    if (!overlay.isEmpty()) // after the target: switching target filters the overlays
        if (int i = overlay_combo->findData(overlay); i >= 0)
            overlay_combo->setCurrentIndex(i);
    refresh();
}

void MemoryViewer::hovered(std::optional<uint32_t> offset)
{
    auto &snap = pixels->snapshot();
    if (!offset || *offset >= snap.bytes.size())
        return hover_label->clear();
    auto text = QString("%1 = 0x%2").arg(hexa(*offset, snap.target)).arg(snap.bytes[*offset], 2, 16, QChar('0')).toUpper().replace("0X", "0x");
    // How often the game reads and writes it (WRAM only), whatever the overlay.
    if (*offset < snap.reads.size() && *offset < snap.writes.size())
        text += tr(" · read %1, written %2 a frame").arg(rateText(snap.reads[*offset]), rateText(snap.writes[*offset]));
    // What the code map knows about it (ROM only), whatever the overlay.
    if (uint8_t f = *offset < snap.code.size() ? snap.code[*offset] : 0)
    {
        if (auto in = instruction_at(snap.bytes, snap.code, *offset, snap.rom_where))
        {
            QString bytes;
            for (int i = 0; i < in->length && in->start + i < snap.bytes.size(); i++)
                bytes += QString("%1 ").arg(snap.bytes[in->start + i], 2, 16, QChar('0')).toUpper();
            QString where = in->address ? QString("CPU $%1:%2")
                                              .arg(*in->address >> 16, 2, 16, QChar('0'))
                                              .arg(*in->address & 0xFFFF, 4, 16, QChar('0'))
                                              .toUpper()
                                        : hexa(in->start, snap.target);
            text += tr(" · %1  %2 %3 (%4)").arg(where, bytes, QString::fromStdString(in->text), class_name(in->cls));
            if (in->both_widths)
                text += tr(", runs 8- and 16-bit");
        }
        if (f & codeflag::Data)
            text += tr(" · read as data");
        if (f & codeflag::Dma)
            text += tr(" · copied by DMA");
    }
    // Live heat (ROM only), whatever the overlay.
    if (*offset < snap.live.size() && snap.live[*offset])
        text += snap.live[*offset] > 200 ? tr(" · in use right now") : tr(" · used in the last second or two");
    // Jump tables (ROM only): which entry of which dispatcher's table it is, and where it goes.
    if (!snap.jump_tables.empty())
    {
        auto cpu = [](uint32_t a) {
            return QString("$%1:%2").arg(a >> 16, 2, 16, QChar('0')).arg(a & 0xFFFF, 4, 16, QChar('0')).toUpper();
        };
        auto at = jump_at(snap.jump_tables, *offset);
        if (at.entry)
        {
            auto &t = *at.table;
            QString use = !at.entry->seen         ? tr("likely: nothing jumped through it yet, but it points at code that ran")
                          : at.entry->hits == 1 ? tr("jumped through once")
                                                : tr("jumped through %1 times").arg(QLocale().toString(qulonglong(at.entry->hits)));
            auto site = instruction_at(snap.bytes, snap.code, t.site, snap.rom_where);
            QString by = site && site->address ? tr("the %1 at %2").arg(QString::fromStdString(site->text), cpu(*site->address))
                                               : tr("the jump at %1").arg(hexa(t.site, snap.target));
            text += tr(" · entry %1 of %2 in a jump table for %3%4: goes to %5, %6")
                        .arg(at.index + 1)
                        .arg(t.entries.size())
                        .arg(by, t.kind == JumpKind::ThroughRam ? tr(" (copied to RAM first)") : QString(), cpu(at.entry->target), use);
        }
        else if (at.site_tables)
            text += tr(" · jumps through a table of routines: %1 entries known (Jumps shows them)").arg(at.site_entries);
    }
    // Which function's code it is (ROM only): its names, size, and whether it's guarded or off.
    if (auto *fn = function_at(snap.function_list, *offset))
    {
        QString where = fn->address ? QString("$%1:%2")
                                          .arg(*fn->address >> 16, 2, 16, QChar('0'))
                                          .arg(*fn->address & 0xFFFF, 4, 16, QChar('0'))
                                          .toUpper()
                                    : hexa(fn->entry, snap.target);
        text += tr(" · in function %1").arg(where);
        if (!fn->label.empty())
            text += " " + QString::fromStdString(fn->label);
        text += tr(" (%1 bytes)").arg(fn->bytes);
        if (fn->guarded)
            text += tr(", guarded");
        if (fn->off)
            text += tr(", off");
    }
    // Whether corruption changed it (ROM only), and whether the game has used it since.
    if (auto *c = snap.rom_change(*offset))
    {
        QString was = QString("%1").arg(c->was, 2, 16, QChar('0')).toUpper();
        text += !c->frames_used     ? tr(" · changed from 0x%1, not used yet").arg(was)
                : c->frames_used == 1 ? tr(" · changed from 0x%1, used in 1 frame").arg(was)
                                      : tr(" · changed from 0x%1, used in %2 frames").arg(was).arg(c->frames_used);
    }
    // What the crash map knows about the byte's block, whatever the overlay.
    size_t block = *offset / CrashMap::kBlock;
    if (block < snap.crash_blocks.size())
    {
        auto c = snap.crash_blocks[block];
        if (c.crashes || c.survivals)
        {
            text += tr(" · block %1: %2 crashes, %3 calm, danger %4")
                        .arg(hexa(uint32_t(block * CrashMap::kBlock), snap.target))
                        .arg(c.crashes)
                        .arg(c.survivals)
                        .arg(CrashMap::danger_of(c), 0, 'f', 2);
            if (block < snap.hot_blocks.size() && snap.hot_blocks[block])
                text += tr(", hot");
        }
    }
    hover_label->setText(text);
}

void MemoryViewer::tileHovered(std::optional<uint32_t> tile)
{
    if (!tile)
        return hover_label->clear();
    hover_label->setText(tr("tile 0x%1 at %2")
                             .arg(QString::number(*tile, 16).toUpper().rightJustified(3, QChar('0')))
                             .arg(hexa(*tile * tiles->tileSpan(), pane()->snapshot().target)));
}

void MemoryViewer::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    open = true;
    source.watch(target());
    refreshRegions();
    refresh();
    timer.start(100);
}

void MemoryViewer::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    open = false;
    timer.stop();
    source.stop();
}

void MemoryViewer::targetChanged()
{
    filterOverlays();
    row_width->setValue(row_widths[size_t(target())]); // each memory keeps its own
    if (open)
        source.watch(target());
    refresh();
}

MemoryPane::Overlay MemoryViewer::overlayNamed(const QString &o)
{
    return o == "hits"      ? MemoryPane::Overlay::Hits
           : o == "danger"  ? MemoryPane::Overlay::Danger
           : o == "crashes" ? MemoryPane::Overlay::Crashes
           : o == "reads"   ? MemoryPane::Overlay::Reads
           : o == "writes"  ? MemoryPane::Overlay::Writes
           : o == "code"    ? MemoryPane::Overlay::Code
           : o == "data"    ? MemoryPane::Overlay::Data
           : o == "dma"     ? MemoryPane::Overlay::Dma
           : o == "changes" ? MemoryPane::Overlay::Changes
           : o == "jumps"   ? MemoryPane::Overlay::Jumps
           : o == "live"    ? MemoryPane::Overlay::Live
           : o == "functions" ? MemoryPane::Overlay::Functions
           : o == "none"    ? MemoryPane::Overlay::None
                            : MemoryPane::Overlay::Activity;
}

// Overlays only make sense for some memories: the game's own reads and writes are counted
// for WRAM, the code map and changes are about the ROM.
void MemoryViewer::filterOverlays()
{
    QString t = target_combo->currentData().toString();
    auto view = qobject_cast<QListView *>(overlay_combo->view());
    for (int i = 0; i < overlay_combo->count(); i++)
    {
        QString o = overlay_combo->itemData(i).toString();
        bool rom_only = o == "code" || o == "data" || o == "dma" || o == "changes" || o == "jumps" || o == "live" ||
                        o == "functions";
        bool fits = (o != "reads" && o != "writes" && !rom_only) ||
                    ((o == "reads" || o == "writes") && t == "wram") || (t == "rom" && o != "reads" && o != "writes");
        if (view)
            view->setRowHidden(i, !fits);
        if (!fits && overlay_combo->currentIndex() == i)
            overlay_combo->setCurrentIndex(overlay_combo->findData(QString("activity")));
    }
    copies_box->setVisible(t == "wram" || t == "oam" || t == "cgram");
    showLegend();
}

void MemoryViewer::showLegend()
{
    QString o = overlay_combo->currentData().toString();
    auto swatch = [](QColor c) {
        return QString("<span style='background:%1'>&nbsp;&nbsp;&nbsp;</span> ").arg(c.name());
    };
    QString text = o == "activity" ? tr("brighter: the game changes it more often (it fades when it stops)")
                   : o == "hits"     ? tr("bytes the recipe wrote since you opened this (Clear Hits starts over)")
                   : o == "danger"   ? tr("how often corrupting here crashed the game; outlined: dangerous, rules set to avoid danger skip it")
                   : o == "crashes"  ? tr("how many crashes corruption here caused")
                   : o == "reads"    ? tr("how often the game reads each byte")
                   : o == "writes"   ? tr("how often the game writes each byte")
                   : o == "code"     ? tr("code the game ran: instructions strong, their values lighter")
                   : o == "data"     ? tr("bytes the game read as data: tables, text, stats")
                   : o == "dma"      ? tr("bytes copied straight to the graphics chip: tiles, maps, colours")
                   : o == "changes"  ? tr("bytes that differ from the clean ROM, bright once the game used them")
                   : o == "live"     ? tr("what the game runs and reads right now: bright this frame, fading over a "
                                          "second and a half")
                   : o == "jumps"    ? tr("jump tables: entries the game jumped through strong, likely ones lighter, "
                                          "the instruction that jumps in between")
                   : o == "functions" ? tr("the code of the game's functions: light, guarded from other corruption "
                                           "stronger (paint and rules leave it alone), the entry of one that's off "
                                           "strongest")
                                     : tr("no colours: just the bytes");
    auto overlay = overlayNamed(o);
    QString base = mode() == "pixels" ? tr(" Under it, each dot's grey is the byte's value.")
                   : mode() == "tiles" ? tr(" Under it, the bytes drawn as the game's graphics.")
                                       : QString();
    legend_label->setText((o == "none" ? QString() : swatch(MemoryPane::overlayColor(overlay))) + text + "." + base +
                          tr(" Hover a byte to see what it is."));
    clear_hits_button->setVisible(o == "hits");
}

void MemoryViewer::refresh()
{
    auto snap = source.snapshot();
    if (snap.target != target() && !snap.bytes.empty())
        return; // the switch hasn't reached the emulation thread yet
    pane()->setSnapshot(snap);
    if (mode() == "tiles")
        tiles->setCgram(source.palette()); // live, so palette corruption shows at once
    refreshMap();
    refreshPaint();
    if (pending_select && !snap.bytes.empty())
    {
        pane()->select(pending_select->first, pending_select->second);
        pending_select.reset();
    }
}

namespace
{
QString count(size_t n, const QString &one, const QString &many)
{
    return n == 1 ? one : many.arg(QLocale().toString(qulonglong(n)));
}
} // namespace

void MemoryViewer::paintToolsShown()
{
    bool on = mode() == "pixels" && paint_button->isChecked();
    paint_bar->setVisible(on);
    history_box->setVisible(on);
    if (on)
        refreshPaint();
}

void MemoryViewer::refreshPaint()
{
    if (!paint_bar->isVisible())
        return;
    bool rom = target() == corrupt::Target::ROM;
    size_t drafted = source.draftSize();
    bool watching = source.paintWatching();
    apply_button->setVisible(rom);
    discard_button->setVisible(rom);
    apply_button->setEnabled(drafted > 0);
    discard_button->setEnabled(drafted > 0);
    apply_button->setDefault(drafted > 0);
    undo_button->setEnabled(source.canUndoPaint());
    redo_button->setEnabled(source.canRedoPaint());
    QString status;
    if (rom && drafted)
        status = count(drafted, tr("1 byte drafted: Apply Paint puts it in the game"),
                       tr("%1 bytes drafted: Apply Paint puts them in the game"));
    else if (rom && watching)
        status = tr("Watching ten seconds for a crash…");
    else if (rom)
        status = tr("Paint here is a draft: it goes into the game when you press Apply Paint");
    else
        status = tr("Painted live, into the game as you drag. Cmd+Z undoes a stroke.");
    if (rom && drafted && watching)
        status += tr(" · watching the last batch for a crash");
    // What became of the last Apply (crashed and taken back, held up), until the next stroke.
    auto news = QString::fromStdString(source.paintNews());
    bool crashed = news.contains("crashed");
    if (rom && !news.isEmpty() && !(watching && !news.startsWith("Applied")))
        status = news;
    draft_label->setText(status);
    ui::setTone(draft_label, crashed ? ui::Tone::Warning : ui::Tone::Normal);

    auto rows = source.paintRows();
    QString key;
    for (auto &r : rows)
        key += QString("%1:%2:%3:%4:%5;").arg(r.index).arg(int(r.kind)).arg(r.active).arg(r.bytes).arg(r.strokes);
    if (key == shown_history)
        return;
    shown_history = key;
    history_list->clear();
    QColor quiet = ui::toneColor(ui::Tone::Secondary, palette());
    for (auto &r : rows)
    {
        QString where = QString::fromStdString(corrupt::hex_address(r.first, r.target));
        QString value = r.value < 0 ? tr("several values") : QString("%1").arg(r.value, 2, 16, QChar('0')).toUpper();
        QString text;
        switch (r.kind)
        {
        case corrupt::PaintEntry::Draft:
            text = tr("Stroke · %1 at %2 → %3").arg(count(r.bytes, tr("1 byte"), tr("%1 bytes")), where, value);
            break;
        case corrupt::PaintEntry::Live:
            text = tr("Stroke · %1 of %2 at %3 → %4")
                       .arg(count(r.bytes, tr("1 byte"), tr("%1 bytes")), QString(corrupt::target_name(r.target)), where,
                            value);
            break;
        case corrupt::PaintEntry::Applied:
            text = tr("Applied · %1, %2")
                       .arg(count(r.strokes, tr("1 stroke"), tr("%1 strokes")), count(r.bytes, tr("1 byte"), tr("%1 bytes")));
            if (r.skipped)
                text += tr(" (%1 guarded, left as they were)").arg(r.skipped);
            break;
        case corrupt::PaintEntry::Discarded:
            text = tr("Thrown away · %1").arg(count(r.strokes, tr("1 stroke"), tr("%1 strokes")));
            break;
        }
        auto item = new QListWidgetItem(text, history_list);
        item->setData(Qt::UserRole, qulonglong(r.index));
        if (!r.active)
        {
            item->setForeground(quiet);
            item->setToolTip(tr("Undone: Redo, or click it, brings it back"));
        }
    }
    if (rows.empty())
    {
        auto item = new QListWidgetItem(tr("Nothing painted yet"), history_list);
        item->setFlags(Qt::NoItemFlags);
        item->setForeground(quiet);
    }
}

void MemoryViewer::goTo()
{
    auto n = parse_number(goto_edit->text().toStdString());
    if (n && *n >= 0)
        pane()->scrollTo(uint32_t(*n));
}

void MemoryViewer::selectionChanged()
{
    auto sel = pane()->selection();
    new_rule_button->setEnabled(sel.has_value());
    name_button->setEnabled(sel.has_value());
    if (!sel)
    {
        selection_label->setText(tr("Drag across bytes to select them (Alt-drag in Pixels: a rectangle)"));
        return;
    }
    if (auto rect = mode() == "pixels" ? pixels->rectSelection() : std::nullopt)
    {
        selection_label->setText(tr("%1 %2x%3 at %4 in rows of %5 (%6 bytes)")
                                     .arg(target_name(pane()->snapshot().target))
                                     .arg(rect->width)
                                     .arg(rect->height)
                                     .arg(hexa(rect->start, pane()->snapshot().target))
                                     .arg(rect->row)
                                     .arg(rect->width * rect->height));
        return;
    }
    selection_label->setText(QString("%1 %2..%3 (%4 bytes)")
                                 .arg(target_name(pane()->snapshot().target), hexa(sel->first, pane()->snapshot().target), hexa(sel->second, pane()->snapshot().target))
                                 .arg(sel->second - sel->first + 1));
}

Rule MemoryViewer::ruleFromSelection() const
{
    Rule r = default_rule();
    if (auto rect = mode() == "pixels" ? pixels->rectSelection() : std::nullopt)
    {
        set_rect(r, pane()->snapshot().target, rect->start, Rect{ rect->row, rect->width, rect->height });
        r.name = std::string(target_name(r.range.target)) + " " + std::to_string(rect->width) + "x" +
                 std::to_string(rect->height) + " at " + hexa(rect->start, pane()->snapshot().target).toStdString();
    }
    else if (auto sel = pane()->selection())
    {
        r.range = { pane()->snapshot().target, sel->first, sel->second };
        r.name = std::string(target_name(r.range.target)) + " " + hexa(sel->first, pane()->snapshot().target).toStdString();
    }
    return r;
}

namespace
{
// What Export PNG covers: the rectangle, the selected range in rows, or the whole memory.
ImageArea exportArea(const PixelView &pixels)
{
    auto &snap = pixels.snapshot();
    auto t = snap.target;
    auto w = uint32_t(pixels.columns());
    if (auto rect = pixels.rectSelection())
        return { { t, rect->start, rect->row, rect->width, rect->height }, rect->last() };
    uint32_t first = 0, last = snap.bytes.empty() ? 0 : uint32_t(snap.bytes.size() - 1);
    if (auto sel = pixels.selection())
    {
        first = sel->first;
        last = sel->second;
    }
    uint32_t rows = (last - first + w) / w;
    return { { t, first, w, w, rows }, last };
}
} // namespace

void MemoryViewer::exportPng(const QString &path)
{
    auto where = exportArea(*pixels);
    if (!areaToImage(pixels->snapshot().bytes, where).save(path, "PNG"))
        selection_label->setText(tr("Couldn't write %1").arg(path));
    else
        selection_label->setText(tr("Exported %1x%2 bytes to %3").arg(where.area.width).arg(where.area.height).arg(path));
}

void MemoryViewer::importPng(const QString &path)
{
    QImage img(path);
    if (img.isNull())
    {
        selection_label->setText(tr("Couldn't read %1 as a picture").arg(path));
        return;
    }
    // Where it came from, if it says; else the selection, if it's the same size.
    auto where = imageArea(img).value_or(exportArea(*pixels));
    if (where.area.target != target())
    {
        selection_label->setText(tr("That picture is of %1: switch to it first").arg(target_name(where.area.target)));
        return;
    }
    QString error;
    auto bytes = imageToBytes(img, where, pixels->snapshot().bytes, &error);
    if (!bytes)
    {
        selection_label->setText(error);
        return;
    }
    auto err = source.setBytes(target(), *bytes);
    QString done = target() == corrupt::Target::ROM
                       ? count(bytes->size(), tr("Imported: 1 byte drafted. Apply Paint puts it in the game."),
                               tr("Imported: %1 bytes drafted. Apply Paint puts them in the game."))
                       : count(bytes->size(), tr("Imported: 1 byte changed, kept as paint"),
                               tr("Imported: %1 bytes changed, kept as paint"));
    selection_label->setText(err.empty() ? done : tr("Couldn't import: %1").arg(QString::fromStdString(err)));
}

void MemoryViewer::newRule()
{
    if (!pane()->selection())
        return;
    SizedMemory sizes;
    sizes.sizes = source.sizes();
    RuleEditor editor(ruleFromSelection(), source.regions(), sizes, this);
    editor.setWindowTitle(tr("New Rule from Selection"));
    if (editor.exec() != QDialog::Accepted)
        return;
    auto error = source.addRule(editor.rule());
    selection_label->setText(error.empty() ? tr("Added rule \"%1\" to the recipe").arg(QString::fromStdString(editor.rule().name))
                                           : tr("Couldn't add the rule: %1").arg(QString::fromStdString(error)));
}

void MemoryViewer::findCopies()
{
    if (auto hint = findChild<QLabel *>("copies_hint"))
        hint->hide();
    copies_list->show();
    copies_list->clear();
    for (auto of : { Target::OAM, Target::CGRAM })
        for (auto &run : source.findCopies(of))
        {
            auto text = QString("%1 → wram %2 (%3 bytes)").arg(target_name(of), hexa(run.haystack_offset, Target::WRAM)).arg(run.length);
            auto item = new QListWidgetItem(text, copies_list);
            item->setToolTip(QString("%1 %2..%3 is copied at wram %4..%5")
                                 .arg(target_name(of), hexa(run.needle_offset, of), hexa(run.needle_offset + run.length - 1, of),
                                      hexa(run.haystack_offset, Target::WRAM), hexa(run.haystack_offset + run.length - 1, Target::WRAM)));
            item->setData(Qt::UserRole, run.haystack_offset);
            item->setData(Qt::UserRole + 1, run.haystack_offset + run.length - 1);
        }
    if (copies_list->count() == 0)
        copies_list->addItem(tr("No copies found. Try again in the middle of gameplay."));
}

void MemoryViewer::refreshRegions()
{
    QString current = regions_list->currentItem() ? regions_list->currentItem()->data(Qt::UserRole).toString() : QString();
    regions_list->clear();
    for (auto &[name, range] : source.regions()) // a std::map: sorted by name
    {
        auto item = new QListWidgetItem(QString("%1 · %2 %3..%4")
                                            .arg(QString::fromStdString(name), target_name(range.target),
                                                 hexa(range.first, range.target), hexa(range.last, range.target)),
                                        regions_list);
        item->setData(Qt::UserRole, QString::fromStdString(name));
        if (item->data(Qt::UserRole).toString() == current)
            regions_list->setCurrentItem(item);
    }
    rename_button->setEnabled(regions_list->count() > 0);
    delete_button->setEnabled(regions_list->count() > 0);
    regions_empty->setVisible(regions_list->count() == 0);
    regions_list->setVisible(regions_list->count() > 0);
}

QString MemoryViewer::nameSelection(const QString &name, bool overwrite)
{
    auto sel = pane()->selection();
    if (!sel)
        return tr("select some bytes first");
    if (!overwrite && source.regions().count(name.toStdString()))
        return tr("there's already an area called '%1'").arg(name);
    auto error = source.saveRegion(name.toStdString(), { pane()->snapshot().target, sel->first, sel->second });
    refreshRegions();
    return QString::fromStdString(error);
}

QString MemoryViewer::renameRegion(const QString &from, const QString &to)
{
    auto error = source.renameRegion(from.toStdString(), to.toStdString());
    refreshRegions();
    return QString::fromStdString(error);
}

QString MemoryViewer::deleteRegion(const QString &name)
{
    auto error = source.deleteRegion(name.toStdString());
    refreshRegions();
    return QString::fromStdString(error);
}

QString MemoryViewer::usersText(const QString &name)
{
    auto users = source.regionUsers(name.toStdString());
    if (users.empty())
        return tr("No recipe uses it.");
    QStringList names;
    for (auto &u : users)
        names << QString::fromStdString(u);
    return tr("Used by: %1.").arg(names.join(", "));
}

void MemoryViewer::showResult(const QString &error, const QString &done)
{
    selection_label->setText(error.isEmpty() ? done : tr("Couldn't do that: %1").arg(error));
}

void MemoryViewer::askNameSelection()
{
    bool ok = false;
    auto name = QInputDialog::getText(this, tr("Name this area"), tr("Name (e.g. player stats):"), QLineEdit::Normal,
                                      QString(), &ok)
                    .trimmed();
    if (!ok || name.isEmpty())
        return;
    bool overwrite = false;
    if (source.regions().count(name.toStdString()))
    {
        if (QMessageBox::question(this, tr("Name this area"),
                                  tr("'%1' already exists. Point it at this selection instead?\n%2")
                                      .arg(name, usersText(name))) != QMessageBox::Yes)
            return;
        overwrite = true;
    }
    showResult(nameSelection(name, overwrite), tr("Named '%1'. Use it as a range in any rule for this game.").arg(name));
}

void MemoryViewer::askRename()
{
    auto item = regions_list->currentItem();
    if (!item)
        return showResult(tr("pick an area in the list first"), {});
    auto from = item->data(Qt::UserRole).toString();
    bool ok = false;
    auto to = QInputDialog::getText(this, tr("Rename area"), tr("New name for '%1':").arg(from), QLineEdit::Normal,
                                    from, &ok)
                  .trimmed();
    if (!ok || to.isEmpty() || to == from)
        return;
    showResult(renameRegion(from, to), tr("Renamed '%1' to '%2'; recipes using it follow.").arg(from, to));
}

void MemoryViewer::askDelete()
{
    auto item = regions_list->currentItem();
    if (!item)
        return showResult(tr("pick an area in the list first"), {});
    auto name = item->data(Qt::UserRole).toString();
    if (QMessageBox::question(this, tr("Delete area"),
                              tr("Delete '%1'? %2\nRecipes using it keep its range, just without the name.")
                                  .arg(name, usersText(name))) != QMessageBox::Yes)
        return;
    showResult(deleteRegion(name), tr("Deleted '%1'.").arg(name));
}

void MemoryViewer::refreshMap()
{
    auto map = source.memoryMap(target());
    bool same = map.size() == shown_map.size();
    for (size_t i = 0; same && i < map.size(); i++)
        same = map[i].name == shown_map[i].name && map[i].range.first == shown_map[i].range.first &&
               map[i].range.last == shown_map[i].range.last;
    if (same)
        return; // live areas are re-read 10 times a second; only redraw when one moved

    QString picked = map_list->currentItem() ? map_list->currentItem()->data(Qt::UserRole).toString() : QString();
    map_list->clear();
    shown_map = std::move(map);
    for (auto &area : shown_map)
    {
        auto item = new QListWidgetItem(QString("%1 · %2..%3").arg(QString::fromStdString(area.name),
                                                                    hexa(area.range.first, area.range.target), hexa(area.range.last, area.range.target)),
                                        map_list);
        item->setToolTip(QString::fromStdString(area.about) +
                         (area.live ? tr("\n(Read from the game right now; it can move.)") : QString()));
        item->setData(Qt::UserRole, QString::fromStdString(area.name));
        if (area.live)
        {
            QFont f = item->font();
            f.setItalic(true);
            item->setFont(f);
        }
        if (item->data(Qt::UserRole).toString() == picked)
            map_list->setCurrentItem(item);
    }
}

