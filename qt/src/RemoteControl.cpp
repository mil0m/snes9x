#include "RemoteControl.hpp"
#include "CorruptionSession.hpp"
#include "Starters.hpp"
#include "corrupt/rule_text.h"

using namespace corrupt;

namespace
{
CorruptionSession &session()
{
    return CorruptionSession::get();
}
} // namespace

std::string SessionRemote::set_active(bool on)
{
    auto s = session().status();
    if (on && !s.game_loaded)
        return "No game loaded";
    if (on && !s.has_recipe)
        return "No recipe loaded: choose one in the panel (or paint something) first";
    session().setActive(on);
    return {};
}

void SessionRemote::normal_here()
{
    session().backToNormal(true);
}

void SessionRemote::back_to_normal()
{
    session().backToNormal();
}

void SessionRemote::reroll()
{
    session().reroll();
}

void SessionRemote::mark_crash()
{
    session().markCrash();
}

void SessionRemote::set_chaos(double chaos)
{
    session().setIntensity(chaos);
}

void SessionRemote::set_rule_enabled(size_t index, bool on)
{
    session().setRuleEnabled(index, on);
}

void SessionRemote::set_rule_strength(size_t index, double strength)
{
    session().setRuleIntensity(index, strength);
}

void SessionRemote::press_hotkey(int n)
{
    session().pressHotkey(n);
}

std::string SessionRemote::paint(Target t, const std::vector<uint32_t> &offsets, uint8_t value)
{
    std::vector<std::pair<uint32_t, uint8_t>> bytes;
    for (auto o : offsets)
        bytes.push_back({ o, value });
    return session().setBytesNow(t, bytes); // ROM paint goes in at once, with the crash watch (#66)
}

size_t SessionRemote::starter_count()
{
    return bundled_starters().size();
}

std::string SessionRemote::starter_name(size_t index)
{
    return bundled_starters().at(index).recipe.name;
}

std::string SessionRemote::load_starter(size_t index)
{
    auto s = session().status();
    if (!s.game_loaded)
        return "No game loaded";
    Recipe recipe = bundled_starters().at(index).recipe;
    recipe.game = s.game;
    auto problems = session().loadRecipe(recipe);
    if (problems.empty())
        return {};
    std::string text = "This recipe doesn't fit this game: " + problems[0];
    return text;
}

size_t SessionRemote::rule_count()
{
    return session().status().rules.size();
}

size_t SessionRemote::memory_size(Target t)
{
    auto s = session().status();
    return s.game_loaded ? s.sizes[size_t(t)] : 0;
}

remote::StatusView SessionRemote::status()
{
    auto s = session().status();
    remote::StatusView v;
    v.game_loaded = s.game_loaded;
    v.game = s.game.title;
    v.recipe = s.has_recipe ? s.engine.recipe_name : "";
    v.active = s.engine.active;
    v.starting = s.starting;
    v.has_recipe = s.has_recipe;
    v.chaos = s.intensity;
    v.seed = s.engine.seed;
    v.crashes = s.crashes;
    v.message = s.message;
    for (auto &r : s.rules)
    {
        remote::RuleView rv;
        rv.name = r.name;
        rv.what = describe_rule(r);
        rv.enabled = r.enabled;
        rv.strength = r.intensity;
        rv.hotkey = r.when.type == TriggerType::Hotkey ? int(r.when.hotkey) : 0;
        v.rules.push_back(std::move(rv));
    }
    for (auto &b : s.buttons)
        v.buttons.push_back({ b.key, b.label, b.details, b.toggle, b.on });
    if (s.game_loaded)
        v.sizes = s.sizes;
    for (auto &st : bundled_starters())
        v.starters.push_back(st.recipe.name);
    return v;
}

std::vector<uint8_t> SessionRemote::read(Target t, uint32_t offset, uint32_t length)
{
    return session().readMemory(t, offset, length);
}

RemoteControl::RemoteControl(QObject *parent) : QObject(parent)
{
    srv = std::make_unique<RemoteServer>(control, RemoteServer::Source{ &SessionRemote::status, &SessionRemote::read },
                                         this);
    connect(srv.get(), &RemoteServer::didSomething, this, [](const QString &who, const QString &what) {
        if (what != "connected")
            session().remoteNote("Remote (" + who.toStdString() + "): " + what.toStdString());
        else
            session().remoteNote("Remote: " + who.toStdString() + " connected");
    });
}

bool RemoteControl::start(quint16 port, const std::string &token)
{
    return srv->start(port, token);
}

void RemoteControl::stop()
{
    srv->stop();
}
