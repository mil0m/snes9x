#include "MemoryViewer.hpp"
#include "AreaImage.hpp"
#include "MemoryView.hpp"
#include "PixelView.hpp"
#include "TileView.hpp"
#include "RuleEditor.hpp"
#include "corrupt/rule_text.h"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFontDatabase>
#include <QInputDialog>
#include <QMessageBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
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

QString hex4(uint32_t v)
{
    return QString("0x%1").arg(v, 4, 16, QChar('0')).toUpper().replace("0X", "0x");
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
    zoom_out_button = new QPushButton(QStringLiteral("−"), this);
    zoom_out_button->setObjectName("zoom_out");
    zoom_out_button->setToolTip(tr("Zoom out (Ctrl+wheel)"));
    zoom_in_button = new QPushButton(QStringLiteral("+"), this);
    zoom_in_button->setObjectName("zoom_in");
    zoom_in_button->setToolTip(tr("Zoom in (Ctrl+wheel)"));
    zoom_label = new QLabel(this);
    zoom_label->setObjectName("zoom");
    row_width = new QSpinBox(this);
    row_width->setObjectName("row_width");
    row_width->setRange(0, PixelView::kMaxRowWidth);
    row_width->setPrefix(tr("Row "));
    row_width->setSuffix(tr(" bytes"));
    row_width->setSpecialValueText(tr("Row: Auto"));
    row_width->setToolTip(tr("Bytes per row of dots. Step it one at a time (arrows or wheel) until the picture "
                             "lines up into columns: then a row of dots is a row of the game's data. 0 is Auto."));
    format_combo = new QComboBox(this);
    format_combo->setObjectName("tile_format");
    for (auto f : { TileFormat::Bpp2, TileFormat::Bpp4, TileFormat::Bpp8, TileFormat::Mode7 })
        format_combo->addItem(tile_format_name(f), QString(tile_format_id(f)));
    format_combo->setCurrentIndex(1); // 4 bpp: most backgrounds and every sprite
    format_combo->setToolTip(tr("How the tiles are stored. Clicking a tile area in the Map picks this for you."));
    palette_spin = new QSpinBox(this);
    palette_spin->setObjectName("palette");
    palette_spin->setPrefix(tr("Palette "));
    palette_spin->setSpecialValueText(tr("Palette: Grey"));
    palette_spin->setToolTip(tr("Which row of the game's palette colours the tiles. Sprites use 8-15 at 4 bpp. "
                                "Grey shows the raw colour numbers."));
    per_row_spin = new QSpinBox(this);
    per_row_spin->setObjectName("tiles_per_row");
    per_row_spin->setRange(1, TileView::kMaxTilesPerRow);
    per_row_spin->setValue(16);
    per_row_spin->setSuffix(tr(" per row"));
    per_row_spin->setToolTip(tr("Tiles per row. Big sprites and fonts often line up at 16 or 32."));
    for (auto b : { zoom_out_button, zoom_in_button })
        b->setFixedWidth(b->fontMetrics().horizontalAdvance("+") + 24);
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
    overlay_combo->addItem(tr("Plain"), QString("none"));
    goto_edit = new QLineEdit(this);
    goto_edit->setObjectName("goto");
    goto_edit->setPlaceholderText(tr("Go to 0x…"));
    goto_edit->setMaximumWidth(110);
    auto clear = new QPushButton(tr("Clear Hits"), this);
    clear->setObjectName("clear_hits");
    top->addWidget(mode_combo);
    top->addWidget(target_combo);
    top->addWidget(overlay_combo, 1);
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
    paint_button = new QPushButton(tr("Paint"), view_options);
    paint_button->setObjectName("paint");
    paint_button->setCheckable(true);
    paint_button->setToolTip(tr("Drag to paint bytes with the value, into the game and the recipe (as patches). "
                                "Alt-click picks the value under the mouse."));
    paint_value = new QSpinBox(view_options);
    paint_value->setObjectName("paint_value");
    paint_value->setRange(0, 255);
    paint_value->setDisplayIntegerBase(16);
    paint_value->setPrefix("0x");
    paint_value->setToolTip(tr("The value to paint"));
    brush_spin = new QSpinBox(view_options);
    brush_spin->setObjectName("brush");
    brush_spin->setRange(1, 64);
    brush_spin->setPrefix(tr("Brush "));
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

    auto middle = new QHBoxLayout;
    map_list = new QListWidget(this);
    map_list->setObjectName("map");
    map_list->setMinimumWidth(190);
    map_list->setMaximumWidth(260);
    map_list->setToolTip(tr("What lives where. Click an area to select it."));
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
    middle->addWidget(map_list);
    middle->addWidget(stack, 1);
    layout->addLayout(middle, 1);

    auto bottom = new QHBoxLayout;
    selection_label = new QLabel(tr("Drag across bytes to select a range"), this);
    selection_label->setObjectName("selection");
    hover_label = new QLabel(this);
    hover_label->setObjectName("hover");
    hover_label->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    new_rule_button = new QPushButton(tr("New Rule from Selection..."), this);
    new_rule_button->setObjectName("new_rule");
    new_rule_button->setEnabled(false);
    bottom->addWidget(selection_label, 1);
    bottom->addWidget(hover_label);
    bottom->addWidget(new_rule_button);
    layout->addLayout(bottom);

    auto copies_box = new QGroupBox(tr("Sprite and palette copies"), this);
    auto copies_layout = new QVBoxLayout(copies_box);
    auto find = new QPushButton(tr("Find Copies in WRAM"), copies_box);
    find->setObjectName("find_copies");
    find->setToolTip(tr("Most games rebuild sprites and colours from a copy in WRAM every frame, so corrupting "
                        "the copy sticks where corrupting oam or cgram only flashes."));
    copies_list = new QListWidget(copies_box);
    copies_list->setObjectName("copies");
    copies_list->setMaximumHeight(110);
    copies_layout->addWidget(find);
    copies_layout->addWidget(copies_list);

    auto regions_box = new QGroupBox(tr("Named areas"), this);
    auto regions_layout = new QVBoxLayout(regions_box);
    regions_list = new QListWidget(regions_box);
    regions_list->setObjectName("regions");
    regions_list->setMaximumHeight(110);
    auto region_buttons = new QHBoxLayout;
    name_button = new QPushButton(tr("Name Selection..."), regions_box);
    name_button->setObjectName("name_selection");
    name_button->setEnabled(false);
    rename_button = new QPushButton(tr("Rename..."), regions_box);
    delete_button = new QPushButton(tr("Delete..."), regions_box);
    region_buttons->addWidget(name_button);
    region_buttons->addStretch(1);
    region_buttons->addWidget(rename_button);
    region_buttons->addWidget(delete_button);
    regions_layout->addWidget(regions_list);
    regions_layout->addLayout(region_buttons);

    auto lists = new QHBoxLayout;
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
        auto o = overlay_combo->currentData().toString();
        auto overlay = o == "hits"     ? MemoryPane::Overlay::Hits
                       : o == "danger"  ? MemoryPane::Overlay::Danger
                       : o == "crashes" ? MemoryPane::Overlay::Crashes
                       : o == "reads"   ? MemoryPane::Overlay::Reads
                       : o == "writes"  ? MemoryPane::Overlay::Writes
                       : o == "code"    ? MemoryPane::Overlay::Code
                       : o == "data"    ? MemoryPane::Overlay::Data
                       : o == "dma"     ? MemoryPane::Overlay::Dma
                       : o == "none"   ? MemoryPane::Overlay::None
                                       : MemoryPane::Overlay::Activity;
        view->setOverlay(overlay);
        pixels->setOverlay(overlay);
        tiles->setOverlay(overlay);
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
    connect(paint_button, &QPushButton::toggled, this, [this](bool on) { pixels->setPaintMode(on); });
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
    for (QWidget *w : { static_cast<QWidget *>(paint_button), static_cast<QWidget *>(paint_value),
                        static_cast<QWidget *>(brush_spin), static_cast<QWidget *>(export_png_button),
                        static_cast<QWidget *>(import_png_button) })
        w->setVisible(m == "pixels");
    if (m != "pixels" && paint_button->isChecked())
        paint_button->click(); // painting only works in the pixel view
    for (QWidget *w : { static_cast<QWidget *>(format_combo), static_cast<QWidget *>(palette_spin),
                        static_cast<QWidget *>(per_row_spin) })
        w->setVisible(m == "tiles");
    hover_label->clear();
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

void MemoryViewer::hovered(std::optional<uint32_t> offset)
{
    auto &snap = pixels->snapshot();
    if (!offset || *offset >= snap.bytes.size())
        return hover_label->clear();
    auto text = QString("%1 = 0x%2").arg(hex4(*offset)).arg(snap.bytes[*offset], 2, 16, QChar('0')).toUpper().replace("0X", "0x");
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
            QString where = in->address ? QString("$%1:%2")
                                              .arg(*in->address >> 16, 2, 16, QChar('0'))
                                              .arg(*in->address & 0xFFFF, 4, 16, QChar('0'))
                                              .toUpper()
                                        : hex4(in->start);
            text += tr(" · %1  %2 %3 (%4)").arg(where, bytes, QString::fromStdString(in->text), class_name(in->cls));
            if (in->both_widths)
                text += tr(", runs 8- and 16-bit");
        }
        if (f & codeflag::Data)
            text += tr(" · read as data");
        if (f & codeflag::Dma)
            text += tr(" · copied by DMA");
    }
    // What the crash map knows about the byte's block, whatever the overlay.
    size_t block = *offset / CrashMap::kBlock;
    if (block < snap.crash_blocks.size())
    {
        auto c = snap.crash_blocks[block];
        if (c.crashes || c.survivals)
        {
            text += tr(" · block %1: %2 crashes, %3 calm, danger %4")
                        .arg(hex4(uint32_t(block * CrashMap::kBlock)))
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
                             .arg(hex4(*tile * tiles->tileSpan())));
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
    row_width->setValue(row_widths[size_t(target())]); // each memory keeps its own
    if (open)
        source.watch(target());
    refresh();
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
    if (pending_select && !snap.bytes.empty())
    {
        pane()->select(pending_select->first, pending_select->second);
        pending_select.reset();
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
        selection_label->setText(tr("Drag across bytes to select a range"));
        return;
    }
    if (auto rect = mode() == "pixels" ? pixels->rectSelection() : std::nullopt)
    {
        selection_label->setText(tr("%1 %2x%3 at %4 in rows of %5 (%6 bytes)")
                                     .arg(target_name(pane()->snapshot().target))
                                     .arg(rect->width)
                                     .arg(rect->height)
                                     .arg(hex4(rect->start))
                                     .arg(rect->row)
                                     .arg(rect->width * rect->height));
        return;
    }
    selection_label->setText(QString("%1 %2..%3 (%4 bytes)")
                                 .arg(target_name(pane()->snapshot().target), hex4(sel->first), hex4(sel->second))
                                 .arg(sel->second - sel->first + 1));
}

Rule MemoryViewer::ruleFromSelection() const
{
    Rule r = default_rule();
    if (auto rect = mode() == "pixels" ? pixels->rectSelection() : std::nullopt)
    {
        set_rect(r, pane()->snapshot().target, rect->start, Rect{ rect->row, rect->width, rect->height });
        r.name = std::string(target_name(r.range.target)) + " " + std::to_string(rect->width) + "x" +
                 std::to_string(rect->height) + " at " + hex4(rect->start).toStdString();
    }
    else if (auto sel = pane()->selection())
    {
        r.range = { pane()->snapshot().target, sel->first, sel->second };
        r.name = std::string(target_name(r.range.target)) + " " + hex4(sel->first).toStdString();
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
    selection_label->setText(err.empty() ? tr("Imported: %n byte(s) changed, kept as paint", "", int(bytes->size()))
                                         : tr("Couldn't import: %1").arg(QString::fromStdString(err)));
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
    copies_list->clear();
    for (auto of : { Target::OAM, Target::CGRAM })
        for (auto &run : source.findCopies(of))
        {
            auto text = QString("%1 → wram %2 (%3 bytes)").arg(target_name(of), hex4(run.haystack_offset)).arg(run.length);
            auto item = new QListWidgetItem(text, copies_list);
            item->setToolTip(QString("%1 %2..%3 is copied at wram %4..%5")
                                 .arg(target_name(of), hex4(run.needle_offset), hex4(run.needle_offset + run.length - 1),
                                      hex4(run.haystack_offset), hex4(run.haystack_offset + run.length - 1)));
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
                                                 hex4(range.first), hex4(range.last)),
                                        regions_list);
        item->setData(Qt::UserRole, QString::fromStdString(name));
        if (item->data(Qt::UserRole).toString() == current)
            regions_list->setCurrentItem(item);
    }
    rename_button->setEnabled(regions_list->count() > 0);
    delete_button->setEnabled(regions_list->count() > 0);
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
        auto item = new QListWidgetItem(QString("%1\n%2..%3").arg(QString::fromStdString(area.name),
                                                                 hex4(area.range.first), hex4(area.range.last)),
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

