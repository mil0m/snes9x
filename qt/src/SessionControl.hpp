#pragma once
#include "CorruptionControl.hpp"

class EmuApplication;
class RemoteControl;

// The panel's and the Workshop's way to the running session (#63): CorruptionSession, the
// app's settings (library folder, saved moments, remote control) and the remote server.
class SessionControl : public CorruptionControl
{
  public:
    SessionControl(EmuApplication &app, RemoteControl &remote);

    PanelStatus status() override;
    void setActive(bool on) override;
    void backToNormal() override;
    void normalHere() override;
    uint64_t reroll() override;
    void setSeed(uint64_t seed) override;
    void pressButton(int key) override;
    void setRuleEnabled(size_t index, bool on) override;
    void setRuleIntensity(size_t index, double intensity) override;
    void setIntensity(double intensity) override;
    void markCrash() override;

    std::vector<corrupt::RecipeEntry> recipes() override;
    std::vector<std::string> loadRecipe(const corrupt::Recipe &recipe, const std::filesystem::path &path) override;
    void newRecipe(const std::string &name) override;
    corrupt::SaveResult save() override;
    corrupt::SaveResult saveAs(const std::string &name, const std::string &notes) override;
    std::string shareRecipe(const std::filesystem::path &path) override;
    std::string importRecipe(const std::filesystem::path &path, std::string &name) override;
    void setSavedMoments(bool on) override;
    corrupt::Regions regions() override;
    std::string addRule(const corrupt::Rule &rule) override;
    std::string replaceRule(size_t index, const corrupt::Rule &rule) override;
    std::string removeRule(size_t index) override;
    std::string removeKept(const corrupt::KeptItem &item) override;

    void setDetectionEnabled(bool on) override;
    void setTestFirstEnabled(bool on) override;
    void setInstantEnabled(bool on) override;
    void setPolicyOverride(std::optional<corrupt::PolicyKind> policy) override;
    std::filesystem::path backupDir() override;
    std::vector<corrupt::BackupInfo> backups() override;
    std::string restoreBackup(const std::filesystem::path &path) override;

    void restoreCleanRom() override;
    void clearPatches() override;
    std::string exportRom(const std::filesystem::path &path) override;

    std::filesystem::path libraryRoot() override;
    void setLibraryRoot(const std::filesystem::path &root) override;
    std::filesystem::path gameFolder() override;
    std::vector<corrupt::Library::Player> players() override;
    std::vector<std::string> conflicts() override;
    std::string me() override;

    void keepMoment() override;
    std::vector<corrupt::MomentInfo> moments() override;
    std::string playMoment(const std::filesystem::path &path) override;
    void stopMoment() override;
    std::string renameMoment(const std::filesystem::path &path, const std::string &name) override;
    std::string deleteMoment(const std::filesystem::path &path) override;
    std::vector<corrupt::Library::ExplorationEntry> explorations() override;
    corrupt::SaveResult keepExplored(const std::filesystem::path &dir, const corrupt::Candidate &c,
                                     const std::string &moment) override;
    std::string deleteExploration(const std::filesystem::path &dir) override;

    RemoteState remote() override;
    std::string setRemote(bool on) override;
    void setRemoteAddress(const std::string &address) override;
    std::string remoteLink() override;
    void newRemoteKey() override;

  private:
    EmuApplication &app;
    RemoteControl &remote_control;
    std::string remote_error;
};
