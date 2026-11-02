#pragma once
#include <QTimer>
#include <QWidget>

#include "MemoryPane.hpp"

#include "corrupt/memory_map.h"
#include "corrupt/memory_watch.h"
#include "corrupt/paint_history.h"
#include "corrupt/recipe_json.h"

#include <array>
#include <string>
#include <vector>

class MemoryPane;
class MemoryView;
class PixelView;
class TileView;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QStackedWidget;

// What the viewer needs from the running game. The app passes the corruption
// session; tests pass a fake.
struct MemorySource
{
    virtual void watch(corrupt::Target target) = 0;
    virtual void stop() = 0;
    virtual void clearHits() = 0;
    virtual corrupt::MemorySnapshot snapshot() = 0;
    virtual std::vector<corrupt::CopyRun> findCopies(corrupt::Target of) = 0; // copies in WRAM
    virtual std::vector<corrupt::MapArea> memoryMap(corrupt::Target target) = 0; // fixed and live areas
    virtual std::vector<uint8_t> palette() = 0;                                  // CGRAM now, for the tile view
    virtual std::array<size_t, corrupt::kTargetCount> sizes() = 0;
    virtual corrupt::Regions regions() = 0;
    virtual std::string addRule(const corrupt::Rule &rule) = 0; // error or empty
    // Sets those bytes to `value` now and keeps them in the recipe as patches. Error or empty.
    virtual std::string paint(corrupt::Target target, const std::vector<uint32_t> &offsets, uint8_t value) = 0;
    // The same for bytes that each get their own value (an imported picture).
    virtual std::string setBytes(corrupt::Target target, const std::vector<std::pair<uint32_t, uint8_t>> &bytes) = 0;
    virtual std::string saveRegion(const std::string &name, const corrupt::AddressRange &range) = 0;
    virtual std::string renameRegion(const std::string &from, const std::string &to) = 0;
    virtual std::string deleteRegion(const std::string &name) = 0;
    virtual std::vector<std::string> regionUsers(const std::string &name) = 0;

    // Paint's draft and history (#66). paint() is a dab of the stroke being drawn; ROM strokes
    // are drafted until applyPaint() puts them in (with a ten-second crash watch).
    virtual void endStroke() {}
    virtual std::string applyPaint() { return {}; }
    virtual bool undoPaint() { return false; }
    virtual bool redoPaint() { return false; }
    virtual void goToPaint(size_t) {}
    virtual void discardDraft() {}
    virtual std::vector<corrupt::PaintHistory::Row> paintRows() { return {}; }
    virtual bool canUndoPaint() { return false; }
    virtual bool canRedoPaint() { return false; }
    virtual size_t draftSize() { return 0; }
    virtual bool paintWatching() { return false; } // a batch is being watched for a crash
    virtual std::string paintNews() { return {}; } // what became of the last Apply, until the next stroke
    virtual ~MemorySource() = default;
};

// The memory viewer window: one target as a hex grid, as pixels (one byte each,
// zoomable) or as decoded graphics tiles, with an activity or hits heatmap, go to an address, find the game's WRAM copies of its sprites and palette, turn a
// selected range into a rule, and name ranges for the game. It watches only while shown.
class MemoryViewer : public QWidget
{
  public:
    explicit MemoryViewer(MemorySource &source, QWidget *parent = nullptr);

    void refresh();                    // pull a fresh snapshot (the timer does this 10 times a second)
    // Shows that memory with those bytes selected and scrolled to, coloured by `overlay`
    // (its combo box value, like "functions"; empty keeps the colours).
    void showRange(corrupt::Target target, uint32_t first, uint32_t last, const QString &overlay = {});
    corrupt::Rule ruleFromSelection() const; // what New Rule from Selection starts from

    // The picture round trip (the buttons ask for a file, then call these). Export writes
    // the rectangle, the selected range or the whole memory, in the pixel view's row width.
    // Import puts an edited copy back where it came from (or onto the selection) as paint.
    void exportPng(const QString &path);
    void importPng(const QString &path);

    // Named regions. The buttons ask with dialogs and call these; each returns an error or empty.
    QString nameSelection(const QString &name, bool overwrite = false);
    QString renameRegion(const QString &from, const QString &to);
    QString deleteRegion(const QString &name);
    QString usersText(const QString &name); // "Used by: a, b" or that nothing uses it

  protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

  private:
    corrupt::Target target() const;
    MemoryPane *pane() const; // the view that's showing
    void modeChanged();
    void zoomChanged();
    void hovered(std::optional<uint32_t> offset);
    void tileHovered(std::optional<uint32_t> tile);
    QString mode() const; // "hex", "pixels" or "tiles"
    void targetChanged();
    void goTo();
    void selectionChanged();
    void findCopies();
    void newRule();
    void refreshRegions();
    void refreshMap();
    static MemoryPane::Overlay overlayNamed(const QString &name);
    void filterOverlays();
    void showLegend();
    void askNameSelection();
    void askRename();
    void askDelete();
    void showResult(const QString &error, const QString &done);
    void refreshPaint(); // the paint bar and the history (#66)
    void paintToolsShown();

    MemorySource &source;
    unsigned slow_ticks = 0;
    QTimer timer;
    bool open = false;
    std::optional<std::pair<uint32_t, uint32_t>> pending_select; // applied once the target's snapshot arrives

    QComboBox *target_combo;
    QComboBox *overlay_combo;
    QLineEdit *goto_edit;
    QComboBox *mode_combo;
    QWidget *view_options;
    QPushButton *zoom_out_button;
    QPushButton *zoom_in_button;
    QLabel *zoom_label;
    QSpinBox *row_width;
    QPushButton *paint_button;
    QPushButton *export_png_button;
    QPushButton *import_png_button;
    QSpinBox *paint_value;
    QSpinBox *brush_spin;
    std::array<int, corrupt::kTargetCount> row_widths{}; // chosen per memory, 0 = auto
    QStackedWidget *stack;
    MemoryView *view;
    PixelView *pixels;
    TileView *tiles;
    QComboBox *format_combo;
    QSpinBox *palette_spin;
    QSpinBox *per_row_spin;
    QLabel *hover_label;
    QLabel *legend_label = nullptr;
    QLabel *regions_empty = nullptr;
    QPushButton *clear_hits_button = nullptr;
    QWidget *copies_box = nullptr;
    QLabel *selection_label;
    QPushButton *new_rule_button;
    QListWidget *copies_list;
    QListWidget *regions_list;
    QListWidget *map_list;
    std::vector<corrupt::MapArea> shown_map;
    QPushButton *name_button;
    QPushButton *rename_button;
    QPushButton *delete_button;

    // Paint's draft and history (#66): shown while painting in the pixel view.
    QWidget *paint_bar;
    QPushButton *undo_button;
    QPushButton *redo_button;
    QLabel *draft_label;
    QPushButton *apply_button;
    QPushButton *discard_button;
    QWidget *history_box;
    QListWidget *history_list;
    QString shown_history; // what the list shows, to fill it only when it changes
};
