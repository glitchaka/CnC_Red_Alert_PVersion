#include <ra/Engine.h>
#include <ra/Net.h>
#include <ra/RTS.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {

struct ClickFx { float x{0}; float y{0}; float life{0}; };
enum class Screen { Splash, Menu, Lobby, Game };
enum class NetworkMode { Offline, Host, Client };

float clampf(float v, float lo, float hi) { return std::max(lo, std::min(v, hi)); }
bool inside(float x, float y, float l, float t, float r, float b) { return x >= l && x <= r && y >= t && y <= b; }
ra::Rect tile16(int x, int y) { return {x * 16.0f, y * 16.0f, 16.0f, 16.0f}; }
ra::Rect cell10(int x, int y) { return {x * 10.0f, y * 10.0f, 10.0f, 10.0f}; }

constexpr std::uint16_t LanPort = 46000;
constexpr const char* DiscoverPacket = "CC_DISCOVER_1";
constexpr const char* WelcomePacket = "CC_WELCOME_1";

bool containsId(const std::vector<ra::rts::UnitId>& ids, ra::rts::UnitId id)
{
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

} // namespace

class ClickerRTS final : public ra::Game {
public:
    void onStart(ra::Engine& e) override
    {
        e.setWindowTitle("Click Command - Loading");
        plains_ = safeLoad(e, "assets/mystic/plains.png");
        people_ = safeLoad(e, "assets/bit-bonanza/People.png");
        ui_ = safeLoad(e, "assets/bit-bonanza/UI.png");
        future_ = safeLoad(e, "assets/bit-bonanza/Misc-Future.png");

        if (!assetError_.empty()) {
            std::ofstream out("RA-Framework-error.txt", std::ios::trunc);
            out << assetError_;
        }
    }

    void onStop(ra::Engine&) override
    {
        socket_.close();
    }

    void onUpdate(ra::Engine& e, float dt) override
    {
        if (screen_ == Screen::Splash) {
            splash_ += dt;
            if (splash_ >= 0.8f || e.keyPressed(ra::Key::Enter) || e.mousePressed(ra::MouseButton::Left)) {
                screen_ = Screen::Menu;
                e.setWindowTitle("Click Command");
            }
            return;
        }

        if (screen_ == Screen::Menu) {
            updateMenu(e);
            return;
        }

        if (screen_ == Screen::Lobby) {
            updateLobby(e, dt);
            return;
        }

        updateGame(e, dt);
    }

    void onRender(ra::Engine& e) override
    {
        if (screen_ == Screen::Splash) renderSplash(e);
        else if (screen_ == Screen::Menu) renderMenu(e);
        else if (screen_ == Screen::Lobby) renderLobby(e);
        else renderGame(e);
    }

private:
    static constexpr float Tile = 32.0f;
    static constexpr int MapW = 128;
    static constexpr int MapH = 80;

    ra::Texture safeLoad(ra::Engine& e, const char* path)
    {
        try {
            return e.loadTexture(path);
        } catch (const std::exception& ex) {
            assetError_ += std::string(path) + ": " + ex.what() + "\n";
            return {};
        }
    }

    void updateMenu(ra::Engine& e)
    {
        if (e.keyPressed(ra::Key::Escape)) {
            e.quit();
            return;
        }

        const float w = static_cast<float>(e.windowWidth());
        const float h = static_cast<float>(e.windowHeight());
        const float bx = w * 0.5f - 210.0f;
        const float y1 = h * 0.54f;
        const float y2 = y1 + 78.0f;
        const float y3 = y2 + 78.0f;
        const float mx = e.mouseX();
        const float my = e.mouseY();

        if (e.keyPressed(ra::Key::Enter) ||
            (e.mousePressed(ra::MouseButton::Left) && inside(mx, my, bx, y1, bx + 420.0f, y1 + 62.0f))) {
            mode_ = NetworkMode::Offline;
            startGame(e);
            return;
        }

        if (e.mousePressed(ra::MouseButton::Left) && inside(mx, my, bx, y2, bx + 420.0f, y2 + 62.0f)) {
            startHost(e);
            return;
        }

        if (e.mousePressed(ra::MouseButton::Left) && inside(mx, my, bx, y3, bx + 420.0f, y3 + 62.0f)) {
            startJoin(e);
        }
    }

    void startHost(ra::Engine& e)
    {
        socket_.close();
        mode_ = NetworkMode::Host;
        connected_ = false;
        peer_ = {};
        if (!socket_.open(LanPort, true)) {
            lobbyStatus_ = "HOST ERROR: " + socket_.lastError();
        } else {
            lobbyStatus_ = "WAITING FOR LAN PLAYER...";
        }
        screen_ = Screen::Lobby;
        e.setWindowTitle("Click Command - Host LAN");
    }

    void startJoin(ra::Engine& e)
    {
        socket_.close();
        mode_ = NetworkMode::Client;
        connected_ = false;
        peer_ = {};
        discoverTimer_ = 0.0f;
        if (!socket_.open(0, true)) {
            lobbyStatus_ = "JOIN ERROR: " + socket_.lastError();
        } else {
            lobbyStatus_ = "SEARCHING LAN HOST...";
        }
        screen_ = Screen::Lobby;
        e.setWindowTitle("Click Command - Join LAN");
    }

    void updateLobby(ra::Engine& e, float dt)
    {
        if (e.keyPressed(ra::Key::Escape)) {
            socket_.close();
            mode_ = NetworkMode::Offline;
            connected_ = false;
            screen_ = Screen::Menu;
            e.setWindowTitle("Click Command");
            return;
        }

        if (!socket_.isOpen()) return;

        if (mode_ == NetworkMode::Client) {
            discoverTimer_ -= dt;
            if (discoverTimer_ <= 0.0f) {
                discoverTimer_ = 0.65f;
                const ra::net::Endpoint broadcast{"255.255.255.255", LanPort};
                socket_.send(broadcast, DiscoverPacket, std::char_traits<char>::length(DiscoverPacket));
            }
        }

        char buffer[1024];
        ra::net::Endpoint from;
        int received = 0;
        while ((received = socket_.receive(buffer, sizeof(buffer), from)) > 0) {
            const std::string msg(buffer, buffer + received);
            if (mode_ == NetworkMode::Host && msg == DiscoverPacket) {
                peer_ = from;
                socket_.send(peer_, WelcomePacket, std::char_traits<char>::length(WelcomePacket));
                connected_ = true;
                lobbyStatus_ = "PLAYER CONNECTED";
                startGame(e);
                return;
            }
            if (mode_ == NetworkMode::Client && msg == WelcomePacket) {
                peer_ = from;
                connected_ = true;
                lobbyStatus_ = "CONNECTED TO HOST";
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
        zoom_ = 1.0f;
        drag_ = false;
        snapshotTimer_ = 0.0f;
        cameraX_ = baseX_ - 430.0f;
        cameraY_ = baseY_ - 300.0f;

        if (mode_ != NetworkMode::Client) {
            spawn(baseX_ + 90, baseY_ + 120, false, 0);
            spawn(baseX_ - 55, baseY_ + 130, false, 0);
            spawn(baseX_ + 150, baseY_ + 70, false, 0);
            if (mode_ == NetworkMode::Host) {
                spawn(enemyBaseX_ + 90, enemyBaseY_ + 120, false, 1);
                spawn(enemyBaseX_ - 55, enemyBaseY_ + 130, false, 1);
                spawn(enemyBaseX_ + 150, enemyBaseY_ + 70, false, 1);
            }
        }
        initialUnits_ = static_cast<int>(world_.units().size());
        e.setWindowTitle(mode_ == NetworkMode::Offline ? "Click Command - RTS Clicker" : "Click Command - LAN RTS Clicker");
    }

    int localTeam() const
    {
        return mode_ == NetworkMode::Client ? 1 : 0;
    }

    int nextCost() const
    {
        const int baseline = mode_ == NetworkMode::Offline ? 3 : 6;
        const int bought = std::max(0, static_cast<int>(world_.units().size()) - baseline);
        return static_cast<int>(std::ceil(static_cast<double>(baseCost_) * std::pow(costGrowth_, bought)));
    }

    void spawn(float x, float y, bool selected, int team)
    {
        const auto id = world_.spawnUnit({x, y}, team, 98.0f);
        world_.setAutonomous(id, true, 150.0f);
        if (selected) {
            world_.clearSelection();
            if (auto* u = world_.find(id)) u->selected = true;
        }
    }

    void buyUnitHostAuthoritative(int team)
    {
        const int cost = nextCost();
        if (resources_ < cost) return;
        resources_ -= cost;
        const int n = static_cast<int>(world_.units().size());
        const float a = static_cast<float>(n) * 0.86f;
        const float bx = team == 0 ? baseX_ : enemyBaseX_;
        const float by = team == 0 ? baseY_ : enemyBaseY_;
        spawn(bx + std::cos(a) * 150.0f, by + std::sin(a) * 120.0f, team == localTeam(), team);
    }

    void requestBuy()
    {
        if (mode_ == NetworkMode::Client) {
            sendText("B");
        } else {
            buyUnitHostAuthoritative(localTeam());
        }
    }

    void updateGame(ra::Engine& e, float dt)
    {
        if (e.keyPressed(ra::Key::Escape)) {
            if (mode_ != NetworkMode::Offline) socket_.close();
            connected_ = false;
            mode_ = NetworkMode::Offline;
            screen_ = Screen::Menu;
            e.setWindowTitle("Click Command");
            return;
        }

        pumpNetwork();

        const float panelX = hudX(e);
        const float mx = e.mouseX();
        const float my = e.mouseY();
        const float wheel = e.mouseWheelY();
        if (wheel != 0) zoom_ = clampf(zoom_ + wheel * 0.12f, 0.65f, 2.6f);
        if (e.keyDown(ra::Key::Q)) zoom_ = clampf(zoom_ - dt, 0.65f, 2.6f);
        if (e.keyDown(ra::Key::E)) zoom_ = clampf(zoom_ + dt, 0.65f, 2.6f);

        float dx = 0, dy = 0;
        if (e.keyDown(ra::Key::A) || e.keyDown(ra::Key::Left)) dx -= 1;
        if (e.keyDown(ra::Key::D) || e.keyDown(ra::Key::Right)) dx += 1;
        if (e.keyDown(ra::Key::W) || e.keyDown(ra::Key::Up)) dy -= 1;
        if (e.keyDown(ra::Key::S) || e.keyDown(ra::Key::Down)) dy += 1;
        if (mx < 10) dx -= 1;
        if (mx < panelX && mx > panelX - 10) dx += 1;
        if (my < 10) dy -= 1;
        if (my > static_cast<float>(e.windowHeight()) - 10) dy += 1;
        const float step = 560.0f / zoom_ * dt;
        cameraX_ += dx * step;
        cameraY_ += dy * step;
        clampCamera(e);

        const float bx = panelX + 18, by = 150, bw = static_cast<float>(e.windowWidth()) - panelX - 36;
        if (e.mousePressed(ra::MouseButton::Left)) {
            if (inside(mx, my, bx, by, bx + bw, by + 72)) requestBuy();
            else if (mx < panelX) { drag_ = true; sx_ = ex_ = mx; sy_ = ey_ = my; }
        }
        if (drag_ && e.mouseDown(ra::MouseButton::Left)) { ex_ = mx; ey_ = my; }
        if (drag_ && e.mouseReleased(ra::MouseButton::Left)) {
            ex_ = mx; ey_ = my;
            const float ddx = ex_ - sx_, ddy = ey_ - sy_;
            if (ddx * ddx + ddy * ddy <= 36) {
                if (mode_ == NetworkMode::Client) sendText("C");
                else ++resources_;
                effects_.push_back({mx, my, 0.7f});
                world_.selectPoint({screenToWorldX(mx), screenToWorldY(my)}, 24.0f / zoom_, false);
            } else {
                world_.selectRect({screenToWorldX(std::min(sx_, ex_)), screenToWorldY(std::min(sy_, ey_)),
                                   screenToWorldX(std::max(sx_, ex_)), screenToWorldY(std::max(sy_, ey_))}, false);
            }
            filterSelectionToLocalTeam();
            drag_ = false;
        }

        if (e.mousePressed(ra::MouseButton::Right) && mx < panelX) {
            const float wx = screenToWorldX(mx), wy = screenToWorldY(my);
            if (mode_ == NetworkMode::Client) sendMoveRequest(wx, wy);
            else world_.commandMove({wx, wy}, 30.0f);
        }

        if (mode_ != NetworkMode::Client) {
            world_.update(dt);
            if (mode_ == NetworkMode::Host && connected_) {
                snapshotTimer_ -= dt;
                if (snapshotTimer_ <= 0.0f) {
                    snapshotTimer_ = 0.05f;
                    sendSnapshot();
                }
            }
        }

        for (auto& fx : effects_) { fx.life -= dt; fx.y -= 26.0f * dt; }
        effects_.erase(std::remove_if(effects_.begin(), effects_.end(), [](const ClickFx& fx) { return fx.life <= 0; }), effects_.end());
    }

    void filterSelectionToLocalTeam()
    {
        const int team = localTeam();
        for (auto& u : world_.units()) if (u.selected && u.team != team) u.selected = false;
    }

    void sendText(const std::string& text)
    {
        if (!connected_ || !peer_ || !socket_.isOpen()) return;
        socket_.send(peer_, text.data(), text.size());
    }

    void sendMoveRequest(float x, float y)
    {
        std::ostringstream out;
        out << "M " << std::fixed << std::setprecision(2) << x << ' ' << y;
        for (const auto& u : world_.units()) if (u.selected && u.team == 1) out << ' ' << u.id;
        sendText(out.str());
    }

    void pumpNetwork()
    {
        if (mode_ == NetworkMode::Offline || !socket_.isOpen()) return;
        char buffer[65507];
        ra::net::Endpoint from;
        int received = 0;
        while ((received = socket_.receive(buffer, sizeof(buffer), from)) > 0) {
            const std::string msg(buffer, buffer + received);
            if (mode_ == NetworkMode::Host) handleHostPacket(msg, from);
            else handleClientPacket(msg, from);
        }
    }

    void handleHostPacket(const std::string& msg, const ra::net::Endpoint& from)
    {
        if (!peer_ || from.address != peer_.address || from.port != peer_.port) return;
        if (msg == "C") {
            ++resources_;
            return;
        }
        if (msg == "B") {
            buyUnitHostAuthoritative(1);
            return;
        }
        if (msg.size() > 2 && msg[0] == 'M' && msg[1] == ' ') {
            std::istringstream in(msg.substr(2));
            float x = 0, y = 0;
            in >> x >> y;
            std::vector<ra::rts::UnitId> ids;
            ra::rts::UnitId id = 0;
            while (in >> id) {
                const auto* u = world_.find(id);
                if (u && u->team == 1) ids.push_back(id);
            }
            world_.commandMoveUnits(ids, {x, y}, 30.0f);
        }
    }

    void sendSnapshot()
    {
        if (!connected_) return;
        std::ostringstream out;
        out << "S " << resources_ << ' ' << world_.units().size();
        out << std::fixed << std::setprecision(2);
        for (const auto& u : world_.units()) {
            out << ' ' << u.id
                << ' ' << u.position.x << ' ' << u.position.y
                << ' ' << u.destination.x << ' ' << u.destination.y
                << ' ' << u.home.x << ' ' << u.home.y
                << ' ' << u.speed << ' ' << u.team
                << ' ' << (u.autonomous ? 1 : 0)
                << ' ' << static_cast<int>(u.order)
                << ' ' << u.aiStep << ' ' << u.thinkCooldown
                << ' ' << u.patrolRadius << ' ' << u.health;
        }
        sendText(out.str());
    }

    void handleClientPacket(const std::string& msg, const ra::net::Endpoint& from)
    {
        if (!peer_ || from.address != peer_.address || from.port != peer_.port) return;
        if (msg.size() < 2 || msg[0] != 'S' || msg[1] != ' ') return;

        std::vector<ra::rts::UnitId> selected;
        for (const auto& u : world_.units()) if (u.selected) selected.push_back(u.id);

        std::istringstream in(msg.substr(2));
        int count = 0;
        if (!(in >> resources_ >> count) || count < 0 || count > 2048) return;

        std::vector<ra::rts::Unit> snapshot;
        snapshot.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) {
            ra::rts::Unit u;
            int autonomous = 0, order = 0;
            if (!(in >> u.id
                     >> u.position.x >> u.position.y
                     >> u.destination.x >> u.destination.y
                     >> u.home.x >> u.home.y
                     >> u.speed >> u.team
                     >> autonomous >> order
                     >> u.aiStep >> u.thinkCooldown
                     >> u.patrolRadius >> u.health)) return;
            u.autonomous = autonomous != 0;
            if (order < 0 || order > 2) order = 0;
            u.order = static_cast<ra::rts::UnitOrder>(order);
            u.maxHealth = 100.0f;
            u.radius = 12.0f;
            u.selected = containsId(selected, u.id) && u.team == 1;
            snapshot.push_back(u);
        }
        world_.replaceUnits(snapshot);
    }

    void renderSplash(ra::Engine& e)
    {
        const float w = static_cast<float>(e.windowWidth()), h = static_cast<float>(e.windowHeight());
        e.clear({8, 11, 9, 255});
        for (int y = 0; y < 6; ++y) for (int x = 0; x < 16; ++x)
            e.drawTextureRegion(plains_, tile16(4, 4), {w * 0.5f - 256 + x * 32.0f, h * 0.18f + y * 32.0f, 32, 32});
        e.drawText(w * 0.5f - 58, h * 0.46f, "CLICK COMMAND", {246, 238, 203, 255});
        e.drawText(w * 0.5f - 82, h * 0.50f, "MYSTIC WOODS + BIT BONANZA", {200, 220, 195, 255});
        e.fillRect({w * 0.5f - 180, h * 0.58f, 360, 12}, {30, 39, 33, 255});
        e.fillRect({w * 0.5f - 180, h * 0.58f, 360 * clampf(splash_ / 0.8f, 0, 1), 12}, {83, 160, 59, 255});
        if (!assetError_.empty()) e.drawText(w * 0.5f - 122, h * 0.65f, "ASSET LOAD ERROR - SEE RA-Framework-error.txt", {255, 96, 96, 255});
    }

    void renderMenu(ra::Engine& e)
    {
        const float w = static_cast<float>(e.windowWidth()), h = static_cast<float>(e.windowHeight());
        e.clear({9, 13, 11, 255});
        for (int y = 0; y < 10; ++y) for (int x = 0; x < 22; ++x) {
            const int tx = (x + y) % 11 == 0 ? 5 : 4;
            const int ty = tx == 5 ? 5 : 4;
            e.drawTextureRegion(plains_, tile16(tx, ty), {w * 0.5f - 352 + x * 32.0f, h * 0.08f + y * 32.0f, 32, 32});
        }
        e.fillRect({w * 0.5f - 290, h * 0.34f, 580, 500}, {12, 18, 14, 230});
        e.drawText(w * 0.5f - 58, h * 0.39f, "CLICK COMMAND", {245, 238, 202, 255});
        e.drawText(w * 0.5f - 96, h * 0.43f, "RTS + CLICKER ECONOMY", {188, 211, 187, 255});

        const float bx = w * 0.5f - 210;
        const float y1 = h * 0.54f, y2 = y1 + 78, y3 = y2 + 78;
        menuButton(e, bx, y1, "SINGLE PLAYER");
        menuButton(e, bx, y2, "HOST LAN GAME");
        menuButton(e, bx, y3, "JOIN LAN GAME");
        e.drawText(w * 0.5f - 104, y3 + 92, "ENTER Single Player    ESC Exit", {150, 170, 154, 255});
    }

    void menuButton(ra::Engine& e, float x, float y, const char* label)
    {
        const bool hot = inside(e.mouseX(), e.mouseY(), x, y, x + 420, y + 62);
        e.fillRect({x, y, 420, 62}, hot ? ra::Color{62, 120, 68, 255} : ra::Color{43, 88, 50, 255});
        e.drawRect({x, y, 420, 62}, {126, 164, 130, 255});
        e.drawText(x + 158, y + 27, label, {247, 244, 220, 255});
    }

    void renderLobby(ra::Engine& e)
    {
        const float w = static_cast<float>(e.windowWidth()), h = static_cast<float>(e.windowHeight());
        e.clear({9, 13, 11, 255});
        e.fillRect({w * 0.5f - 340, h * 0.28f, 680, 330}, {12, 18, 14, 255});
        e.drawRect({w * 0.5f - 340, h * 0.28f, 680, 330}, {126, 164, 130, 255});
        e.drawText(w * 0.5f - 42, h * 0.34f, mode_ == NetworkMode::Host ? "HOST LAN" : "JOIN LAN", {245, 238, 202, 255});
        e.drawText(w * 0.5f - 150, h * 0.43f, lobbyStatus_, {188, 211, 187, 255});
        if (mode_ == NetworkMode::Host) e.drawText(w * 0.5f - 122, h * 0.49f, "UDP PORT 46000 - AUTO DISCOVERY", {150, 170, 154, 255});
        else e.drawText(w * 0.5f - 132, h * 0.49f, "BROADCAST SEARCHING LOCAL NETWORK", {150, 170, 154, 255});
        e.drawText(w * 0.5f - 56, h * 0.57f, "ESC Cancel", {150, 170, 154, 255});
    }

    void renderGame(ra::Engine& e)
    {
        const float vw = hudX(e), vh = static_cast<float>(e.windowHeight());
        e.clear({5, 7, 6, 255});
        drawTerrain(e, vw, vh);
        drawBase(e, baseX_, baseY_, 0);
        if (mode_ != NetworkMode::Offline) drawBase(e, enemyBaseX_, enemyBaseY_, 1);
        drawUnits(e, vw, vh);
        if (drag_) {
            const float l = std::min(sx_, ex_), r = std::max(sx_, ex_), t = std::min(sy_, ey_), b = std::max(sy_, ey_);
            e.drawRect({l, t, r - l, b - t}, {96, 238, 104, 255});
        }
        for (const auto& fx : effects_) e.drawText(fx.x - 5, fx.y, "+1", {255, 218, 65, 255});
        drawHud(e, vw, vh);
    }

    void drawTerrain(ra::Engine& e, float viewW, float viewH)
    {
        const int firstX = std::max(0, static_cast<int>(std::floor(cameraX_ / Tile)) - 1);
        const int firstY = std::max(0, static_cast<int>(std::floor(cameraY_ / Tile)) - 1);
        const int lastX = std::min(MapW - 1, firstX + static_cast<int>(viewW / (Tile * zoom_)) + 4);
        const int lastY = std::min(MapH - 1, firstY + static_cast<int>(viewH / (Tile * zoom_)) + 4);

        for (int y = firstY; y <= lastY; ++y) {
            for (int x = firstX; x <= lastX; ++x) {
                int sx = 4, sy = 4;
                const bool roadH = (y >= 30 && y <= 33 && x > 2 && x < MapW - 3);
                const bool roadV = (x >= 54 && x <= 57 && y > 4 && y < MapH - 4);
                if (roadH || roadV) { sx = 4; sy = 0; }
                drawTile(e, x, y, sx, sy);
                if (!roadH && !roadV) {
                    const int hash = (x * 37 + y * 71) % 257;
                    if (hash == 11) drawTile(e, x, y, 5, 5);
                    else if (hash == 29) drawTile(e, x, y, 5, 4);
                    else if (hash == 47) drawTile(e, x, y, 0, 3);
                }
            }
        }
    }

    void drawTile(ra::Engine& e, int mapX, int mapY, int sheetX, int sheetY)
    {
        const float s = Tile * zoom_;
        e.drawTextureRegion(plains_, tile16(sheetX, sheetY), {(mapX * Tile - cameraX_) * zoom_, (mapY * Tile - cameraY_) * zoom_, s, s});
    }

    void drawBase(ra::Engine& e, float wx, float wy, int team)
    {
        const int mx = static_cast<int>(wx / Tile) - 3;
        const int my = static_cast<int>(wy / Tile) - 2;
        for (int y = 0; y < 5; ++y) {
            for (int x = 0; x < 7; ++x) {
                int sx = 4, sy = 0;
                if (y == 0) { sx = (x == 0 ? 1 : (x == 6 ? 3 : 2)); sy = 0; }
                else if (y == 4) { sx = (x == 0 ? 1 : (x == 6 ? 3 : 2)); sy = 3; }
                else if (x == 0) { sx = 0; sy = 1; }
                else if (x == 6) { sx = 3; sy = 1; }
                drawTile(e, mx + x, my + y, sx, sy);
            }
        }

        const float px = (wx - cameraX_ - 2 * Tile) * zoom_;
        const float py = (wy - cameraY_ - Tile) * zoom_;
        const float cell = 36.0f * zoom_;
        for (int y = 0; y < 3; ++y) for (int x = 0; x < 4; ++x)
            e.drawTextureRegion(future_, cell10((x + 1 + team) % 4, (y + 2) % 9), {px + x * cell, py + y * cell, cell, cell});
        e.drawText(px + 10, py - 18, team == 0 ? "BASE P1" : "BASE P2", team == 0 ? ra::Color{120, 180, 255, 255} : ra::Color{255, 130, 110, 255});
    }

    void drawUnits(ra::Engine& e, float viewW, float viewH)
    {
        const float size = 30.0f * zoom_;
        int i = 0;
        for (const auto& u : world_.units()) {
            const float x = (u.position.x - cameraX_) * zoom_, y = (u.position.y - cameraY_) * zoom_;
            if (x < -size || y < -size || x > viewW + size || y > viewH + size) { ++i; continue; }
            if (u.selected) {
                const float tx = (u.destination.x - cameraX_) * zoom_, ty = (u.destination.y - cameraY_) * zoom_;
                e.drawLine(x, y, tx, ty, {83, 208, 95, 150});
                e.drawRect({x - size * 0.62f, y - size * 0.62f, size * 1.24f, size * 1.24f}, {92, 242, 106, 255});
            }
            const int sx = ((i + u.team * 4) % 6) * 2;
            const int sy = ((i + u.team * 3) % 9) * 3;
            e.drawTextureRegion(people_, cell10(sx, sy), {x - size * 0.5f, y - size * 0.5f, size, size});
            ++i;
        }
    }

    void drawHud(ra::Engine& e, float x, float h)
    {
        const float pw = static_cast<float>(e.windowWidth()) - x;
        e.fillRect({x, 0, pw, h}, {14, 19, 16, 255});
        e.drawText(x + 18, 18, "CLICK COMMAND", {245, 238, 203, 255});
        if (mode_ == NetworkMode::Offline) e.drawText(x + 18, 40, "SINGLE PLAYER", {160, 180, 164, 255});
        else e.drawText(x + 18, 40, mode_ == NetworkMode::Host ? "LAN: HOST / P1" : "LAN: CLIENT / P2", connected_ ? ra::Color{96, 220, 110, 255} : ra::Color{255, 96, 96, 255});
        e.drawText(x + 18, 70, "RESOURCES", {160, 180, 164, 255});
        e.drawText(x + 18, 94, std::to_string(resources_), {255, 216, 64, 255});
        e.drawText(x + 18, 120, "CLICK MAP: +1", {132, 153, 138, 255});

        const int cost = nextCost();
        const bool can = resources_ >= cost;
        const float bx = x + 18, by = 150, bw = pw - 36;
        e.fillRect({bx, by, bw, 72}, can ? ra::Color{43, 92, 51, 255} : ra::Color{44, 49, 45, 255});
        e.drawRect({bx, by, bw, 72}, {105, 133, 111, 255});
        e.drawTextureRegion(people_, cell10(localTeam() == 0 ? 0 : 8, localTeam() == 0 ? 0 : 9), {bx + 12, by + 12, 44, 44});
        e.drawText(bx + 70, by + 18, "BASIC UNIT", {243, 242, 218, 255});
        e.drawText(bx + 70, by + 44, ("COST: " + std::to_string(cost)).c_str(), can ? ra::Color{255, 216, 64, 255} : ra::Color{132, 138, 132, 255});

        e.drawText(x + 18, 248, ("UNITS: " + std::to_string(world_.units().size())).c_str(), {190, 203, 193, 255});
        e.drawText(x + 18, 272, ("SELECTED: " + std::to_string(world_.selectedCount())).c_str(), {190, 203, 193, 255});
        e.drawText(x + 18, 296, "UNIT AI: HOST AUTHORITATIVE", {96, 220, 110, 255});
        e.drawText(x + 18, 320, "COST GROWTH: 18%", {151, 168, 156, 255});
        e.drawText(x + 18, 368, "LMB       +1 resource", {160, 177, 165, 255});
        e.drawText(x + 18, 392, "DRAG LMB  select", {160, 177, 165, 255});
        e.drawText(x + 18, 416, "RMB       move", {160, 177, 165, 255});
        e.drawText(x + 18, 440, "WASD      camera", {160, 177, 165, 255});
        e.drawText(x + 18, 464, "WHEEL     zoom", {160, 177, 165, 255});
        e.drawTextureRegion(ui_, {0, 130, 200, 10}, {x + 18, 505, pw - 36, 18});

        const float mx = x + 18, my = h - 180, mw = pw - 36, mh = 120;
        e.drawText(mx, my - 20, "MINIMAP", {220, 226, 212, 255});
        e.fillRect({mx, my, mw, mh}, {45, 86, 47, 255});
        for (const auto& u : world_.units()) {
            const ra::Color c = u.team == 0 ? ra::Color{110, 180, 255, 255} : ra::Color{255, 120, 100, 255};
            e.fillRect({mx + u.position.x / worldW_ * mw - 1, my + u.position.y / worldH_ * mh - 1, 3, 3}, c);
        }
        e.drawRect({mx + cameraX_ / worldW_ * mw, my + cameraY_ / worldH_ * mh,
                    std::max(8.0f, (hudX(e) / zoom_) / worldW_ * mw),
                    std::max(8.0f, (static_cast<float>(e.windowHeight()) / zoom_) / worldH_ * mh)}, {242, 242, 218, 255});
    }

    float hudX(const ra::Engine& e) const { return static_cast<float>(e.windowWidth()) - 320.0f; }
    float screenToWorldX(float x) const { return cameraX_ + x / zoom_; }
    float screenToWorldY(float y) const { return cameraY_ + y / zoom_; }

    void clampCamera(const ra::Engine& e)
    {
        const float vw = hudX(e) / zoom_, vh = static_cast<float>(e.windowHeight()) / zoom_;
        cameraX_ = clampf(cameraX_, 0, std::max(0.0f, worldW_ - vw));
        cameraY_ = clampf(cameraY_, 0, std::max(0.0f, worldH_ - vh));
    }

    Screen screen_{Screen::Splash};
    NetworkMode mode_{NetworkMode::Offline};
    float splash_{0};
    int resources_{0};
    int initialUnits_{0};
    ra::rts::World world_{{0, 0, MapW * Tile, MapH * Tile}};
    std::vector<ClickFx> effects_;
    float cameraX_{0}, cameraY_{0}, zoom_{1};
    bool drag_{false};
    float sx_{0}, sy_{0}, ex_{0}, ey_{0};
    const float worldW_ = MapW * Tile, worldH_ = MapH * Tile;
    const float baseX_ = 26 * Tile, baseY_ = 20 * Tile;
    const float enemyBaseX_ = 86 * Tile, enemyBaseY_ = 54 * Tile;
    const int baseCost_ = 10;
    const double costGrowth_ = 1.18;
    ra::Texture plains_, people_, ui_, future_;
    std::string assetError_;

    ra::net::DatagramSocket socket_;
    ra::net::Endpoint peer_{};
    bool connected_{false};
    float discoverTimer_{0.0f};
    float snapshotTimer_{0.0f};
    std::string lobbyStatus_;
};

int main()
{
    ra::EngineConfig c;
    c.title = "Click Command";
    c.width = 1440;
    c.height = 900;
    c.resizable = true;
    c.vsync = true;

    try {
        ra::Engine e(c);
        ClickerRTS game;
        return e.run(game);
    } catch (const std::exception& ex) {
        std::ofstream out("RA-Framework-error.txt", std::ios::trunc);
        out << "Fatal startup/runtime error:\n" << ex.what() << "\n";
        std::fprintf(stderr, "Fatal error: %s\n", ex.what());
#ifdef _WIN32
        MessageBoxA(nullptr, ex.what(), "Click Command - Fatal Error", MB_OK | MB_ICONERROR);
#endif
        return 1;
    }
}
