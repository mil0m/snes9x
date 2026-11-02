#include "SessionControl.hpp"
#include "CorruptionSession.hpp"
#include "EmuApplication.hpp"
#include "EmuConfig.hpp"
#include "RemoteControl.hpp"
#include "corrupt/remote_api.h"

using namespace corrupt;

namespace
{
CorruptionSession &session()
{
    return CorruptionSession::get();
}
} // namespace

SessionControl::SessionControl(EmuApplication &app, RemoteControl &remote) : app(app), remote_control(remote)
{
    if (app.config->corruption_remote_token.empty())
        app.config->corruption_remote_token = remote::new_token();
}

PanelStatus SessionControl::status()
{
    auto s = session().status();
    s.paused = s.game_loaded && app.isPaused();
    return s;
}
void SessionControl::setActive(bool on) { session().setActive(on); }
void SessionControl::backToNormal() { session().backToNormal(); }
void SessionControl::normalHere() { session().backToNormal(true); }
uint64_t SessionControl::reroll() { return session().reroll(); }
void SessionControl::setSeed(uint64_t seed) { session().setSeed(seed); }
void SessionControl::pressButton(int key) { session().pressHotkey(key); }
void SessionControl::setRuleEnabled(size_t index, bool on) { session().setRuleEnabled(index, on); }
void SessionControl::setRuleIntensity(size_t index, double intensity) { session().setRuleIntensity(index, intensity); }
void SessionControl::setIntensity(double intensity) { session().setIntensity(intensity); }
void SessionControl::markCrash() { session().markCrash(); }

std::vector<RecipeEntry> SessionControl::recipes()
{
    auto s = session().status();
    return s.game_loaded ? session().library().list_recipes(s.game) : std::vector<RecipeEntry>{};
}
std::vector<std::string> SessionControl::loadRecipe(const Recipe &recipe, const std::filesystem::path &path)
{
    return session().loadRecipe(recipe, path);
}
void SessionControl::newRecipe(const std::string &name) { session().newRecipe(name); }
SaveResult SessionControl::save() { return session().save(); }
SaveResult SessionControl::saveAs(const std::string &name, const std::string &notes)
{
    return session().saveRecipeAs(name, notes);
}
std::string SessionControl::shareRecipe(const std::filesystem::path &path) { return session().shareRecipe(path); }
std::string SessionControl::importRecipe(const std::filesystem::path &path, std::string &name)
{
    return session().importRecipe(path, name);
}
void SessionControl::setSavedMoments(bool on)
{
    app.config->corruption_saved_moments = on;
    session().setSavedMoments(on);
}
Regions SessionControl::regions() { return session().regions(); }
std::string SessionControl::addRule(const Rule &rule) { return session().addRule(rule); }
std::string SessionControl::replaceRule(size_t index, const Rule &rule) { return session().replaceRule(index, rule); }
std::string SessionControl::removeRule(size_t index) { return session().removeRule(index); }
std::string SessionControl::removeKept(const KeptItem &item) { return session().removeKept(item); }

void SessionControl::setDetectionEnabled(bool on) { session().setDetectionEnabled(on); }
void SessionControl::setTestFirstEnabled(bool on) { session().setTestFirstEnabled(on); }
void SessionControl::setInstantEnabled(bool on) { session().setInstantEnabled(on); }
void SessionControl::setPolicyOverride(std::optional<PolicyKind> policy) { session().setPolicyOverride(policy); }
std::filesystem::path SessionControl::backupDir() { return session().backupDir(); }
std::vector<BackupInfo> SessionControl::backups() { return session().backups(); }
std::string SessionControl::restoreBackup(const std::filesystem::path &path)
{
    auto err = session().restoreBackup(path);
    if (err.empty())
        app.reset();
    return err;
}

void SessionControl::restoreCleanRom() { session().restoreCleanRom(); }
void SessionControl::clearPatches() { session().clearPatches(); }
std::string SessionControl::exportRom(const std::filesystem::path &path) { return session().exportRom(path); }

std::filesystem::path SessionControl::libraryRoot() { return app.config->corruption_library_root; }
void SessionControl::setLibraryRoot(const std::filesystem::path &root)
{
    app.config->corruption_library_root = root.string();
    session().setLibraryRoot(root);
}
std::filesystem::path SessionControl::gameFolder()
{
    auto s = session().status();
    return s.game_loaded ? session().library().game_dir(s.game) : std::filesystem::path{};
}
std::vector<Library::Player> SessionControl::players()
{
    auto s = session().status();
    return s.game_loaded ? session().library().players(s.game) : std::vector<Library::Player>{};
}
std::vector<std::string> SessionControl::conflicts()
{
    auto s = session().status();
    return s.game_loaded ? session().library().conflict_copies(s.game) : std::vector<std::string>{};
}
std::string SessionControl::me() { return CorruptionSession::playerName(); }

RemoteState SessionControl::remote()
{
    RemoteState r;
    auto &srv = remote_control.server();
    r.enabled = srv.running();
    r.address = app.config->corruption_remote_address;
    if (!remote_error.empty())
        r.status = remote_error;
    else if (!srv.running())
        r.status = "Off. Nobody can control this from a browser.";
    else
    {
        auto who = srv.clients();
        r.status = "Listening on this computer, port " + std::to_string(srv.port()) + ". " +
                   (who.isEmpty() ? std::string("Nobody connected.")
                                  : "Connected: " + who.join(", ").toStdString() + ".");
    }
    return r;
}

std::string SessionControl::setRemote(bool on)
{
    remote_error.clear();
    app.config->corruption_remote_enabled = on;
    remote_control.stop();
    if (on && !remote_control.start(quint16(app.config->corruption_remote_port), app.config->corruption_remote_token))
    {
        app.config->corruption_remote_enabled = false;
        remote_error = "Couldn't listen on port " + std::to_string(app.config->corruption_remote_port) +
                       ": something else uses it. Change RemotePort in the settings file.";
        return remote_error;
    }
    return {};
}

void SessionControl::setRemoteAddress(const std::string &address) { app.config->corruption_remote_address = address; }

std::string SessionControl::remoteLink()
{
    std::string base = app.config->corruption_remote_address;
    while (!base.empty() && base.back() == '/')
        base.pop_back();
    if (base.empty())
        base = "http://127.0.0.1:" + std::to_string(app.config->corruption_remote_port);
    return base + "/#" + app.config->corruption_remote_token;
}

void SessionControl::newRemoteKey()
{
    app.config->corruption_remote_token = remote::new_token();
    if (remote_control.server().running())
        setRemote(true); // restarts with the new key, dropping everyone
}
