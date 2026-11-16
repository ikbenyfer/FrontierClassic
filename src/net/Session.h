// Multiplayer sessions. One device hosts and runs the simulation; the others
// send their orders and receive the game state (no lockstep, so an iPad 2 and
// a PC never drift apart).
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "../core/Game.h"
#include "Net.h"

namespace fc {

const int kDefaultPort = 47800;
const int kMaxClients = 7;
const unsigned kProtocolMagic = 0x504D4346;  // "FCMP"
const unsigned kProtocolVersion = 1;

enum OrderKind { ORD_SPAWN = 1, ORD_ATTACK, ORD_BUILD, ORD_UPGRADE, ORD_RECRUIT };

class HostSession {
public:
    bool start(int port, std::string* err);
    void stop();
    bool running() const { return listener_.open(); }
    int port() const { return port_; }
    int clientCount() const;  // connected and greeted

    // Lobby: accept players and keep them informed. `info` is shown to them.
    void pumpLobby(const std::string& info);
    // Start: the game must have been created with numHumans = 1 + clientCount().
    void startGame(Game& g);
    // In game: apply the clients' orders; a player who drops out becomes an AI.
    void pumpGame(Game& g);
    // Sends the state to everyone (throttled internally).
    void broadcast(Game& g, bool force = false);

private:
    struct Client {
        Conn conn;
        bool greeted = false;
        int playerId = 0;
        unsigned lastEventSeq = 0;
    };
    Listener listener_;
    std::vector<std::unique_ptr<Client> > clients_;
    int port_ = 0;
    bool inGame_ = false;
    unsigned lastLobbyMs_ = 0, lastSendMs_ = 0;
    std::string lastLobbyInfo_;
    int lastLobbyCount_ = -1;
    std::vector<uint8_t> sentOwner_;  // what the clients know
    uint32_t sentBuildingsHash_ = 0;
    bool namesDirty_ = true;

    void acceptNew(bool reject);
    void handleOrder(Game& g, Client& c, const std::vector<uint8_t>& payload);
    void dropDead(Game* g);
};

class ClientSession {
public:
    enum State { IDLE, CONNECTING, LOBBY, PLAYING, CLOSED };

    bool connect(const std::string& host, int port, std::string* err);
    void close();
    State state() const { return state_; }
    const std::string& closeReason() const { return reason_; }
    int lobbySlot() const { return slot_; }
    int lobbyCount() const { return count_; }
    const std::string& lobbyInfo() const { return info_; }

    // Reads messages and applies them to `g`. Returns true when the game has
    // just started (the caller must rebuild its map graphics).
    bool pump(Game& g, const std::string& mapsDir);
    void sendOrder(int kind, int a, int b, float f);
    // Messages from the host for this player ("not enough gold", ...).
    bool nextToast(std::string& out);

private:
    Conn conn_;
    State state_ = IDLE;
    std::string reason_, info_;
    int slot_ = 0, count_ = 0;
    bool helloSent_ = false;
    std::vector<std::string> toasts_;
    void applyDelta(Game& g, const std::vector<uint8_t>& payload);
};

}  // namespace fc
