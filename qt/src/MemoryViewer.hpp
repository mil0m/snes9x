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
    virtual std::string saveRegion(const std::string &name, const corrupt::AddressRange &range) = 0;
    virtual std::string renameRegion(const std::string &from, const std::string &to) = 0;
    virtual std::string deleteRegion(const std::string &name) = 0;
    virtual std::vector<std::string> regionUsers(const std::string &name) = 0;
    virtual ~MemorySource() = default;
};

// The memory viewer window: one target as a hex grid with an activity or hits heatmap,
// go to an address, find the game's WRAM copies of its sprites and palette, turn a
// selected range into a rule, and name ranges for the game. It watches only while shown.
class MemoryViewer : public QWidget
{
  public:
    explicit MemoryViewer(MemorySource &source, QWidget *parent = nullptr);

    void refresh();                    // pull a fresh snapshot (the timer does this 10 times a second)
    corrupt::Rule ruleFromSelection() const; // what New Rule from Selection starts from

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
    void targetChanged();
    void goTo();
    void selectionChanged();
    void findCopies();
    void newRule();
    void refreshRegions();
    void askNameSelection();
    void askRename();
    void askDelete();
    void showResult(const QString &error, const QString &done);

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
    QListWidget *regions_list;
    QPushButton *name_button;
    QPushButton *rename_button;
    QPushButton *delete_button;
};
