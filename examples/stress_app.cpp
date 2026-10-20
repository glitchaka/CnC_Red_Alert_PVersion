#include <ra/Engine.h>
#include <ra/RTS.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

struct ClickFx {
    float x{0.0f};
    float y{0.0f};
    float life{0.0f};
};

enum class Screen { Splash, Menu, Game };

float clampf(float v, float lo, float hi)
{
    return std::max(lo, std::min(v, hi));
}

bool inside(float x, float y, float l, float t, float r, float b)
{
    return x >= l && x <= r && y >= t && y <= b;
}

} // namespace

class ClickerRTS final : public ra::Game {
public:
    void onStart(ra::Engine& engine) override
    {
        engine.setWindowTitle("Click Command - Loading");
        grassA_ = tryLoad(engine, "assets/click-command/plains_grass_a.png");
        grassB_ = tryLoad(engine, "assets/click-command/plains_grass_b.png");
        dirt_ = tryLoad(engine, "assets/click-command/plains_dirt.png");
        flowers_ = tryLoad(engine, "assets/click-command/plains_flowers.png");
        forest_ = tryLoad(engine, "assets/click-command/plains_forest.png");
        rock_ = tryLoad(engine, "assets/click-command/plains_rock.png");
        water_ = tryLoad(engine, "assets/click-command/plains_water.png");
        baseTex_ = tryLoad(engine, "assets/click-command/building_base.png");
        barracksTex_ = tryLoad(engine, "assets/click-command/building_barracks.png");
        towerTex_ = tryLoad(engine, "assets/click-command/building_tower.png");
        unitTex_ = tryLoad(engine, "assets/click-command/unit_basic.png");
        coinTex_ = tryLoad(engine, "assets/click-command/resource_coin.png");
    }

    void onUpdate(ra::Engine& engine, float dt) override
    {
        if (screen_ == Screen::Splash) {
            splashTime_ += dt;
            if (splashTime_ >= 1.1f || engine.keyPressed(ra::Key::Enter) || engine.mousePressed(ra::MouseButton::Left)) {
                screen_ = Screen::Menu;
                engine.setWindowTitle("Click Command");
            }
            return;
        }

        if (screen_ == Screen::Menu) {
            updateMenu(engine);
            return;
        }

        updateGame(engine, dt);
    }

    void onRender(ra::Engine& engine) override
    {
        if (screen_ == Screen::Splash) renderSplash(engine);
        else if (screen_ == Screen::Menu) renderMenu(engine);
        else renderGame(engine);
    }

private:
    ra::Texture tryLoad(ra::Engine& engine, const char* path)
    {
        try {
            return engine.loadTexture(path);
        } catch (...) {
            return {};
        }
    }

    void updateMenu(ra::Engine& engine)
    {
        if (engine.keyPressed(ra::Key::Escape)) {
            engine.quit();
            return;
        }

        const float w = static_cast<float>(engine.windowWidth());
        const float h = static_cast<float>(engine.windowHeight());
        const float bx = w * 0.5f - 210.0f;
        const float by = h * 0.55f;

        if (engine.keyPressed(ra::Key::Enter) ||
            (engine.mousePressed(ra::MouseButton::Left) && inside(engine.mouseX(), engine.mouseY(), bx, by, bx + 420.0f, by + 66.0f))) {
            startGame(engine);
        }
    }

    void startGame(ra::Engine& engine)
    {
        screen_ = Screen::Game;
        resources_ = 0;
        world_.clear();
        clickFx_.clear();
        cameraX_ = 350.0f;
        cameraY_ = 250.0f;
        zoom_ = 1.0f;
        dragActive_ = false;
        spawnBasicUnit(baseX_ + 40.0f, baseY_ + 110.0f, false);
        spawnBasicUnit(baseX_ - 45.0f, baseY_ + 120.0f, false);
        spawnBasicUnit(baseX_ + 95.0f, baseY_ + 55.0f, false);
        engine.setWindowTitle("Click Command - RTS Clicker");
    }

    int nextUnitCost() const
    {
        const int bought = std::max(0, static_cast<int>(world_.units().size()) - 3);
        return static_cast<int>(std::ceil(static_cast<double>(baseUnitCost_) * std::pow(unitCostGrowth_, bought)));
    }

    void buyBasicUnit()
    {
        const int cost = nextUnitCost();
        if (resources_ < cost) return;

        resources_ -= cost;
        const int n = static_cast<int>(world_.units().size());
        const float angle = static_cast<float>(n) * 0.88f;
        const float radius = 105.0f + 14.0f * static_cast<float>(n % 4);
        spawnBasicUnit(baseX_ + std::cos(angle) * radius, baseY_ + std::sin(angle) * radius, true);
    }

    void spawnBasicUnit(float x, float y, bool select)
    {
        const auto id = world_.spawnUnit({x, y}, 0, 92.0f);
        world_.setAutonomous(id, true, 150.0f);

        if (select) {
            world_.clearSelection();
            if (auto* unit = world_.find(id)) unit->selected = true;
        }
    }

    void updateGame(ra::Engine& engine, float dt)
    {
        if (engine.keyPressed(ra::Key::Escape)) {
            screen_ = Screen::Menu;
            engine.setWindowTitle("Click Command");
            return;
        }

        const float panelX = hudX(engine);
        const float mx = engine.mouseX();
        const float my = engine.mouseY();

        const float wheel = engine.mouseWheelY();
        if (wheel != 0.0f) zoom_ = clampf(zoom_ + wheel * 0.12f, 0.55f, 2.6f);
        if (engine.keyDown(ra::Key::Q)) zoom_ = clampf(zoom_ - dt, 0.55f, 2.6f);
        if (engine.keyDown(ra::Key::E)) zoom_ = clampf(zoom_ + dt, 0.55f, 2.6f);

        float dx = 0.0f;
        float dy = 0.0f;
        if (engine.keyDown(ra::Key::A) || engine.keyDown(ra::Key::Left)) dx -= 1.0f;
        if (engine.keyDown(ra::Key::D) || engine.keyDown(ra::Key::Right)) dx += 1.0f;
        if (engine.keyDown(ra::Key::W) || engine.keyDown(ra::Key::Up)) dy -= 1.0f;
        if (engine.keyDown(ra::Key::S) || engine.keyDown(ra::Key::Down)) dy += 1.0f;

        if (mx >= 0.0f && mx < 10.0f) dx -= 1.0f;
        if (mx < panelX && mx > panelX - 10.0f) dx += 1.0f;
        if (my >= 0.0f && my < 10.0f) dy -= 1.0f;
        if (my > static_cast<float>(engine.windowHeight()) - 10.0f) dy += 1.0f;

        const float cameraSpeed = (620.0f / zoom_) * dt;
        cameraX_ += dx * cameraSpeed;
        cameraY_ += dy * cameraSpeed;
        clampCamera(engine);

        const float buyX = panelX + 20.0f;
        const float buyY = 160.0f;
        const float buyW = static_cast<float>(engine.windowWidth()) - panelX - 40.0f;

        if (engine.mousePressed(ra::MouseButton::Left)) {
            if (inside(mx, my, buyX, buyY, buyX + buyW, buyY + 72.0f)) {
                buyBasicUnit();
            } else if (mx < panelX) {
                dragActive_ = true;
                dragStartX_ = dragEndX_ = mx;
                dragStartY_ = dragEndY_ = my;
            }
        }

        if (dragActive_ && engine.mouseDown(ra::MouseButton::Left)) {
            dragEndX_ = mx;
            dragEndY_ = my;
        }

        if (dragActive_ && engine.mouseReleased(ra::MouseButton::Left)) {
            dragEndX_ = mx;
            dragEndY_ = my;
            const float ddx = dragEndX_ - dragStartX_;
            const float ddy = dragEndY_ - dragStartY_;
            const float dist2 = ddx * ddx + ddy * ddy;

            if (dist2 <= 36.0f) {
                ++resources_;
                clickFx_.push_back({mx, my, 0.75f});
                world_.selectPoint({screenToWorldX(mx), screenToWorldY(my)}, 24.0f / zoom_, false);
            } else {
                const float l = screenToWorldX(std::min(dragStartX_, dragEndX_));
                const float r = screenToWorldX(std::max(dragStartX_, dragEndX_));
                const float t = screenToWorldY(std::min(dragStartY_, dragEndY_));
                const float b = screenToWorldY(std::max(dragStartY_, dragEndY_));
                world_.selectRect({l, t, r, b}, false);
            }
            dragActive_ = false;
        }

        if (engine.mousePressed(ra::MouseButton::Right) && mx < panelX) {
            world_.commandMove({screenToWorldX(mx), screenToWorldY(my)}, 34.0f);
        }

        world_.update(dt);

        for (ClickFx& fx : clickFx_) {
            fx.life -= dt;
            fx.y -= 28.0f * dt;
        }
        clickFx_.erase(
            std::remove_if(clickFx_.begin(), clickFx_.end(), [](const ClickFx& fx) { return fx.life <= 0.0f; }),
            clickFx_.end());
    }

    void renderSplash(ra::Engine& engine)
    {
        const float w = static_cast<float>(engine.windowWidth());
        const float h = static_cast<float>(engine.windowHeight());
        const float p = clampf(splashTime_ / 1.1f, 0.0f, 1.0f);
        engine.clear({10, 14, 18, 255});
        engine.drawText(w * 0.5f - 72.0f, h * 0.40f, "CLICK COMMAND", {240, 230, 186, 255});
        engine.drawText(w * 0.5f - 86.0f, h * 0.45f, "PIXEL RTS + CLICKER", {149, 174, 149, 255});
        engine.fillRect({w * 0.5f - 220.0f, h * 0.58f, 440.0f, 16.0f}, {35, 43, 48, 255});
        engine.fillRect({w * 0.5f - 220.0f, h * 0.58f, 440.0f * p, 16.0f}, {185, 151, 57, 255});
        engine.drawText(w * 0.5f - 28.0f, h * 0.62f, "LOADING", {200, 207, 204, 255});
    }

    void renderMenu(ra::Engine& engine)
    {
        const float w = static_cast<float>(engine.windowWidth());
        const float h = static_cast<float>(engine.windowHeight());
        engine.clear({9, 13, 16, 255});

        const float tile = 64.0f;
        for (int y = 0; y < static_cast<int>(h / tile) + 1; ++y) {
            for (int x = 0; x < static_cast<int>(w / tile) + 1; ++x) {
                const ra::Texture& t = ((x + y) & 1) ? grassA_ : grassB_;
                if (t) engine.drawTexture(t, {x * tile, y * tile, tile, tile});
            }
        }
        engine.fillRect({0.0f, 0.0f, w, h}, {8, 12, 15, 145});

        const float boxX = w * 0.5f - 290.0f;
        const float boxY = h * 0.18f;
        engine.fillRect({boxX, boxY, 580.0f, 540.0f}, {18, 25, 30, 240});
        engine.drawRect({boxX, boxY, 580.0f, 540.0f}, {118, 104, 72, 255});
        engine.drawText(boxX + 212.0f, boxY + 70.0f, "CLICK COMMAND", {239, 226, 181, 255});
        engine.drawText(boxX + 174.0f, boxY + 110.0f, "RTS WITH CLICKER ECONOMY", {150, 166, 174, 255});
        engine.drawText(boxX + 72.0f, boxY + 185.0f, "CLICK THE MAP TO GENERATE RESOURCES.", {196, 203, 199, 255});
        engine.drawText(boxX + 72.0f, boxY + 215.0f, "UNITS MOVE, PATROL AND SEPARATE.", {196, 203, 199, 255});
        engine.drawText(boxX + 72.0f, boxY + 245.0f, "BUY UNITS AND ISSUE RTS ORDERS.", {196, 203, 199, 255});

        const float bx = w * 0.5f - 210.0f;
        const float by = h * 0.55f;
        const bool hot = inside(engine.mouseX(), engine.mouseY(), bx, by, bx + 420.0f, by + 66.0f);
        engine.fillRect({bx, by, 420.0f, 66.0f}, hot ? ra::Color{79, 126, 73, 255} : ra::Color{55, 96, 61, 255});
        engine.drawRect({bx, by, 420.0f, 66.0f}, {187, 170, 114, 255});
        engine.drawText(bx + 168.0f, by + 29.0f, "START GAME", {246, 247, 243, 255});
        engine.drawText(boxX + 185.0f, boxY + 490.0f, "ENTER Start    ESC Exit", {137, 150, 157, 255});
    }

    void renderGame(ra::Engine& engine)
    {
        const float panelX = hudX(engine);
        const float h = static_cast<float>(engine.windowHeight());

        engine.clear({8, 11, 13, 255});
        drawWorld(engine, panelX, h);
        drawBuildings(engine);
        drawUnits(engine, panelX, h);

        if (dragActive_) {
            const float l = std::min(dragStartX_, dragEndX_);
            const float r = std::max(dragStartX_, dragEndX_);
            const float t = std::min(dragStartY_, dragEndY_);
            const float b = std::max(dragStartY_, dragEndY_);
            engine.drawRect({l, t, r - l, b - t}, {103, 235, 120, 255});
        }

        for (const ClickFx& fx : clickFx_) {
            if (coinTex_) engine.drawTexture(coinTex_, {fx.x - 10.0f, fx.y - 13.0f, 18.0f, 18.0f});
            engine.drawText(fx.x + 10.0f, fx.y - 7.0f, "+1", {239, 211, 82, 255});
        }

        drawHud(engine, panelX, h);
    }

    const ra::Texture& terrainTexture(int gx, int gy) const
    {
        if (gx >= 38 && gx <= 41) return water_;
        if (gy >= 18 && gy <= 20) return dirt_;
        if (gx < 5 || gy < 4 || gx > 59 || gy > 35) return forest_;
        const unsigned hash = static_cast<unsigned>(gx * 73856093u) ^ static_cast<unsigned>(gy * 19349663u);
        if ((hash % 53u) == 0u) return rock_;
        if ((hash % 17u) == 0u) return flowers_;
        return ((gx + gy) & 1) ? grassA_ : grassB_;
    }

    void drawWorld(ra::Engine& engine, float viewW, float viewH)
    {
        const float worldTile = 64.0f;
        const float tile = worldTile * zoom_;
        const int firstX = std::max(0, static_cast<int>(cameraX_ / worldTile));
        const int firstY = std::max(0, static_cast<int>(cameraY_ / worldTile));
        const int nx = static_cast<int>(viewW / tile) + 3;
        const int ny = static_cast<int>(viewH / tile) + 3;

        for (int y = firstY; y < firstY + ny; ++y) {
            for (int x = firstX; x < firstX + nx; ++x) {
                const float sx = (x * worldTile - cameraX_) * zoom_;
                const float sy = (y * worldTile - cameraY_) * zoom_;
                const auto& tex = terrainTexture(x, y);
                if (tex) engine.drawTexture(tex, {sx, sy, tile + 1.0f, tile + 1.0f});
                else engine.fillRect({sx, sy, tile + 1.0f, tile + 1.0f}, ((x + y) & 1) ? ra::Color{67, 105, 54, 255} : ra::Color{61, 96, 49, 255});
            }
        }
    }

    void drawBuilding(ra::Engine& engine, const ra::Texture& texture, float x, float y, float w, float h, ra::Color fallback)
    {
        const float sx = (x - cameraX_) * zoom_;
        const float sy = (y - cameraY_) * zoom_;
        const float sw = w * zoom_;
        const float sh = h * zoom_;
        if (texture) engine.drawTexture(texture, {sx, sy, sw, sh});
        else engine.fillRect({sx, sy, sw, sh}, fallback);
    }

    void drawBuildings(ra::Engine& engine)
    {
        drawBuilding(engine, baseTex_, baseX_ - 72.0f, baseY_ - 80.0f, 144.0f, 144.0f, {72, 78, 82, 255});
        drawBuilding(engine, barracksTex_, baseX_ + 190.0f, baseY_ - 30.0f, 128.0f, 112.0f, {91, 73, 55, 255});
        drawBuilding(engine, towerTex_, baseX_ - 260.0f, baseY_ - 155.0f, 80.0f, 128.0f, {75, 77, 70, 255});
        drawBuilding(engine, towerTex_, baseX_ + 360.0f, baseY_ - 175.0f, 80.0f, 128.0f, {75, 77, 70, 255});
    }

    void drawUnits(ra::Engine& engine, float viewW, float viewH)
    {
        const float size = std::max(18.0f, 34.0f * zoom_);

        for (const auto& u : world_.units()) {
            const float sx = (u.position.x - cameraX_) * zoom_;
            const float sy = (u.position.y - cameraY_) * zoom_;
            if (sx < -size || sy < -size || sx > viewW + size || sy > viewH + size) continue;

            if (u.selected) {
                const float tx = (u.destination.x - cameraX_) * zoom_;
                const float ty = (u.destination.y - cameraY_) * zoom_;
                engine.drawLine(sx, sy, tx, ty, {88, 190, 105, 160});
                engine.drawRect({sx - size * 0.64f, sy - size * 0.62f, size * 1.28f, size * 1.28f}, {99, 236, 116, 255});
            }

            if (unitTex_) engine.drawTexture(unitTex_, {sx - size * 0.5f, sy - size * 0.5f, size, size});
            else engine.fillRect({sx - size * 0.35f, sy - size * 0.35f, size * 0.7f, size * 0.7f}, {215, 204, 157, 255});
        }
    }

    void drawHud(ra::Engine& engine, float x, float h)
    {
        const float panelW = static_cast<float>(engine.windowWidth()) - x;
        engine.fillRect({x, 0.0f, panelW, h}, {17, 23, 28, 255});
        engine.drawRect({x, 0.0f, panelW, h}, {96, 86, 68, 255});
        engine.drawText(x + 20.0f, 22.0f, "CLICK COMMAND", {239, 226, 181, 255});
        engine.drawText(x + 20.0f, 58.0f, "RESOURCES", {160, 174, 180, 255});
        if (coinTex_) engine.drawTexture(coinTex_, {x + 20.0f, 82.0f, 28.0f, 28.0f});
        engine.drawText(x + 58.0f, 91.0f, std::to_string(resources_), {239, 211, 82, 255});
        engine.drawText(x + 20.0f, 124.0f, "MAP CLICK: +1", {138, 151, 158, 255});

        const int cost = nextUnitCost();
        const bool canBuy = resources_ >= cost;
        const float buyX = x + 20.0f;
        const float buyY = 160.0f;
        const float buyW = panelW - 40.0f;
        const bool hot = inside(engine.mouseX(), engine.mouseY(), buyX, buyY, buyX + buyW, buyY + 72.0f);

        ra::Color buttonColor = canBuy ? ra::Color{55, 110, 70, 255} : ra::Color{68, 70, 72, 255};
        if (hot && canBuy) buttonColor = {72, 137, 87, 255};
        engine.fillRect({buyX, buyY, buyW, 72.0f}, buttonColor);
        engine.drawRect({buyX, buyY, buyW, 72.0f}, {144, 155, 158, 255});
        if (unitTex_) engine.drawTexture(unitTex_, {buyX + 12.0f, buyY + 10.0f, 50.0f, 50.0f});
        engine.drawText(buyX + 76.0f, buyY + 17.0f, "BASIC UNIT", {244, 245, 239, 255});
        engine.drawText(buyX + 76.0f, buyY + 43.0f, ("COST: " + std::to_string(cost)).c_str(), canBuy ? ra::Color{239, 211, 82, 255} : ra::Color{160, 160, 160, 255});

        engine.drawText(x + 20.0f, 258.0f, ("UNITS: " + std::to_string(world_.units().size())).c_str(), {190, 201, 205, 255});
        engine.drawText(x + 20.0f, 282.0f, ("SELECTED: " + std::to_string(world_.selectedCount())).c_str(), {190, 201, 205, 255});
        engine.drawText(x + 20.0f, 306.0f, "UNIT AI: ACTIVE", {103, 216, 120, 255});
        engine.drawText(x + 20.0f, 330.0f, "COST: +18% / UNIT", {150, 163, 169, 255});

        engine.drawText(x + 20.0f, 376.0f, "CONTROLS", {235, 238, 236, 255});
        engine.drawText(x + 20.0f, 404.0f, "LMB MAP   +1 resource", {165, 177, 183, 255});
        engine.drawText(x + 20.0f, 428.0f, "DRAG LMB  Select units", {165, 177, 183, 255});
        engine.drawText(x + 20.0f, 452.0f, "RMB       Move selected", {165, 177, 183, 255});
        engine.drawText(x + 20.0f, 476.0f, "WASD      Pan camera", {165, 177, 183, 255});
        engine.drawText(x + 20.0f, 500.0f, "WHEEL     Zoom", {165, 177, 183, 255});
        engine.drawText(x + 20.0f, 524.0f, "ESC       Main menu", {165, 177, 183, 255});

        const float miniX = x + 20.0f;
        const float miniY = h - 190.0f;
        const float miniW = panelW - 40.0f;
        const float miniH = 130.0f;
        engine.drawText(miniX, miniY - 22.0f, "MINIMAP", {235, 238, 236, 255});
        engine.fillRect({miniX, miniY, miniW, miniH}, {43, 76, 43, 255});
        const float riverX = miniX + (38.0f * 64.0f / worldW_) * miniW;
        const float riverW = (4.0f * 64.0f / worldW_) * miniW;
        engine.fillRect({riverX, miniY, riverW, miniH}, {40, 88, 112, 255});

        for (const auto& u : world_.units()) {
            const float ux = miniX + (u.position.x / worldW_) * miniW;
            const float uy = miniY + (u.position.y / worldH_) * miniH;
            engine.fillRect({ux - 1.0f, uy - 1.0f, 3.0f, 3.0f}, u.selected ? ra::Color{103, 235, 120, 255} : ra::Color{235, 224, 179, 255});
        }

        const float vx = cameraX_ / worldW_ * miniW;
        const float vy = cameraY_ / worldH_ * miniH;
        const float vw = std::min(miniW, (hudX(engine) / zoom_) / worldW_ * miniW);
        const float vh = std::min(miniH, (static_cast<float>(engine.windowHeight()) / zoom_) / worldH_ * miniH);
        engine.drawRect({miniX + vx, miniY + vy, std::max(7.0f, vw), std::max(7.0f, vh)}, {237, 239, 216, 255});
    }

    float hudX(const ra::Engine& engine) const
    {
        return static_cast<float>(engine.windowWidth()) - 330.0f;
    }

    float screenToWorldX(float sx) const
    {
        return cameraX_ + sx / zoom_;
    }

    float screenToWorldY(float sy) const
    {
        return cameraY_ + sy / zoom_;
    }

    void clampCamera(const ra::Engine& engine)
    {
        const float visibleW = hudX(engine) / zoom_;
        const float visibleH = static_cast<float>(engine.windowHeight()) / zoom_;
        cameraX_ = clampf(cameraX_, 0.0f, std::max(0.0f, worldW_ - visibleW));
        cameraY_ = clampf(cameraY_, 0.0f, std::max(0.0f, worldH_ - visibleH));
    }

    Screen screen_{Screen::Splash};
    float splashTime_{0.0f};
    int resources_{0};
    ra::rts::World world_{{0.0f, 0.0f, 4200.0f, 2600.0f}};
    std::vector<ClickFx> clickFx_;
    float worldW_{4200.0f};
    float worldH_{2600.0f};
    float cameraX_{0.0f};
    float cameraY_{0.0f};
    float zoom_{1.0f};
    bool dragActive_{false};
    float dragStartX_{0.0f};
    float dragStartY_{0.0f};
    float dragEndX_{0.0f};
    float dragEndY_{0.0f};
    const float baseX_{980.0f};
    const float baseY_{760.0f};
    const int baseUnitCost_{10};
    const double unitCostGrowth_{1.18};

    ra::Texture grassA_{};
    ra::Texture grassB_{};
    ra::Texture dirt_{};
    ra::Texture flowers_{};
    ra::Texture forest_{};
    ra::Texture rock_{};
    ra::Texture water_{};
    ra::Texture baseTex_{};
    ra::Texture barracksTex_{};
    ra::Texture towerTex_{};
    ra::Texture unitTex_{};
    ra::Texture coinTex_{};
};

int main()
{
    ra::EngineConfig config;
    config.title = "Click Command";
    config.width = 1440;
    config.height = 900;
    config.resizable = true;
    config.vsync = true;

    try {
        ra::Engine engine(config);
        ClickerRTS app;
        return engine.run(app);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Startup failed: %s\n", e.what());
        return 1;
    }
}
