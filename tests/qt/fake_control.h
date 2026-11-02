#pragma once
// A stand-in for the running session, for the cockpit and Workshop tests (#63): a status to
// show, and every call written down.
#include "CorruptionControl.hpp"
#include "MemoryViewer.hpp"
#include "SurvivorsPanel.hpp"

#include <string>
#include <vector>

struct FakeControl : CorruptionControl
{
    PanelStatus s;
    std::vector<std::string> calls;
    std::vector<corrupt::RecipeEntry> entries;
    std::vector<corrupt::Library::Player> people;
    std::vector<std::string> conflict_list;
    std::vector<corrupt::BackupInfo> backup_list;
    RemoteState remote_state;
    std::filesystem::path root = "/tmp/library";
    corrupt::Recipe last_loaded;

    FakeControl()
    {
        s.game_loaded = true;
        s.game = { "POCKY & ROCKY", "0123456789abcdef0123", "" };
        s.sizes[size_t(corrupt::Target::WRAM)] = 0x20000;
        s.sizes[size_t(corrupt::Target::ROM)] = 0x100000;
        s.rom_size = 0x100000;
        remote_state.status = "Off. Nobody can control this from a browser.";
    }
    // A recipe with rules, running.
    void running(size_t rules = 3)
    {
        s.has_recipe = true;
        s.engine.recipe_name = "Gravity mess";
        s.engine.active = true;
        s.engine.seed = 1007;
        s.engine.frame = 600;
        s.recipe_path = "/tmp/library/recipes/gravity-mess.json";
        s.rules.clear();
        for (size_t i = 0; i < rules; i++)
        {
            corrupt::Rule r;
            r.name = "rule " + std::to_string(i + 1);
            r.range = { corrupt::Target::WRAM, 0, 0x1FFF };
            s.rules.push_back(r);
        }
        s.rule_crashes.assign(rules, 0);
        s.recipe_generation++;
        s.rules_generation++;
    }
    void call(const std::string &what) { calls.push_back(what); }

    PanelStatus status() override { return s; }
    void setActive(bool on) override
    {
        s.engine.active = on;
        call(on ? "on" : "off");
    }
    void backToNormal() override { call("back to normal"); }
    void normalHere() override { call("normal here"); }
    uint64_t reroll() override
    {
        call("reroll");
        return ++s.engine.seed;
    }
    void setSeed(uint64_t seed) override { call("seed " + std::to_string(seed)); }
    void pressButton(int key) override { call("button " + std::to_string(key)); }
    void setRuleEnabled(size_t i, bool on) override { call("rule " + std::to_string(i) + (on ? " on" : " off")); }
    void setRuleIntensity(size_t i, double v) override
    {
        call("strength " + std::to_string(i) + " " + std::to_string(int(v * 100)));
    }
    void setIntensity(double v) override { call("chaos " + std::to_string(int(v * 100))); }
    void markCrash() override { call("mark crash"); }

    std::vector<corrupt::RecipeEntry> recipes() override { return entries; }
    std::vector<std::string> loadRecipe(const corrupt::Recipe &recipe, const std::filesystem::path &path) override
    {
        last_loaded = recipe;
        call("load " + recipe.name + (path.empty() ? "" : " from " + path.filename().string()));
        return {};
    }
    void newRecipe(const std::string &name) override { call("new " + name); }
    corrupt::SaveResult save() override
    {
        call("save");
        return { s.recipe_path, {} };
    }
    corrupt::SaveResult saveAs(const std::string &name, const std::string &) override
    {
        call("save as " + name);
        return { "/tmp/library/recipes/" + name + ".json", {} };
    }
    std::string shareRecipe(const std::filesystem::path &path) override
    {
        call("share " + path.filename().string());
        return {};
    }
    std::string importRecipe(const std::filesystem::path &path, std::string &name) override
    {
        call("import " + path.filename().string());
        name = "Imported";
        return {};
    }
    void setSavedMoments(bool on) override { call(on ? "moments on" : "moments off"); }
    corrupt::Regions regions() override { return {}; }
    std::string addRule(const corrupt::Rule &rule) override
    {
        call("add rule " + rule.name);
        return {};
    }
    std::string replaceRule(size_t i, const corrupt::Rule &rule) override
    {
        call("replace rule " + std::to_string(i) + " " + rule.name);
        return {};
    }
    std::string removeRule(size_t i) override
    {
        call("remove rule " + std::to_string(i));
        return {};
    }
    std::string removeKept(const corrupt::KeptItem &item) override
    {
        call("remove kept " + item.what);
        return {};
    }

    void setDetectionEnabled(bool on) override { call(on ? "detect on" : "detect off"); }
    void setTestFirstEnabled(bool on) override { call(on ? "test first on" : "test first off"); }
    void setInstantEnabled(bool on) override { call(on ? "instant on" : "instant off"); }
    void setPolicyOverride(std::optional<corrupt::PolicyKind> p) override
    {
        call(p ? std::string("policy ") + corrupt::policy_name(*p) : std::string("policy recipe"));
    }
    std::filesystem::path backupDir() override { return "/tmp/backups"; }
    std::vector<corrupt::BackupInfo> backups() override { return backup_list; }
    std::string restoreBackup(const std::filesystem::path &path) override
    {
        call("restore " + path.filename().string());
        return {};
    }

    void restoreCleanRom() override { call("restore clean rom"); }
    void clearPatches() override { call("clear paint"); }
    std::string exportRom(const std::filesystem::path &path) override
    {
        call("export " + path.filename().string());
        return {};
    }

    std::filesystem::path libraryRoot() override { return root; }
    void setLibraryRoot(const std::filesystem::path &r) override
    {
        root = r;
        call("library " + r.string());
    }
    std::filesystem::path gameFolder() override { return s.game_loaded ? root / "POCKY" : std::filesystem::path{}; }
    std::vector<corrupt::Library::Player> players() override { return people; }
    std::vector<std::string> conflicts() override { return conflict_list; }
    std::string me() override { return "mikael"; }

    RemoteState remote() override { return remote_state; }
    std::string setRemote(bool on) override
    {
        remote_state.enabled = on;
        call(on ? "remote on" : "remote off");
        return {};
    }
    void setRemoteAddress(const std::string &a) override
    {
        remote_state.address = a;
        call("remote address " + a);
    }
    std::string remoteLink() override { return "http://127.0.0.1:8095/#key"; }
    void newRemoteKey() override { call("new key"); }
};

// The memory viewer and survivors list with nothing to show: enough for the Workshop.
struct EmptyMemory : MemorySource
{
    void watch(corrupt::Target) override {}
    void stop() override {}
    void clearHits() override {}
    corrupt::MemorySnapshot snapshot() override { return {}; }
    std::vector<corrupt::CopyRun> findCopies(corrupt::Target) override { return {}; }
    std::vector<corrupt::MapArea> memoryMap(corrupt::Target) override { return {}; }
    std::vector<uint8_t> palette() override { return {}; }
    std::array<size_t, corrupt::kTargetCount> sizes() override { return {}; }
    corrupt::Regions regions() override { return {}; }
    std::string addRule(const corrupt::Rule &) override { return {}; }
    std::string paint(corrupt::Target, const std::vector<uint32_t> &, uint8_t) override { return {}; }
    std::string setBytes(corrupt::Target, const std::vector<std::pair<uint32_t, uint8_t>> &) override { return {}; }
    std::string saveRegion(const std::string &, const corrupt::AddressRange &) override { return {}; }
    std::string renameRegion(const std::string &, const std::string &) override { return {}; }
    std::string deleteRegion(const std::string &) override { return {}; }
    std::vector<std::string> regionUsers(const std::string &) override { return {}; }
};

struct EmptySurvivors : SurvivorSource
{
    std::vector<corrupt::Survivor> survivors() override { return {}; }
    std::vector<corrupt::Pin> pins() override { return {}; }
    std::string pin(const std::vector<uint32_t> &, const std::string &) override { return {}; }
    void unpin(const std::vector<uint32_t> &) override {}
};
