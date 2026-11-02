#pragma once
#include "RemoteServer.hpp"

#include <memory>

// The session behind the remote page (#53): what a remote may do, done through the same
// CorruptionSession calls the panel makes, and what it may see.
class SessionRemote : public corrupt::remote::Control
{
  public:
    std::string set_active(bool on) override;
    void back_to_normal() override;
    void normal_here() override;
    void reroll() override;
    void mark_crash() override;
    void set_chaos(double chaos) override;
    void set_rule_enabled(size_t index, bool on) override;
    void set_rule_strength(size_t index, double strength) override;
    void press_hotkey(int n) override;
    std::string paint(corrupt::Target t, const std::vector<uint32_t> &offsets, uint8_t value) override;
    size_t starter_count() override;
    std::string starter_name(size_t index) override;
    std::string load_starter(size_t index) override;
    size_t rule_count() override;
    size_t memory_size(corrupt::Target t) override;

    static corrupt::remote::StatusView status();
    static std::vector<uint8_t> read(corrupt::Target t, uint32_t offset, uint32_t length);
};

// The server and the session together, as the panel switches it on and off.
class RemoteControl : public QObject
{
    Q_OBJECT

  public:
    explicit RemoteControl(QObject *parent = nullptr);
    bool start(quint16 port, const std::string &token);
    void stop();
    RemoteServer &server() { return *srv; }

  private:
    SessionRemote control;
    std::unique_ptr<RemoteServer> srv;
};
