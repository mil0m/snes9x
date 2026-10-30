#pragma once
#include <QDockWidget>
#include <QTimer>

#include "corrupt/library.h"

class EmuApplication;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTableWidget;

// The corruption panel: pick a recipe for the loaded game, toggle it, re-roll,
// save it to the library, and restore save backups.
class CorruptionDock : public QDockWidget
{
    Q_OBJECT

  public:
    CorruptionDock(EmuApplication &app, QWidget *parent);

    void refreshRecipes();
    void refreshBackups();

  private:
    void refreshStatus();
    void rebuildRules();
    void chooseLibrary();
    void loadSelectedRecipe();
    void saveAs();
    void addExample();
    void restoreSelectedBackup();
    void showMessage(const QString &text, bool error = false);

    EmuApplication &app;
    QTimer timer;
    std::vector<corrupt::RecipeEntry> entries;
    uint64_t shown_generation = UINT64_MAX;
    std::string shown_game;
    std::string shown_message;
    int ticks = 0;

    QLabel *game_label;
    QLineEdit *library_edit;
    QComboBox *recipe_combo;
    QComboBox *example_combo;
    QCheckBox *active_check;
    QLineEdit *seed_edit;
    QTableWidget *rules_table;
    QLabel *status_label;
    QListWidget *backup_list;
    QLabel *message_label;
};
