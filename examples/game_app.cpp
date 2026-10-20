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
ra::Rect frame48(int x, int y) { return {x * 48.0f, y * 48.0f, 48.0f, 48.0f}; }
ra::Rect frame32(int x, int y) { return {x * 32.0f, y * 32.0f, 32.0f, 32.0f}; }

constexpr std::uint16_t LanPort = 46000;
constexpr const char* DiscoverPacket = "CC_DISCOVER_2";
constexpr const char* WelcomePacket = "CC_WELCOME_2";

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

        // Real Mystic Woods files supplied for this project.
        plains_ = safeLoad(e, "assets/mystic/plains.png");
        grass_ = safeLoad(e, "assets/mystic/tilesets/grass.png");
        fences_ = safeLoad(e, "assets/mystic/tilesets/fences.png");
        walls_ = safeLoad(e, "assets/mystic/tilesets/walls.png");
        player_ = safeLoad(e, "assets/mystic/characters/player.png");
        slime_ = safeLoad(e, "assets/mystic/characters/slime.png");

        if (!assetError_.empty()) {
            std::ofstream out("RA-Framework-error.txt", std::ios::trunc);
            out << assetError_;
        }
    }

    void onStop(ra::Engine&) override { socket_.close(); }

    void onUpdate(ra::Engine& e, float dt) override
    {
        if (screen_ == Screen::Splash) {
            splash_ += dt;
            if (splash_ >= 0.65f || e.keyPressed(ra::Key::Enter) || e.mousePressed(ra::MouseButton::Left)) {
                screen_ = Screen::Menu;
                e.setWindowTitle("Click Command");
            }
            return;
        }
        if (screen_ == Screen::Menu) { updateMenu(e); return; }
        if (screen_ == Screen::Lobby) { updateLobby(e, dt); return; }
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
        try { return e.loadTexture(path); }
        catch (const std::exception& ex) {
            assetError_ += std::string(path) + ": " + ex.what() + "\n";
            return {};
        }
    }

    void updateMenu(ra::Engine& e)
    {
        if (e.keyPressed(ra::Key::Escape)) { e.quit(); return; }

        const float w = static_cast<float>(e.windowWidth());
        const float h = static_cast<float>(e.windowHeight());
        const float bx = w * 0.5f - 210.0f;
        const float y1 = h * 0.54f;
        const float y2 = y1 + 78.0f;
        const float y3 = y2 + 78.0f;
        const float mx = e.mouseX(), my = e.mouseY();

        if (e.keyPressed(ra::Key::Enter) ||
            (e.mousePressed(ra::MouseButton::Left) && inside(mx, my, bx, y1, bx + 420, y1 + 62))) {
            mode_ = NetworkMode::Offline;
            startGame(e);
        } else if (e.mousePressed(ra::MouseButton::Left) && inside(mx, my, bx, y2, bx + 420, y2 + 62)) {
            startHost(e);
        } else if (e.mousePressed(ra::MouseButton::Left) && inside(mx, my, bx, y3, bx + 420, y3 + 62)) {
            startJoin(e);
        }
    }

    void startHost(ra::Engine& e)
    {
        socket_.close();
        mode_ = NetworkMode::Host;
        connected_ = false;
        peer_ = {};
        lobbyStatus_ = socket_.open(LanPort, true) ? "WAITING FOR LAN PLAYER..." : "HOST ERROR: " + socket_.lastError();
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
        lobbyStatus_ = socket_.open(0, true) ? "SEARCHING LAN HOST..." : "JOIN ERROR: " + socket_.lastError();
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
                discoverTimer_ = 0.55f;
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
                startGame(e);
                return;
            }
            if (mode_ == NetworkMode::Client && msg == WelcomePacket) {
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

        e.setWindowTitle(mode_ == NetworkMode::Offline ? "Click Command - Mystic Woods RTS" : "Click Command - Mystic Woods LAN");
    }

    int localTeam() const { return mode_ == NetworkMode::Client ? 1 : 0; }

    int teamUnitCount(int team) const
    {
        int count = 0;
        for (const auto& u : world_.units()) if (u.team == team) ++count;
        return count;
    }

    int nextCost() const
    {
        const int bought = std::max(0, teamUnitCount(localTeam()) - 3);
        return static_cast<int>(std::ceil(static_cast<double>(baseCost_) * std::pow(costGrowth_, bought)));
    }

    void spawn(float x, float y, bool selected, int team)
    {
        const auto id = world_.spawnUnit({x, y}, team, team == 0 ? 105.0f : 88.0f);
        world_.setAutonomous(id, true, 145.0f);
        if (selected) {
            world_.clearSelection();
            if (auto* u = world_.find(id)) u->selected = true;
        }
    }

    void buyUnitHostAuthoritative(int team)
    {
        const int bought = std::max(0, teamUnitCount(team) - 3);
        const int cost = static_cast<int>(std::ceil(static_cast<double>(baseCost_) * std::pow(costGrowth_, bought)));
        if (resources_ < cost) return;
        resources_ -= cost;
        const int n = teamUnitCount(team);
        const float a = static_cast<float>(n) * 0.92f;
        const float bx = team == 0 ? baseX_ : enemyBaseX_;
        const float by = team == 0 ? baseY_ : enemyBaseY_;
        spawn(bx + std::cos(a) * 150.0f, by + std::sin(a) * 120.0f, team == localTeam(), team);
    }

    void requestBuy()
    {
        if (mode_ == NetworkMode::Client) sendText("B");
        else buyUnitHostAuthoritative(localTeam());
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
        const float mx = e.mouseX(), my = e.mouseY();
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
                if (mode_ == NetworkMode::Client) sendText("C"); else ++resources_;
                effects_.push_back({mx, my, 0.7f});
                world_.selectPoint({screenToWorldX(mx), screenToWorldY(my)}, 28.0f / zoom_, false);
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
            else world_.commandMove({wx, wy}, 36.0f);
        }

        if (mode_ != NetworkMode::Client) {
            world_.update(dt);
            if (mode_ == NetworkMode::Host && connected_) {
                snapshotTimer_ -= dt;
                if (snapshotTimer_ <= 0.0f) { snapshotTimer_ = 0.05f; sendSnapshot(); }
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
        if (connected_ && peer_ && socket_.isOpen()) socket_.send(peer_, text.data(), text.size());
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
        if (msg == "C") { ++resources_; return; }
        if (msg == "B") { buyUnitHostAuthoritative(1); return; }
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
            world_.commandMoveUnits(ids, {x, y}, 36.0f);
        }
    }

    void sendSnapshot()
    {
        if (!connected_) return;
        std::ostringstream out;
        out << "S " << resources_ << ' ' << world_.units().size() << std::fixed << std::setprecision(2);
        for (const auto& u : world_.units()) {
            out << ' ' << u.id << ' ' << u.position.x << ' ' << u.position.y
                << ' ' << u.destination.x << ' ' << u.destination.y
                << ' ' << u.home.x << ' ' << u.home.y << ' ' << u.speed << ' ' << u.team
                << ' ' << (u.autonomous ? 1 : 0) << ' ' << static_cast<int>(u.order)
                << ' ' << u.aiStep << ' ' << u.thinkCooldown << ' ' << u.patrolRadius << ' ' << u.health;
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
            if (!(in >> u.id >> u.position.x >> u.position.y >> u.destination.x >> u.destination.y
                     >> u.home.x >> u.home.y >> u.speed >> u.team >> autonomous >> order
                     >> u.aiStep >> u.thinkCooldown >> u.patrolRadius >> u.health)) return;
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
        e.clear({7, 11, 8, 255});
        for (int y = 0; y < 8; ++y) for (int x = 0; x < 18; ++x)
            e.drawTexture(grass_, {w * 0.5f - 288 + x * 32.0f, h * 0.12f + y * 32.0f, 32, 32});
        e.drawTextureRegion(player_, frame48(0, 0), {w * 0.5f - 48, h * 0.30f, 96, 96});
        e.drawText(w * 0.5f - 58, h * 0.48f, "CLICK COMMAND", {246, 238, 203, 255});
        e.drawText(w * 0.5f - 66, h * 0.52f, "MYSTIC WOODS RTS", {200, 220, 195, 255});
        if (!assetError_.empty()) e.drawText(w * 0.5f - 122, h * 0.62f, "ASSET LOAD ERROR - SEE RA-Framework-error.txt", {255, 96, 96, 255});
    }

    void renderMenu(ra::Engine& e)
    {
        const float w = static_cast<float>(e.windowWidth()), h = static_cast<float>(e.windowHeight());
        e.clear({8, 13, 9, 255});
        for (int y = 0; y < 12; ++y) for (int x = 0; x < 24; ++x)
            e.drawTexture(grass_, {w * 0.5f - 384 + x * 32.0f, h * 0.03f + y * 32.0f, 32, 32});
        e.fillRect({w * 0.5f - 290, h * 0.31f, 580, 530}, {10, 17, 12, 230});
        e.drawTextureRegion(player_, frame48(0, 0), {w * 0.5f - 150, h * 0.35f, 72, 72});
        e.drawTextureRegion(slime_, frame32(0, 0), {w * 0.5f + 82, h * 0.37f, 58, 58});
        e.drawText(w * 0.5f - 58, h * 0.40f, "CLICK COMMAND", {245, 238, 202, 255});
        e.drawText(w * 0.5f - 92, h * 0.44f, "MYSTIC WOODS RTS + LAN", {188, 211, 187, 255});

        const float bx = w * 0.5f - 210;
        const float y1 = h * 0.54f, y2 = y1 + 78, y3 = y2 + 78;
        menuButton(e, bx, y1, "SINGLE PLAYER");
        menuButton(e, bx, y2, "HOST LAN GAME");
        menuButton(e, bx, y3, "JOIN LAN GAME");
    }

    void menuButton(ra::Engine& e, float x, float y, const char* label)
    {
        const bool hot = inside(e.mouseX(), e.mouseY(), x, y, x + 420, y + 62);
        e.fillRect({x, y, 420, 62}, hot ? ra::Color{55, 105, 57, 255} : ra::Color{34, 71, 38, 255});
        e.drawRect({x, y, 420, 62}, {132, 174, 126, 255});
        e.drawText(x + 150, y + 27, label, {247, 244, 220, 255});
    }

    void renderLobby(ra::Engine& e)
    {
        const float w = static_cast<float>(e.windowWidth()), h = static_cast<float>(e.windowHeight());
        e.clear({8, 13, 9, 255});
        e.fillRect({w * 0.5f - 340, h * 0.28f, 680, 330}, {10, 17, 12, 255});
        e.drawRect({w * 0.5f - 340, h * 0.28f, 680, 330}, {132, 174, 126, 255});
        e.drawText(w * 0.5f - 48, h * 0.34f, mode_ == NetworkMode::Host ? "HOST LAN" : "JOIN LAN", {245, 238, 202, 255});
        e.drawText(w * 0.5f - 150, h * 0.43f, lobbyStatus_, {188, 211, 187, 255});
        e.drawText(w * 0.5f - 145, h * 0.49f, "UDP 46000 - AUTOMATIC LAN DISCOVERY", {150, 170, 154, 255});
        e.drawText(w * 0.5f - 48, h * 0.57f, "ESC CANCEL", {150, 170, 154, 255});
    }

    void renderGame(ra::Engine& e)
    {
        const float vw = hudX(e), vh = static_cast<float>(e.windowHeight());
        e.clear({4, 7, 5, 255});
        drawTerrain(e, vw, vh);
        drawBase(e, 0, baseX_, baseY_);
        if (mode_ != NetworkMode::Offline) drawBase(e, 1, enemyBaseX_, enemyBaseY_);
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
                const float s = Tile * zoom_;
                const float dx = (x * Tile - cameraX_) * zoom_;
                const float dy = (y * Tile - cameraY_) * zoom_;
                const bool roadH = y >= 30 && y <= 32 && x > 3 && x < MapW - 4;
                const bool roadV = x >= 54 && x <= 56 && y > 3 && y < MapH - 4;

                if (roadH || roadV) e.drawTextureRegion(plains_, tile16(4, 0), {dx, dy, s, s});
                else e.drawTextureRegion(plains_, tile16(4, 4), {dx, dy, s, s});

                // Non-premium Mystic Woods decoration only.
                const int hash = (x * 37 + y * 71) % 223;
                if (!roadH && !roadV && hash == 17) e.drawTexture(grass_, {dx, dy, s, s});
                if (!roadH && !roadV && hash == 61) e.drawTextureRegion(plains_, tile16(5, 5), {dx, dy, s, s});
            }
        }
    }

    void drawBase(ra::Engine& e, int team, float worldX, float worldY)
    {
        const float x0 = (worldX - cameraX_ - 96.0f) * zoom_;
        const float y0 = (worldY - cameraY_ - 80.0f) * zoom_;
        const float cell = 32.0f * zoom_;

        // Building and palisade are composed from the actual Mystic Woods wall/fence sheets.
        for (int y = 0; y < 5; ++y) {
            for (int x = 0; x < 6; ++x) {
                const int sx = (x == 0 ? 0 : (x == 5 ? 2 : 1));
                const int sy = (y == 0 ? 0 : (y == 4 ? 2 : 1));
                e.drawTextureRegion(walls_, tile16(sx, sy), {x0 + x * cell, y0 + y * cell, cell, cell});
            }
        }
        for (int x = -1; x <= 6; ++x) {
            e.drawTextureRegion(fences_, tile16(1, 1), {x0 + x * cell, y0 - cell, cell, cell});
            e.drawTextureRegion(fences_, tile16(1, 1), {x0 + x * cell, y0 + 5 * cell, cell, cell});
        }
        for (int y = 0; y < 5; ++y) {
            e.drawTextureRegion(fences_, tile16(0, 1), {x0 - cell, y0 + y * cell, cell, cell});
            e.drawTextureRegion(fences_, tile16(0, 1), {x0 + 6 * cell, y0 + y * cell, cell, cell});
        }
        e.drawText(x0 + 58.0f * zoom_, y0 - 18.0f, team == 0 ? "HUMAN CAMP" : "SLIME CAMP", team == 0 ? ra::Color{210, 235, 255, 255} : ra::Color{155, 210, 255, 255});
    }

    void drawUnits(ra::Engine& e, float viewW, float viewH)
    {
        int i = 0;
        for (const auto& u : world_.units()) {
            const float x = (u.position.x - cameraX_) * zoom_;
            const float y = (u.position.y - cameraY_) * zoom_;
            const float size = (u.team == 0 ? 52.0f : 42.0f) * zoom_;
            if (x < -size || y < -size || x > viewW + size || y > viewH + size) { ++i; continue; }

            if (u.selected) {
                const float tx = (u.destination.x - cameraX_) * zoom_;
                const float ty = (u.destination.y - cameraY_) * zoom_;
                e.drawLine(x, y, tx, ty, {110, 245, 120, 150});
                e.drawRect({x - size * 0.52f, y - size * 0.52f, size * 1.04f, size * 1.04f}, {96, 238, 104, 255});
            }

            if (u.team == 0) {
                const int frame = (i + static_cast<int>(u.aiStep)) % 6;
                e.drawTextureRegion(player_, frame48(frame, 0), {x - size * 0.5f, y - size * 0.7f, size, size});
            } else {
                const int frame = (i + static_cast<int>(u.aiStep)) % 4;
                e.drawTextureRegion(slime_, frame32(frame, 0), {x - size * 0.5f, y - size * 0.5f, size, size});
            }
            ++i;
        }
    }

    void drawHud(ra::Engine& e, float x, float h)
    {
        const float pw = static_cast<float>(e.windowWidth()) - x;
        e.fillRect({x, 0, pw, h}, {12, 18, 14, 255});
        e.drawText(x + 18, 18, "CLICK COMMAND", {245, 238, 203, 255});
        e.drawText(x + 18, 42, mode_ == NetworkMode::Offline ? "SINGLE PLAYER" : (mode_ == NetworkMode::Host ? "LAN HOST / HUMAN" : "LAN CLIENT / SLIME"), {155, 190, 160, 255});
        e.drawText(x + 18, 76, "RESOURCES", {160, 180, 164, 255});
        e.drawText(x + 18, 100, std::to_string(resources_), {255, 216, 64, 255});
        e.drawText(x + 18, 124, "CLICK MAP: +1", {132, 153, 138, 255});

        const int cost = nextCost();
        const bool can = resources_ >= cost;
        const float bx = x + 18, by = 158, bw = pw - 36;
        e.fillRect({bx, by, bw, 78}, can ? ra::Color{43, 92, 51, 255} : ra::Color{40, 45, 41, 255});
        e.drawRect({bx, by, bw, 78}, {105, 133, 111, 255});
        if (localTeam() == 0) e.drawTextureRegion(player_, frame48(0, 0), {bx + 8, by + 7, 64, 64});
        else e.drawTextureRegion(slime_, frame32(0, 0), {bx + 15, by + 14, 50, 50});
        e.drawText(bx + 82, by + 20, localTeam() == 0 ? "HUMAN UNIT" : "SLIME UNIT", {243, 242, 218, 255});
        e.drawText(bx + 82, by + 48, ("COST: " + std::to_string(cost)).c_str(), can ? ra::Color{255, 216, 64, 255} : ra::Color{132, 138, 132, 255});

        e.drawText(x + 18, 262, ("YOUR UNITS: " + std::to_string(teamUnitCount(localTeam()))).c_str(), {190, 203, 193, 255});
        e.drawText(x + 18, 286, ("SELECTED: " + std::to_string(world_.selectedCount())).c_str(), {190, 203, 193, 255});
        e.drawText(x + 18, 310, "UNIT AI: ACTIVE", {96, 220, 110, 255});
        if (mode_ != NetworkMode::Offline) e.drawText(x + 18, 334, connected_ ? "LAN: CONNECTED" : "LAN: WAITING", connected_ ? ra::Color{96, 220, 110, 255} : ra::Color{255, 110, 90, 255});

        e.drawText(x + 18, 382, "LMB       +1 resource", {160, 177, 165, 255});
        e.drawText(x + 18, 406, "DRAG LMB  select", {160, 177, 165, 255});
        e.drawText(x + 18, 430, "RMB       move", {160, 177, 165, 255});
        e.drawText(x + 18, 454, "WASD      camera", {160, 177, 165, 255});
        e.drawText(x + 18, 478, "WHEEL     zoom", {160, 177, 165, 255});

        const float mx = x + 18, my = h - 180, mw = pw - 36, mh = 120;
        e.drawText(mx, my - 20, "MINIMAP", {220, 226, 212, 255});
        e.fillRect({mx, my, mw, mh}, {41, 78, 42, 255});
        for (const auto& u : world_.units()) {
            const ra::Color c = u.team == 0 ? ra::Color{230, 230, 210, 255} : ra::Color{110, 175, 255, 255};
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

    ra::Texture plains_, grass_, fences_, walls_, player_, slime_;
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
