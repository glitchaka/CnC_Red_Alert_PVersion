#include <ra/Engine.h>
#include <ra/Net.h>
#include <ra/RTS.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <winhttp.h>
#endif

namespace {

struct ClickFx { float x{}, y{}, life{}; };
enum class Screen { Splash, Menu, Lobby, Game, Editor };
enum class NetworkMode { Offline, Host, Client };

enum class Sheet : int {
    Grass = 0,
    Plains,
    Decor,
    Fences,
    Walls,
    Flooring,
    Wooden,
    Chest,
    Player,
    Slime
};

struct PaletteItem {
    Sheet sheet{};
    int sx{};
    int sy{};
    int sw{16};
    int sh{16};
    const char* name{};
};

float clampf(float v, float lo, float hi) { return std::max(lo, std::min(v, hi)); }
bool inside(float x, float y, float l, float t, float r, float b) { return x >= l && x <= r && y >= t && y <= b; }
ra::Rect tile16(int x, int y) { return {x * 16.0f, y * 16.0f, 16.0f, 16.0f}; }
ra::Rect frame48(int x, int y) { return {x * 48.0f, y * 48.0f, 48.0f, 48.0f}; }
ra::Rect frame32(int x, int y) { return {x * 32.0f, y * 32.0f, 32.0f, 32.0f}; }

constexpr std::uint16_t GamePort = 46000;
constexpr const char* Discover = "CC_DISCOVER_4";
constexpr const char* Welcome = "CC_WELCOME_4";

std::string trim(std::string s)
{
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    if (i) s.erase(0, i);
    const auto colon = s.find(':');
    if (colon != std::string::npos) s.erase(colon);
    return s;
}

std::string localIPv4()
{
#ifdef _WIN32
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return "unavailable";
    char host[256]{};
    std::string result = "unavailable";
    if (gethostname(host, sizeof(host)) == 0) {
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        addrinfo* list = nullptr;
        if (getaddrinfo(host, nullptr, &hints, &list) == 0) {
            for (auto* p = list; p; p = p->ai_next) {
                auto* a = reinterpret_cast<sockaddr_in*>(p->ai_addr);
                char buf[INET_ADDRSTRLEN]{};
                if (inet_ntop(AF_INET, &a->sin_addr, buf, sizeof(buf)) && std::string(buf) != "127.0.0.1") {
                    result = buf;
                    break;
                }
            }
            freeaddrinfo(list);
        }
    }
    WSACleanup();
    return result;
#else
    return "unavailable";
#endif
}

std::string publicIPv4()
{
#ifdef _WIN32
    HINTERNET session = WinHttpOpen(L"ClickCommand/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return "unavailable";
    WinHttpSetTimeouts(session, 1500, 1500, 1500, 1500);
    HINTERNET connect = WinHttpConnect(session, L"api.ipify.org", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) { WinHttpCloseHandle(session); return "unavailable"; }
    HINTERNET request = WinHttpOpenRequest(connect, L"GET", L"/", nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request) { WinHttpCloseHandle(connect); WinHttpCloseHandle(session); return "unavailable"; }
    std::string out;
    if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr)) {
        for (;;) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request, &available) || available == 0) break;
            std::string chunk(available, '\0');
            DWORD read = 0;
            if (!WinHttpReadData(request, chunk.data(), available, &read) || read == 0) break;
            chunk.resize(read);
            out += chunk;
            if (out.size() > 128) break;
        }
    }
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    out = trim(out);
    return out.empty() ? "unavailable" : out;
#else
    return "unavailable";
#endif
}

bool hasId(const std::vector<ra::rts::UnitId>& ids, ra::rts::UnitId id)
{
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

} // namespace

class ClickCommand final : public ra::Game {
public:
    explicit ClickCommand(std::string directHost) : directHost_(trim(std::move(directHost))) {}

    void onStart(ra::Engine& e) override
    {
        e.setWindowTitle("Click Command - Mystic Woods");
        grass_ = load(e, "assets/mystic/tilesets/grass.png");
        plains_ = load(e, "assets/mystic/plains.png");
        decor_ = load(e, "assets/mystic/tilesets/decor_16x16.png");
        fences_ = load(e, "assets/mystic/tilesets/fences.png");
        walls_ = load(e, "assets/mystic/tilesets/walls.png");
        flooring_ = load(e, "assets/mystic/tilesets/flooring.png");
        wooden_ = load(e, "assets/mystic/tilesets/wooden.png");
        chest_ = load(e, "assets/mystic/objects/chest_01.png");
        player_ = load(e, "assets/mystic/characters/player.png");
        slime_ = load(e, "assets/mystic/characters/slime.png");

        buildPalette();
        editorCells_.assign(MapW * MapH, 0);
        loadMap();

        localIp_ = localIPv4();
        publicIp_ = publicIPv4();

        if (!assetError_.empty()) {
            std::ofstream out("RA-Framework-error.txt", std::ios::trunc);
            out << assetError_;
        }

        if (!directHost_.empty()) startDirectJoin(e, directHost_);
    }

    void onStop(ra::Engine&) override { socket_.close(); }

    void onUpdate(ra::Engine& e, float dt) override
    {
        if (screen_ == Screen::Splash) {
            splash_ += dt;
            if (splash_ > 0.45f || e.keyPressed(ra::Key::Enter) || e.mousePressed(ra::MouseButton::Left)) screen_ = Screen::Menu;
            return;
        }
        if (screen_ == Screen::Menu) { updateMenu(e); return; }
        if (screen_ == Screen::Lobby) { updateLobby(e, dt); return; }
        if (screen_ == Screen::Editor) { updateEditor(e, dt); return; }
        updateGame(e, dt);
    }

    void onRender(ra::Engine& e) override
    {
        if (screen_ == Screen::Splash) renderSplash(e);
        else if (screen_ == Screen::Menu) renderMenu(e);
        else if (screen_ == Screen::Lobby) renderLobby(e);
        else if (screen_ == Screen::Editor) renderEditor(e);
        else renderGame(e);
    }

private:
    static constexpr float Tile = 32.0f;
    static constexpr int MapW = 128;
    static constexpr int MapH = 80;

    ra::Texture load(ra::Engine& e, const char* path)
    {
        try { return e.loadTexture(path); }
        catch (const std::exception& ex) {
            assetError_ += std::string(path) + ": " + ex.what() + "\n";
            return {};
        }
    }

    void buildPalette()
    {
        palette_ = {
            {Sheet::Grass, 0,0,16,16,"GRASS"},
            {Sheet::Plains,16,0,16,16,"DIRT EDGE"}, {Sheet::Plains,32,0,16,16,"DIRT"}, {Sheet::Plains,48,0,16,16,"DIRT EDGE"},
            {Sheet::Plains,16,64,16,16,"PATH"}, {Sheet::Plains,32,64,16,16,"PATH"}, {Sheet::Plains,48,64,16,16,"PATH"},
            {Sheet::Plains,64,64,16,16,"STONE"}, {Sheet::Plains,80,80,16,16,"STONE"},
            {Sheet::Decor,0,0,16,16,"DECOR"}, {Sheet::Decor,16,0,16,16,"DECOR"}, {Sheet::Decor,32,0,16,16,"DECOR"}, {Sheet::Decor,48,0,16,16,"DECOR"},
            {Sheet::Fences,0,16,16,16,"FENCE V"}, {Sheet::Fences,16,16,16,16,"FENCE H"},
            {Sheet::Walls,0,16,16,16,"WALL L"}, {Sheet::Walls,16,0,16,16,"WALL T"}, {Sheet::Walls,48,16,16,16,"WALL R"},
            {Sheet::Flooring,0,16,16,16,"FLOOR"}, {Sheet::Flooring,16,16,16,16,"FLOOR"}, {Sheet::Flooring,32,16,16,16,"FLOOR"},
            {Sheet::Wooden,0,0,16,16,"WOOD"}, {Sheet::Wooden,16,0,16,16,"WOOD"},
            {Sheet::Chest,0,0,16,16,"CHEST"},
            {Sheet::Player,0,0,48,48,"PLAYER"}, {Sheet::Slime,0,0,32,32,"SLIME"}
        };
    }

    const ra::Texture& textureFor(Sheet sheet) const
    {
        switch (sheet) {
            case Sheet::Grass: return grass_;
            case Sheet::Plains: return plains_;
            case Sheet::Decor: return decor_;
            case Sheet::Fences: return fences_;
            case Sheet::Walls: return walls_;
            case Sheet::Flooring: return flooring_;
            case Sheet::Wooden: return wooden_;
            case Sheet::Chest: return chest_;
            case Sheet::Player: return player_;
            case Sheet::Slime: return slime_;
        }
        return grass_;
    }

    void updateMenu(ra::Engine& e)
    {
        if (e.keyPressed(ra::Key::Escape)) { e.quit(); return; }
        const float w = static_cast<float>(e.windowWidth());
        const float h = static_cast<float>(e.windowHeight());
        const float x = w * 0.5f - 220.0f;
        const float y = h * 0.42f;
        const float mx = e.mouseX(), my = e.mouseY();

        if (e.keyPressed(ra::Key::Enter) || click(e, x, y, 440, 56)) {
            mode_ = NetworkMode::Offline;
            startGame(e);
            return;
        }
        if (click(e, x, y + 66, 440, 56)) { startHost(e); return; }
        if (click(e, x, y + 132, 440, 56)) { startJoinLan(e); return; }
        if (click(e, x, y + 198, 440, 56)) {
            char* clip = SDL_GetClipboardText();
            std::string host = clip ? trim(clip) : std::string{};
            if (clip) SDL_free(clip);
            if (!host.empty()) startDirectJoin(e, host);
            else menuStatus_ = "COPY HOST PUBLIC IP TO CLIPBOARD FIRST";
            return;
        }
        if (click(e, x, y + 264, 440, 56)) {
            screen_ = Screen::Editor;
            editorCamX_ = 0;
            editorCamY_ = 0;
            e.setWindowTitle("Click Command - Map Editor");
        }
    }

    bool click(ra::Engine& e, float x, float y, float w, float h) const
    {
        return e.mousePressed(ra::MouseButton::Left) && inside(e.mouseX(), e.mouseY(), x, y, x + w, y + h);
    }

    void startHost(ra::Engine& e)
    {
        socket_.close();
        mode_ = NetworkMode::Host;
        connected_ = false;
        peer_ = {};
        directTarget_ = {};
        lobbyStatus_ = socket_.open(GamePort, true) ? "WAITING FOR PLAYER..." : "HOST ERROR: " + socket_.lastError();
        screen_ = Screen::Lobby;
        e.setWindowTitle("Click Command - Host");
    }

    void startJoinLan(ra::Engine& e)
    {
        socket_.close();
        mode_ = NetworkMode::Client;
        connected_ = false;
        peer_ = {};
        directTarget_ = {};
        discoverTimer_ = 0;
        lobbyStatus_ = socket_.open(0, true) ? "SEARCHING LOCAL HOST..." : "JOIN ERROR: " + socket_.lastError();
        screen_ = Screen::Lobby;
        e.setWindowTitle("Click Command - Join LAN");
    }

    void startDirectJoin(ra::Engine& e, const std::string& host)
    {
        socket_.close();
        mode_ = NetworkMode::Client;
        connected_ = false;
        peer_ = {};
        directTarget_ = {trim(host), GamePort};
        discoverTimer_ = 0;
        lobbyStatus_ = socket_.open(0, true) ? "CONNECTING TO " + directTarget_.address + "..." : "JOIN ERROR: " + socket_.lastError();
        screen_ = Screen::Lobby;
        e.setWindowTitle("Click Command - Join Internet");
    }

    void updateLobby(ra::Engine& e, float dt)
    {
        if (e.keyPressed(ra::Key::Escape)) {
            socket_.close();
            mode_ = NetworkMode::Offline;
            screen_ = Screen::Menu;
            return;
        }
        if (mode_ == NetworkMode::Host && e.mousePressed(ra::MouseButton::Left)) {
            const float w = static_cast<float>(e.windowWidth());
            const float h = static_cast<float>(e.windowHeight());
            if (inside(e.mouseX(), e.mouseY(), w * .5f - 150, h * .58f, w * .5f + 150, h * .63f)) {
                if (publicIp_ != "unavailable") SDL_SetClipboardText(publicIp_.c_str());
                lobbyStatus_ = "PUBLIC IP COPIED";
            }
        }
        if (!socket_.isOpen()) return;

        if (mode_ == NetworkMode::Client) {
            discoverTimer_ -= dt;
            if (discoverTimer_ <= 0) {
                discoverTimer_ = 0.5f;
                const ra::net::Endpoint target = directTarget_ ? directTarget_ : ra::net::Endpoint{"255.255.255.255", GamePort};
                socket_.send(target, Discover, std::char_traits<char>::length(Discover));
            }
        }

        char buf[1024];
        ra::net::Endpoint from;
        int n = 0;
        while ((n = socket_.receive(buf, sizeof(buf), from)) > 0) {
            std::string m(buf, buf + n);
            if (mode_ == NetworkMode::Host && m == Discover) {
                peer_ = from;
                socket_.send(peer_, Welcome, std::char_traits<char>::length(Welcome));
                connected_ = true;
                startGame(e);
                return;
            }
            if (mode_ == NetworkMode::Client && m == Welcome) {
                peer_ = from;
                connected_ = true;
                startGame(e);
                return;
            }
        }
    }

    void startGame(ra::Engine& e)
    {
        screen_ = Screen::Game;
        resources_ = 0;
        world_.clear();
        effects_.clear();
        zoom_ = 1;
        drag_ = false;
        snapshotTimer_ = 0;
        cameraX_ = baseX_ - 420;
        cameraY_ = baseY_ - 285;
        if (mode_ != NetworkMode::Client) {
            spawn(baseX_ + 120, baseY_ + 120, 0);
            spawn(baseX_ - 70, baseY_ + 135, 0);
            spawn(baseX_ + 170, baseY_ + 55, 0);
            if (mode_ == NetworkMode::Host) {
                spawn(enemyX_ + 120, enemyY_ + 120, 1);
                spawn(enemyX_ - 70, enemyY_ + 135, 1);
                spawn(enemyX_ + 170, enemyY_ + 55, 1);
            }
        }
        e.setWindowTitle(mode_ == NetworkMode::Offline ? "Click Command - Mystic Woods RTS" : "Click Command - Online");
    }

    int localTeam() const { return mode_ == NetworkMode::Client ? 1 : 0; }
    int teamCount(int team) const { int n = 0; for (const auto& u : world_.units()) if (u.team == team) ++n; return n; }
    int nextCost(int team) const { return static_cast<int>(std::ceil(10.0 * std::pow(1.18, std::max(0, teamCount(team) - 3)))); }

    void spawn(float x, float y, int team)
    {
        auto id = world_.spawnUnit({x, y}, team, team == 0 ? 105.0f : 90.0f);
        world_.setAutonomous(id, true, 145.0f);
    }

    void buy(int team)
    {
        const int cost = nextCost(team);
        if (resources_ < cost) return;
        resources_ -= cost;
        const int n = teamCount(team);
        const float a = n * .91f;
        const float bx = team ? enemyX_ : baseX_;
        const float by = team ? enemyY_ : baseY_;
        spawn(bx + std::cos(a) * 155, by + std::sin(a) * 120, team);
    }

    void updateGame(ra::Engine& e, float dt)
    {
        if (e.keyPressed(ra::Key::Escape)) {
            socket_.close();
            connected_ = false;
            mode_ = NetworkMode::Offline;
            screen_ = Screen::Menu;
            return;
        }
        pumpNetwork();

        const float panel = hudX(e);
        const float mx = e.mouseX(), my = e.mouseY();
        if (e.mouseWheelY() != 0) zoom_ = clampf(zoom_ + e.mouseWheelY() * .12f, .65f, 2.5f);
        float dx = 0, dy = 0;
        if (e.keyDown(ra::Key::A) || e.keyDown(ra::Key::Left)) dx--;
        if (e.keyDown(ra::Key::D) || e.keyDown(ra::Key::Right)) dx++;
        if (e.keyDown(ra::Key::W) || e.keyDown(ra::Key::Up)) dy--;
        if (e.keyDown(ra::Key::S) || e.keyDown(ra::Key::Down)) dy++;
        if (mx < 8) dx--;
        if (mx < panel && mx > panel - 8) dx++;
        if (my < 8) dy--;
        if (my > e.windowHeight() - 8) dy++;
        const float step = 560.0f / zoom_ * dt;
        cameraX_ += dx * step;
        cameraY_ += dy * step;
        clampCamera(e);

        const float bx = panel + 18, by = 154, bw = e.windowWidth() - panel - 36;
        if (e.mousePressed(ra::MouseButton::Left)) {
            if (inside(mx, my, bx, by, bx + bw, by + 76)) {
                if (mode_ == NetworkMode::Client) send("B"); else buy(localTeam());
            } else if (mx < panel) {
                drag_ = true; sx_ = ex_ = mx; sy_ = ey_ = my;
            }
        }
        if (drag_ && e.mouseDown(ra::MouseButton::Left)) { ex_ = mx; ey_ = my; }
        if (drag_ && e.mouseReleased(ra::MouseButton::Left)) {
            ex_ = mx; ey_ = my;
            const float ax = ex_ - sx_, ay = ey_ - sy_;
            if (ax * ax + ay * ay < 36) {
                if (mode_ == NetworkMode::Client) send("C"); else ++resources_;
                effects_.push_back({mx, my, .7f});
                world_.selectPoint({wX(mx), wY(my)}, 26.0f / zoom_, false);
            } else {
                world_.selectRect({wX(std::min(sx_, ex_)), wY(std::min(sy_, ey_)), wX(std::max(sx_, ex_)), wY(std::max(sy_, ey_))}, false);
            }
            for (auto& u : world_.units()) if (u.selected && u.team != localTeam()) u.selected = false;
            drag_ = false;
        }
        if (e.mousePressed(ra::MouseButton::Right) && mx < panel) {
            const float x = wX(mx), y = wY(my);
            if (mode_ == NetworkMode::Client) sendMove(x, y); else world_.commandMove({x, y}, 34);
        }
        if (mode_ != NetworkMode::Client) {
            world_.update(dt);
            if (mode_ == NetworkMode::Host && connected_) {
                snapshotTimer_ -= dt;
                if (snapshotTimer_ <= 0) { snapshotTimer_ = .05f; sendSnapshot(); }
            }
        }
        for (auto& f : effects_) { f.life -= dt; f.y -= 24 * dt; }
        effects_.erase(std::remove_if(effects_.begin(), effects_.end(), [](const auto& f) { return f.life <= 0; }), effects_.end());
    }

    void send(const std::string& s) { if (connected_ && peer_ && socket_.isOpen()) socket_.send(peer_, s.data(), s.size()); }

    void sendMove(float x, float y)
    {
        std::ostringstream o;
        o << "M " << std::fixed << std::setprecision(2) << x << ' ' << y;
        for (const auto& u : world_.units()) if (u.selected && u.team == 1) o << ' ' << u.id;
        send(o.str());
    }

    void pumpNetwork()
    {
        if (mode_ == NetworkMode::Offline || !socket_.isOpen()) return;
        char b[65507];
        ra::net::Endpoint from;
        int n = 0;
        while ((n = socket_.receive(b, sizeof(b), from)) > 0) {
            std::string m(b, b + n);
            if (mode_ == NetworkMode::Host) hostPacket(m, from); else clientPacket(m, from);
        }
    }

    bool samePeer(const ra::net::Endpoint& f) const { return peer_ && f.address == peer_.address && f.port == peer_.port; }

    void hostPacket(const std::string& m, const ra::net::Endpoint& f)
    {
        if (!samePeer(f)) return;
        if (m == "C") { ++resources_; return; }
        if (m == "B") { buy(1); return; }
        if (m.rfind("M ", 0) == 0) {
            std::istringstream in(m.substr(2));
            float x = 0, y = 0;
            in >> x >> y;
            std::vector<ra::rts::UnitId> ids;
            ra::rts::UnitId id = 0;
            while (in >> id) {
                auto* u = world_.find(id);
                if (u && u->team == 1) ids.push_back(id);
            }
            world_.commandMoveUnits(ids, {x, y}, 34);
        }
    }

    void sendSnapshot()
    {
        std::ostringstream o;
        o << "S " << resources_ << ' ' << world_.units().size() << std::fixed << std::setprecision(2);
        for (const auto& u : world_.units()) {
            o << ' ' << u.id << ' ' << u.position.x << ' ' << u.position.y << ' ' << u.destination.x << ' ' << u.destination.y
              << ' ' << u.home.x << ' ' << u.home.y << ' ' << u.speed << ' ' << u.team << ' ' << (u.autonomous ? 1 : 0)
              << ' ' << static_cast<int>(u.order) << ' ' << u.aiStep << ' ' << u.thinkCooldown << ' ' << u.patrolRadius << ' ' << u.health;
        }
        send(o.str());
    }

    void clientPacket(const std::string& m, const ra::net::Endpoint& f)
    {
        if (!samePeer(f) || m.rfind("S ", 0) != 0) return;
        std::vector<ra::rts::UnitId> selected;
        for (const auto& u : world_.units()) if (u.selected) selected.push_back(u.id);
        std::istringstream in(m.substr(2));
        int count = 0;
        if (!(in >> resources_ >> count) || count < 0 || count > 2048) return;
        std::vector<ra::rts::Unit> snapshot;
        snapshot.reserve(count);
        for (int i = 0; i < count; ++i) {
            ra::rts::Unit u;
            int autonomous = 0, order = 0;
            if (!(in >> u.id >> u.position.x >> u.position.y >> u.destination.x >> u.destination.y >> u.home.x >> u.home.y
                     >> u.speed >> u.team >> autonomous >> order >> u.aiStep >> u.thinkCooldown >> u.patrolRadius >> u.health)) return;
            u.autonomous = autonomous != 0;
            u.order = static_cast<ra::rts::UnitOrder>(std::max(0, std::min(order, 2)));
            u.maxHealth = 100;
            u.radius = 12;
            u.selected = u.team == 1 && hasId(selected, u.id);
            snapshot.push_back(u);
        }
        world_.replaceUnits(snapshot);
    }

    void updateEditor(ra::Engine& e, float dt)
    {
        if (e.keyPressed(ra::Key::Escape)) {
            screen_ = Screen::Menu;
            e.setWindowTitle("Click Command - Mystic Woods");
            return;
        }
        const float panel = static_cast<float>(e.windowWidth()) - 330.0f;
        const float mx = e.mouseX(), my = e.mouseY();
        if (e.mouseWheelY() != 0) editorZoom_ = clampf(editorZoom_ + e.mouseWheelY() * .12f, .5f, 3.0f);

        float dx = 0, dy = 0;
        if (e.keyDown(ra::Key::A) || e.keyDown(ra::Key::Left)) dx--;
        if (e.keyDown(ra::Key::D) || e.keyDown(ra::Key::Right)) dx++;
        if (e.keyDown(ra::Key::W) || e.keyDown(ra::Key::Up)) dy--;
        if (e.keyDown(ra::Key::S) || e.keyDown(ra::Key::Down)) dy++;
        const float speed = 650.0f / editorZoom_ * dt;
        editorCamX_ = clampf(editorCamX_ + dx * speed, 0, std::max(0.0f, MapW * Tile - panel / editorZoom_));
        editorCamY_ = clampf(editorCamY_ + dy * speed, 0, std::max(0.0f, MapH * Tile - e.windowHeight() / editorZoom_));

        if (mx >= panel) {
            if (e.mousePressed(ra::MouseButton::Left)) {
                if (inside(mx, my, panel + 14, 48, panel + 154, 82)) { saveMap(); return; }
                if (inside(mx, my, panel + 166, 48, panel + 316, 82)) { loadMap(); return; }
                const int cols = 4;
                const float cell = 70;
                const float startY = 118;
                const int px = static_cast<int>((mx - (panel + 14)) / cell);
                const int py = static_cast<int>((my - startY) / cell);
                if (px >= 0 && px < cols && py >= 0) {
                    const int idx = palettePage_ * 20 + py * cols + px;
                    if (idx >= 0 && idx < static_cast<int>(palette_.size())) selectedPalette_ = idx;
                }
                if (inside(mx, my, panel + 14, e.windowHeight() - 58, panel + 154, e.windowHeight() - 20)) {
                    if (palettePage_ > 0) --palettePage_;
                }
                if (inside(mx, my, panel + 166, e.windowHeight() - 58, panel + 316, e.windowHeight() - 20)) {
                    const int maxPage = std::max(0, (static_cast<int>(palette_.size()) - 1) / 20);
                    if (palettePage_ < maxPage) ++palettePage_;
                }
            }
            return;
        }

        if (e.mouseDown(ra::MouseButton::Left) || e.mouseDown(ra::MouseButton::Right)) {
            const int tx = static_cast<int>((editorCamX_ + mx / editorZoom_) / Tile);
            const int ty = static_cast<int>((editorCamY_ + my / editorZoom_) / Tile);
            if (tx >= 0 && tx < MapW && ty >= 0 && ty < MapH) {
                editorCells_[ty * MapW + tx] = e.mouseDown(ra::MouseButton::Right) ? 0 : selectedPalette_;
                customMapLoaded_ = true;
            }
        }
    }

    void saveMap()
    {
        std::filesystem::create_directories("maps");
        std::ofstream out("maps/editor_map.txt", std::ios::trunc);
        if (!out) { editorStatus_ = "SAVE FAILED"; return; }
        out << "CCMAP1 " << MapW << ' ' << MapH << '\n';
        for (int y = 0; y < MapH; ++y) {
            for (int x = 0; x < MapW; ++x) out << editorCells_[y * MapW + x] << (x + 1 == MapW ? '\n' : ' ');
        }
        customMapLoaded_ = true;
        editorStatus_ = "SAVED maps/editor_map.txt";
    }

    void loadMap()
    {
        std::ifstream in("maps/editor_map.txt");
        if (!in) { editorStatus_ = "NO SAVED MAP"; return; }
        std::string magic;
        int w = 0, h = 0;
        if (!(in >> magic >> w >> h) || magic != "CCMAP1" || w != MapW || h != MapH) {
            editorStatus_ = "INVALID MAP";
            return;
        }
        std::vector<int> tmp(MapW * MapH, 0);
        for (int& v : tmp) {
            if (!(in >> v)) { editorStatus_ = "INVALID MAP DATA"; return; }
            v = std::max(0, std::min(v, static_cast<int>(palette_.size()) - 1));
        }
        editorCells_ = std::move(tmp);
        customMapLoaded_ = true;
        editorStatus_ = "LOADED";
    }

    void renderSplash(ra::Engine& e)
    {
        const float w = static_cast<float>(e.windowWidth()), h = static_cast<float>(e.windowHeight());
        e.clear({9,14,10,255});
        for (int y = 0; y < 12; ++y) for (int x = 0; x < 22; ++x)
            e.drawTexture(grass_, {w * .5f - 352 + x * 32, h * .04f + y * 32, 32, 32});
        e.drawTextureRegion(player_, frame48(0,0), {w * .5f - 48, h * .28f, 96, 96});
        e.drawText(w * .5f - 60, h * .46f, "CLICK COMMAND", {245,238,205,255});
        e.drawText(w * .5f - 72, h * .50f, "MYSTIC WOODS RTS", {190,220,190,255});
    }

    void menuButton(ra::Engine& e, float x, float y, const char* text)
    {
        const bool hot = inside(e.mouseX(), e.mouseY(), x, y, x + 440, y + 56);
        e.fillRect({x,y,440,56}, hot ? ra::Color{53,104,52,245} : ra::Color{29,63,34,245});
        e.drawRect({x,y,440,56}, {125,167,121,255});
        e.drawText(x + 142, y + 24, text, {247,244,220,255});
    }

    void renderMenu(ra::Engine& e)
    {
        const float w = static_cast<float>(e.windowWidth()), h = static_cast<float>(e.windowHeight());
        e.clear({8,12,9,255});
        for (int y = 0; y < 18; ++y) for (int x = 0; x < 30; ++x) {
            const float dx = w * .5f - 480 + x * 32, dy = h * .01f + y * 32;
            e.drawTexture(grass_, {dx,dy,32,32});
            if ((x * 13 + y * 7) % 31 == 0) e.drawTextureRegion(decor_, tile16((x+y)%4,0), {dx,dy,32,32});
        }
        e.fillRect({w * .5f - 300, h * .18f, 600, 700}, {10,16,12,230});
        e.drawText(w * .5f - 60, h * .24f, "CLICK COMMAND", {245,238,205,255});
        e.drawText(w * .5f - 92, h * .28f, "MYSTIC WOODS RTS + ONLINE", {190,220,190,255});
        e.drawText(w * .5f - 180, h * .32f, ("PUBLIC IP: " + publicIp_ + ":46000").c_str(), {255,220,90,255});
        e.drawText(w * .5f - 180, h * .35f, ("LOCAL IP:  " + localIp_ + ":46000").c_str(), {170,195,175,255});

        const float x = w * .5f - 220, y = h * .42f;
        menuButton(e, x, y, "SINGLE PLAYER");
        menuButton(e, x, y + 66, "HOST GAME");
        menuButton(e, x, y + 132, "JOIN LAN");
        menuButton(e, x, y + 198, "JOIN INTERNET FROM CLIPBOARD");
        menuButton(e, x, y + 264, "MAP EDITOR");
        e.drawText(w * .5f - 175, y + 330, "Internet: copy host public IP, then click JOIN INTERNET", {170,195,175,255});
        if (!menuStatus_.empty()) e.drawText(w * .5f - 170, y + 356, menuStatus_, {255,130,100,255});
    }

    void renderLobby(ra::Engine& e)
    {
        const float w = static_cast<float>(e.windowWidth()), h = static_cast<float>(e.windowHeight());
        e.clear({9,14,10,255});
        e.fillRect({w * .5f - 360, h * .25f, 720, 430}, {14,22,16,255});
        e.drawRect({w * .5f - 360, h * .25f, 720, 430}, {125,167,121,255});
        e.drawText(w * .5f - 70, h * .31f, mode_ == NetworkMode::Host ? "HOST GAME" : "JOIN GAME", {245,238,205,255});
        e.drawText(w * .5f - 165, h * .39f, lobbyStatus_, {190,220,190,255});
        if (mode_ == NetworkMode::Host) {
            e.drawText(w * .5f - 190, h * .46f, ("PUBLIC IP TO SHARE: " + publicIp_ + ":46000").c_str(), {255,220,90,255});
            e.drawText(w * .5f - 190, h * .50f, ("LOCAL IP: " + localIp_ + ":46000").c_str(), {170,195,175,255});
            e.fillRect({w * .5f - 150, h * .58f, 300, h * .05f}, {36,82,43,255});
            e.drawText(w * .5f - 74, h * .60f, "COPY PUBLIC IP", {245,238,205,255});
        } else if (directTarget_) {
            e.drawText(w * .5f - 150, h * .48f, ("TARGET: " + directTarget_.address + ":46000").c_str(), {255,220,90,255});
        }
        e.drawText(w * .5f - 45, h * .69f, "ESC CANCEL", {150,175,155,255});
    }

    void renderGame(ra::Engine& e)
    {
        const float vw = hudX(e), vh = static_cast<float>(e.windowHeight());
        e.clear({8,13,9,255});
        drawTerrain(e, vw, vh);
        drawCamp(e, 0, baseX_, baseY_);
        if (mode_ != NetworkMode::Offline) drawCamp(e, 1, enemyX_, enemyY_);
        drawUnits(e, vw, vh);
        if (drag_) {
            const float l = std::min(sx_,ex_), r = std::max(sx_,ex_), t = std::min(sy_,ey_), b = std::max(sy_,ey_);
            e.drawRect({l,t,r-l,b-t}, {96,238,104,255});
        }
        for (const auto& f : effects_) e.drawText(f.x - 4, f.y, "+1", {255,220,70,255});
        drawHud(e, vw, vh);
    }

    void renderEditor(ra::Engine& e)
    {
        const float panel = static_cast<float>(e.windowWidth()) - 330.0f;
        const float cell = Tile * editorZoom_;
        e.clear({8,13,9,255});

        const int fx = std::max(0, static_cast<int>(std::floor(editorCamX_ / Tile)) - 1);
        const int fy = std::max(0, static_cast<int>(std::floor(editorCamY_ / Tile)) - 1);
        const int lx = std::min(MapW - 1, fx + static_cast<int>(panel / cell) + 3);
        const int ly = std::min(MapH - 1, fy + static_cast<int>(e.windowHeight() / cell) + 3);
        for (int y = fy; y <= ly; ++y) {
            for (int x = fx; x <= lx; ++x) {
                const int pi = std::max(0, std::min(editorCells_[y * MapW + x], static_cast<int>(palette_.size()) - 1));
                const auto& item = palette_[pi];
                const float dx = (x * Tile - editorCamX_) * editorZoom_;
                const float dy = (y * Tile - editorCamY_) * editorZoom_;
                e.drawTextureRegion(textureFor(item.sheet), {(float)item.sx,(float)item.sy,(float)item.sw,(float)item.sh}, {dx,dy,cell,cell});
                e.drawRect({dx,dy,cell,cell}, {25,45,29,100});
            }
        }

        e.fillRect({panel,0,330.0f,(float)e.windowHeight()}, {12,18,14,255});
        e.drawText(panel + 14, 16, "MAP EDITOR - MYSTIC WOODS", {245,238,205,255});
        e.fillRect({panel + 14,48,140,34}, {36,82,43,255});
        e.fillRect({panel + 166,48,150,34}, {36,82,43,255});
        e.drawText(panel + 58,60,"SAVE",{245,238,205,255});
        e.drawText(panel + 211,60,"LOAD",{245,238,205,255});
        e.drawText(panel + 14,92,("SELECTED: " + std::string(palette_[selectedPalette_].name)).c_str(),{190,220,190,255});

        const int start = palettePage_ * 20;
        const int end = std::min(start + 20, static_cast<int>(palette_.size()));
        for (int i = start; i < end; ++i) {
            const int local = i - start;
            const int col = local % 4, row = local / 4;
            const float x = panel + 14 + col * 70.0f;
            const float y = 118 + row * 70.0f;
            const auto& item = palette_[i];
            e.fillRect({x,y,62,62}, i == selectedPalette_ ? ra::Color{65,110,68,255} : ra::Color{24,36,27,255});
            e.drawTextureRegion(textureFor(item.sheet), {(float)item.sx,(float)item.sy,(float)item.sw,(float)item.sh}, {x + 7,y + 7,48,48});
            e.drawRect({x,y,62,62}, i == selectedPalette_ ? ra::Color{190,235,170,255} : ra::Color{80,100,82,255});
        }

        const float bottom = static_cast<float>(e.windowHeight()) - 58;
        e.fillRect({panel + 14,bottom,140,38},{30,62,35,255});
        e.fillRect({panel + 166,bottom,150,38},{30,62,35,255});
        e.drawText(panel + 57,bottom + 15,"PREV",{220,225,210,255});
        e.drawText(panel + 214,bottom + 15,"NEXT",{220,225,210,255});
        e.drawText(panel + 14,bottom - 48,"LMB paint  RMB grass  Wheel zoom  WASD move",{150,175,155,255});
        e.drawText(panel + 14,bottom - 26,"ESC back to game menu",{150,175,155,255});
        if (!editorStatus_.empty()) e.drawText(panel + 14,bottom - 70,editorStatus_,{255,220,90,255});
    }

    void drawWorldTile(ra::Engine& e, const ra::Texture& tex, ra::Rect src, float wx, float wy)
    {
        const float s = Tile * zoom_;
        e.drawTextureRegion(tex, src, {(wx - cameraX_) * zoom_, (wy - cameraY_) * zoom_, s, s});
    }

    void drawGeneratedTerrain(ra::Engine& e, float vw, float vh)
    {
        const int fx = std::max(0, static_cast<int>(std::floor(cameraX_ / Tile)) - 1);
        const int fy = std::max(0, static_cast<int>(std::floor(cameraY_ / Tile)) - 1);
        const int lx = std::min(MapW - 1, fx + static_cast<int>(vw / (Tile * zoom_)) + 4);
        const int ly = std::min(MapH - 1, fy + static_cast<int>(vh / (Tile * zoom_)) + 4);
        for (int y = fy; y <= ly; ++y) for (int x = fx; x <= lx; ++x) {
            const float s = Tile * zoom_, dx = (x * Tile - cameraX_) * zoom_, dy = (y * Tile - cameraY_) * zoom_;
            e.drawTexture(grass_, {dx,dy,s,s});
            const int h = (x * 31 + y * 47) % 113;
            if (h == 7 || h == 29 || h == 77) e.drawTextureRegion(decor_, tile16((x+y)%4,0), {dx,dy,s,s});
        }
        for (int y = 16; y < 25; ++y) for (int x = 19; x < 34; ++x) drawWorldTile(e, plains_, tile16(2,1), x*Tile, y*Tile);
        for (int y = 50; y < 59; ++y) for (int x = 76; x < 91; ++x) drawWorldTile(e, plains_, tile16(2,1), x*Tile, y*Tile);
        for (int y = 29; y < 32; ++y) for (int x = 34; x < 62; ++x) drawWorldTile(e, plains_, tile16(2,1), x*Tile, y*Tile);
        for (int y = 22; y < 53; ++y) for (int x = 54; x < 57; ++x) drawWorldTile(e, plains_, tile16(2,1), x*Tile, y*Tile);
    }

    void drawTerrain(ra::Engine& e, float vw, float vh)
    {
        if (!customMapLoaded_) { drawGeneratedTerrain(e, vw, vh); return; }
        const int fx = std::max(0, static_cast<int>(std::floor(cameraX_ / Tile)) - 1);
        const int fy = std::max(0, static_cast<int>(std::floor(cameraY_ / Tile)) - 1);
        const int lx = std::min(MapW - 1, fx + static_cast<int>(vw / (Tile * zoom_)) + 4);
        const int ly = std::min(MapH - 1, fy + static_cast<int>(vh / (Tile * zoom_)) + 4);
        for (int y = fy; y <= ly; ++y) for (int x = fx; x <= lx; ++x) {
            const int pi = std::max(0, std::min(editorCells_[y * MapW + x], static_cast<int>(palette_.size()) - 1));
            const auto& item = palette_[pi];
            const float s = Tile * zoom_, dx = (x * Tile - cameraX_) * zoom_, dy = (y * Tile - cameraY_) * zoom_;
            e.drawTextureRegion(textureFor(item.sheet), {(float)item.sx,(float)item.sy,(float)item.sw,(float)item.sh}, {dx,dy,s,s});
        }
    }

    void drawCamp(ra::Engine& e, int team, float wx, float wy)
    {
        for (int y = -2; y <= 2; ++y) for (int x = -3; x <= 3; ++x) drawWorldTile(e, wooden_, tile16(0,0), wx + x*Tile, wy + y*Tile);
        for (int x = -3; x <= 3; ++x) drawWorldTile(e, flooring_, tile16((x+3)%5,1), wx + x*Tile, wy + 3*Tile);
        for (int x = -2; x <= 2; ++x) {
            drawWorldTile(e, walls_, tile16(1,0), wx + x*Tile, wy - 2*Tile);
            drawWorldTile(e, walls_, tile16(1,3), wx + x*Tile, wy + 2*Tile);
        }
        for (int y = -1; y <= 1; ++y) {
            drawWorldTile(e, walls_, tile16(0,1), wx - 3*Tile, wy + y*Tile);
            drawWorldTile(e, walls_, tile16(3,1), wx + 3*Tile, wy + y*Tile);
        }
        for (int x = -4; x <= 4; ++x) {
            drawWorldTile(e, fences_, tile16(1,1), wx + x*Tile, wy - 4*Tile);
            drawWorldTile(e, fences_, tile16(1,1), wx + x*Tile, wy + 4*Tile);
        }
        for (int y = -3; y <= 3; ++y) {
            drawWorldTile(e, fences_, tile16(0,1), wx - 5*Tile, wy + y*Tile);
            drawWorldTile(e, fences_, tile16(0,1), wx + 5*Tile, wy + y*Tile);
        }
        const float cx = (wx + 2*Tile - cameraX_) * zoom_, cy = (wy + Tile - cameraY_) * zoom_;
        e.drawTextureRegion(chest_, tile16(0,0), {cx,cy,32*zoom_,32*zoom_});
        e.drawText((wx-cameraX_-52)*zoom_, (wy-cameraY_-150)*zoom_, team == 0 ? "HUMAN CAMP" : "SLIME CAMP",
                   team == 0 ? ra::Color{220,240,255,255} : ra::Color{180,230,180,255});
    }

    void drawUnits(ra::Engine& e, float vw, float vh)
    {
        int i = 0;
        for (const auto& u : world_.units()) {
            const float x = (u.position.x - cameraX_) * zoom_;
            const float y = (u.position.y - cameraY_) * zoom_;
            const float s = (u.team ? 42.0f : 52.0f) * zoom_;
            if (x < -s || y < -s || x > vw + s || y > vh + s) { ++i; continue; }
            if (u.selected) e.drawRect({x-s*.55f,y-s*.55f,s*1.1f,s*1.1f},{96,238,104,255});
            if (u.team == 0) e.drawTextureRegion(player_, frame48((i + static_cast<int>(u.aiStep)) % 6,0), {x-s*.5f,y-s*.7f,s,s});
            else e.drawTextureRegion(slime_, frame32((i + static_cast<int>(u.aiStep)) % 4,0), {x-s*.5f,y-s*.5f,s,s});
            ++i;
        }
    }

    void drawHud(ra::Engine& e, float x, float h)
    {
        const float pw = e.windowWidth() - x;
        e.fillRect({x,0,pw,h},{12,18,14,255});
        e.drawText(x+18,18,"CLICK COMMAND",{245,238,203,255});
        e.drawText(x+18,42, mode_ == NetworkMode::Offline ? "SINGLE PLAYER" : (mode_ == NetworkMode::Host ? "ONLINE HOST" : "ONLINE CLIENT"), {155,190,160,255});
        if (mode_ == NetworkMode::Host) e.drawText(x+18,68,("IP: " + publicIp_ + ":46000").c_str(),{255,220,90,255});
        e.drawText(x+18,98,"RESOURCES",{160,180,164,255});
        e.drawText(x+18,120,std::to_string(resources_),{255,216,64,255});
        const int cost = nextCost(localTeam());
        const bool can = resources_ >= cost;
        const float bx=x+18,by=154,bw=pw-36;
        e.fillRect({bx,by,bw,76},can?ra::Color{43,92,51,255}:ra::Color{40,45,41,255});
        e.drawRect({bx,by,bw,76},{105,133,111,255});
        if(localTeam()==0)e.drawTextureRegion(player_,frame48(0,0),{bx+8,by+6,62,62});
        else e.drawTextureRegion(slime_,frame32(0,0),{bx+14,by+12,50,50});
        e.drawText(bx+82,by+18,localTeam()==0?"HUMAN UNIT":"SLIME UNIT",{243,242,218,255});
        e.drawText(bx+82,by+44,("COST: "+std::to_string(cost)).c_str(),can?ra::Color{255,216,64,255}:ra::Color{132,138,132,255});
        e.drawText(x+18,255,("YOUR UNITS: "+std::to_string(teamCount(localTeam()))).c_str(),{190,203,193,255});
        e.drawText(x+18,280,"LMB select/click  RMB move",{150,170,154,255});
        e.drawText(x+18,304,"WASD camera  Wheel zoom",{150,170,154,255});
        e.drawText(x+18,328,"ESC menu",{150,170,154,255});
    }

    float hudX(const ra::Engine& e) const { return std::max(720.0f, static_cast<float>(e.windowWidth()) - 320.0f); }
    float wX(float x) const { return cameraX_ + x / zoom_; }
    float wY(float y) const { return cameraY_ + y / zoom_; }

    void clampCamera(const ra::Engine& e)
    {
        const float vw = hudX(e) / zoom_, vh = e.windowHeight() / zoom_;
        cameraX_ = clampf(cameraX_, 0, std::max(0.0f, MapW * Tile - vw));
        cameraY_ = clampf(cameraY_, 0, std::max(0.0f, MapH * Tile - vh));
    }

    Screen screen_{Screen::Splash};
    NetworkMode mode_{NetworkMode::Offline};
    float splash_{};
    int resources_{};
    ra::rts::World world_{{0,0,MapW*Tile,MapH*Tile}};
    std::vector<ClickFx> effects_;
    float cameraX_{}, cameraY_{}, zoom_{1};
    bool drag_{};
    float sx_{}, sy_{}, ex_{}, ey_{};
    const float baseX_ = 26*Tile, baseY_ = 20*Tile;
    const float enemyX_ = 86*Tile, enemyY_ = 54*Tile;

    ra::Texture grass_, plains_, decor_, fences_, walls_, flooring_, wooden_, chest_, player_, slime_;
    std::string assetError_;

    ra::net::DatagramSocket socket_;
    ra::net::Endpoint peer_{};
    ra::net::Endpoint directTarget_{};
    bool connected_{};
    float discoverTimer_{};
    float snapshotTimer_{};
    std::string lobbyStatus_;
    std::string menuStatus_;
    std::string directHost_;

    std::string localIp_{"unavailable"};
    std::string publicIp_{"unavailable"};

    std::vector<PaletteItem> palette_;
    std::vector<int> editorCells_;
    int selectedPalette_{};
    int palettePage_{};
    float editorCamX_{}, editorCamY_{}, editorZoom_{1};
    bool customMapLoaded_{};
    std::string editorStatus_;
};

int main()
{
    ra::EngineConfig config;
    config.title = "Click Command";
    config.width = 1440;
    config.height = 900;
    config.resizable = true;
    config.vsync = true;

    const char* envHost = std::getenv("CC_HOST");
    const std::string directHost = envHost ? envHost : "";

    try {
        ra::Engine engine(config);
        ClickCommand game(directHost);
        return engine.run(game);
    } catch (const std::exception& ex) {
        std::ofstream out("RA-Framework-error.txt", std::ios::trunc);
        out << "Fatal startup/runtime error:\n" << ex.what() << '\n';
        std::fprintf(stderr, "Fatal error: %s\n", ex.what());
#ifdef _WIN32
        MessageBoxA(nullptr, ex.what(), "Click Command - Fatal Error", MB_OK | MB_ICONERROR);
#endif
        return 1;
    }
}
