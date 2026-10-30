#pragma once
#include <QTimer>
#include <QWidget>

#include "corrupt/memory_watch.h"
#include "corrupt/recipe_json.h"

#include <array>
#include <string>
#include <vector>

class MemoryView;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

// What the viewer needs from the running game. The app passes the corruption
// session; tests pass a fake.
struct MemorySource
{
    virtual void watch(corrupt::Target target) = 0;
    virtual void stop() = 0;
    virtual void clearHits() = 0;
    virtual corrupt::MemorySnapshot snapshot() = 0;
    virtual std::vector<corrupt::CopyRun> findCopies(corrupt::Target of) = 0; // copies in WRAM
    virtual std::array<size_t, corrupt::kTargetCount> sizes() = 0;
    virtual corrupt::Regions regions() = 0;
    virtual std::string addRule(const corrupt::Rule &rule) = 0; // error or empty
    virtual ~MemorySource() = default;
};

// The memory viewer window: one target as a hex grid with an activity or hits heatmap,
// go to an address, find the game's WRAM copies of its sprites and palette, and turn a
// selected range into a rule. It watches only while it's shown.
class MemoryViewer : public QWidget
{
  public:
    explicit MemoryViewer(MemorySource &source, QWidget *parent = nullptr);

    void refresh();                    // pull a fresh snapshot (the timer does this 10 times a second)
    corrupt::Rule ruleFromSelection() const; // what New Rule from Selection starts from

  protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

  private:
    corrupt::Target target() const;
    void targetChanged();
    void goTo();
    void selectionChanged();
    void findCopies();
    void newRule();

    MemorySource &source;
    QTimer timer;
    bool open = false;
    std::optional<std::pair<uint32_t, uint32_t>> pending_select; // applied once the target's snapshot arrives

    QComboBox *target_combo;
    QComboBox *overlay_combo;
    QLineEdit *goto_edit;
    MemoryView *view;
    QLabel *selection_label;
    QPushButton *new_rule_button;
    QListWidget *copies_list;
};
