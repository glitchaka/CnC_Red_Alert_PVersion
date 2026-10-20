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
    }

    void onUpdate(ra::Engine& engine, float dt) override
    {
        if (screen_ == Screen::Splash) {
            splashTime_ += dt;
            if (splashTime_ >= 1.2f || engine.keyPressed(ra::Key::Enter) || engine.mousePressed(ra::MouseButton::Left)) {
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
        cameraX_ = 0.0f;
        cameraY_ = 0.0f;
        zoom_ = 1.0f;
        dragActive_ = false;
        spawnBasicUnit(baseX_, baseY_ + 82.0f, false);
        engine.setWindowTitle("Click Command - RTS Clicker");
    }

    int nextUnitCost() const
    {
        const int bought = std::max(0, static_cast<int>(world_.units().size()) - 1);
        return static_cast<int>(std::ceil(static_cast<double>(baseUnitCost_) * std::pow(unitCostGrowth_, bought)));
    }

    void buyBasicUnit()
    {
        const int cost = nextUnitCost();
        if (resources_ < cost) return;

        resources_ -= cost;
        const int n = static_cast<int>(world_.units().size());
        const float angle = static_cast<float>(n) * 0.88f;
        const float radius = 82.0f + 14.0f * static_cast<float>(n % 4);
        spawnBasicUnit(baseX_ + std::cos(angle) * radius, baseY_ + std::sin(angle) * radius, true);
    }

    void spawnBasicUnit(float x, float y, bool select)
    {
        const auto id = world_.spawnUnit({x, y}, 0, 92.0f);
        world_.setAutonomous(id, true, 135.0f);

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
        if (wheel != 0.0f) zoom_ = clampf(zoom_ + wheel * 0.1f, 0.45f, 2.5f);
        if (engine.keyDown(ra::Key::Q)) zoom_ = clampf(zoom_ - dt, 0.45f, 2.5f);
        if (engine.keyDown(ra::Key::E)) zoom_ = clampf(zoom_ + dt, 0.45f, 2.5f);

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
        const float p = clampf(splashTime_ / 1.2f, 0.0f, 1.0f);

        engine.clear({7, 10, 13, 255});
        engine.drawText(w * 0.5f - 72.0f, h * 0.40f, "CLICK COMMAND", {239, 238, 225, 255});
        engine.drawText(w * 0.5f - 70.0f, h * 0.45f, "RTS + CLICKER", {150, 166, 174, 255});
        engine.fillRect({w * 0.5f - 220.0f, h * 0.58f, 440.0f, 16.0f}, {35, 43, 48, 255});
        engine.fillRect({w * 0.5f - 220.0f, h * 0.58f, 440.0f * p, 16.0f}, {182, 145, 54, 255});
        engine.drawText(w * 0.5f - 28.0f, h * 0.62f, "LOADING", {200, 207, 204, 255});
    }

    void renderMenu(ra::Engine& engine)
    {
        const float w = static_cast<float>(engine.windowWidth());
        const float h = static_cast<float>(engine.windowHeight());
        engine.clear({9, 13, 16, 255});

        const float boxX = w * 0.5f - 290.0f;
        const float boxY = h * 0.18f;
        engine.fillRect({boxX, boxY, 580.0f, 540.0f}, {18, 25, 30, 255});
        engine.drawRect({boxX, boxY, 580.0f, 540.0f}, {78, 92, 100, 255});
        engine.drawText(boxX + 212.0f, boxY + 70.0f, "CLICK COMMAND", {239, 238, 225, 255});
        engine.drawText(boxX + 174.0f, boxY + 110.0f, "RTS WITH CLICKER ECONOMY", {150, 166, 174, 255});
        engine.drawText(boxX + 72.0f, boxY + 185.0f, "CLICK THE MAP TO GENERATE RESOURCES.", {196, 203, 199, 255});
        engine.drawText(boxX + 72.0f, boxY + 215.0f, "UNITS USE RTS MOVEMENT, AI AND SEPARATION.", {196, 203, 199, 255});
        engine.drawText(boxX + 72.0f, boxY + 245.0f, "BUY UNITS, SELECT THEM AND ISSUE ORDERS.", {196, 203, 199, 255});

        const float bx = w * 0.5f - 210.0f;
        const float by = h * 0.55f;
        const bool hot = inside(engine.mouseX(), engine.mouseY(), bx, by, bx + 420.0f, by + 66.0f);
        engine.fillRect({bx, by, 420.0f, 66.0f}, hot ? ra::Color{73, 126, 81, 255} : ra::Color{54, 104, 66, 255});
        engine.drawRect({bx, by, 420.0f, 66.0f}, {160, 172, 164, 255});
        engine.drawText(bx + 168.0f, by + 29.0f, "START GAME", {246, 247, 243, 255});
        engine.drawText(boxX + 185.0f, boxY + 490.0f, "ENTER Start    ESC Exit", {137, 150, 157, 255});
    }

    void renderGame(ra::Engine& engine)
    {
        const float panelX = hudX(engine);
        const float h = static_cast<float>(engine.windowHeight());

        engine.clear({8, 11, 13, 255});
        drawWorld(engine, panelX, h);
        drawUnits(engine, panelX, h);

        if (dragActive_) {
            const float l = std::min(dragStartX_, dragEndX_);
            const float r = std::max(dragStartX_, dragEndX_);
            const float t = std::min(dragStartY_, dragEndY_);
            const float b = std::max(dragStartY_, dragEndY_);
            engine.drawRect({l, t, r - l, b - t}, {103, 235, 120, 255});
        }

        for (const ClickFx& fx : clickFx_) {
            engine.drawText(fx.x - 6.0f, fx.y, "+1", {239, 211, 82, 255});
        }

        drawHud(engine, panelX, h);
    }

    void drawWorld(ra::Engine& engine, float viewW, float viewH)
    {
        engine.fillRect({0.0f, 0.0f, viewW, viewH}, {47, 67, 45, 255});

        const float tile = 64.0f * zoom_;
        const int firstX = static_cast<int>(cameraX_ / 64.0f);
        const int firstY = static_cast<int>(cameraY_ / 64.0f);
        const int nx = static_cast<int>(viewW / tile) + 3;
        const int ny = static_cast<int>(viewH / tile) + 3;

        for (int y = firstY; y < firstY + ny; ++y) {
            for (int x = firstX; x < firstX + nx; ++x) {
                const float sx = (x * 64.0f - cameraX_) * zoom_;
                const float sy = (y * 64.0f - cameraY_) * zoom_;
                const ra::Color c = ((x + y) & 1) ? ra::Color{51, 72, 48, 255} : ra::Color{45, 65, 43, 255};
                engine.fillRect({sx, sy, tile - 1.0f, tile - 1.0f}, c);
            }
        }

        worldRect(engine, 0.0f, 1230.0f, worldW_, 155.0f, {94, 82, 62, 255});
        worldRect(engine, 2550.0f, 0.0f, 240.0f, worldH_, {35, 73, 91, 255});
        worldRect(engine, 1220.0f, 440.0f, 650.0f, 420.0f, {58, 82, 49, 255});

        const float bx = (baseX_ - cameraX_) * zoom_;
        const float by = (baseY_ - cameraY_) * zoom_;
        const float bw = 120.0f * zoom_;
        const float bh = 92.0f * zoom_;
        engine.fillRect({bx - bw * 0.5f, by - bh * 0.5f, bw, bh}, {70, 78, 82, 255});
        engine.drawRect({bx - bw * 0.5f, by - bh * 0.5f, bw, bh}, {185, 190, 179, 255});
        engine.drawText(bx - 16.0f, by - 4.0f, "BASE", {240, 239, 227, 255});
    }

    void worldRect(ra::Engine& engine, float x, float y, float w, float h, ra::Color c)
    {
        engine.fillRect({(x - cameraX_) * zoom_, (y - cameraY_) * zoom_, w * zoom_, h * zoom_}, c);
    }

    void drawUnits(ra::Engine& engine, float viewW, float viewH)
    {
        const float size = std::max(8.0f, 18.0f * zoom_);

        for (const auto& u : world_.units()) {
            const float sx = (u.position.x - cameraX_) * zoom_;
            const float sy = (u.position.y - cameraY_) * zoom_;
            if (sx < -size || sy < -size || sx > viewW + size || sy > viewH + size) continue;

            if (u.selected) {
                const float tx = (u.destination.x - cameraX_) * zoom_;
                const float ty = (u.destination.y - cameraY_) * zoom_;
                engine.drawLine(sx, sy, tx, ty, {88, 190, 105, 160});
                engine.drawRect({sx - size * 0.72f, sy - size * 0.72f, size * 1.44f, size * 1.44f}, {99, 236, 116, 255});
            }

            engine.fillRect({sx - size * 0.5f, sy - size * 0.5f, size, size}, {208, 211, 190, 255});
            engine.fillRect({sx - size * 0.22f, sy - size * 0.55f, size * 0.44f, size * 0.35f}, {98, 112, 116, 255});
        }
    }

    void drawHud(ra::Engine& engine, float x, float h)
    {
        const float panelW = static_cast<float>(engine.windowWidth()) - x;
        engine.fillRect({x, 0.0f, panelW, h}, {17, 23, 28, 255});
        engine.drawText(x + 20.0f, 22.0f, "CLICK COMMAND", {239, 238, 225, 255});
        engine.drawText(x + 20.0f, 58.0f, "RESOURCES", {160, 174, 180, 255});
        engine.drawText(x + 20.0f, 82.0f, std::to_string(resources_), {239, 211, 82, 255});
        engine.drawText(x + 20.0f, 112.0f, "MAP CLICK: +1", {138, 151, 158, 255});

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
        engine.drawText(buyX + 14.0f, buyY + 16.0f, "BASIC UNIT", {244, 245, 239, 255});
        engine.drawText(
            buyX + 14.0f,
            buyY + 42.0f,
            ("COST: " + std::to_string(cost)).c_str(),
            canBuy ? ra::Color{239, 211, 82, 255} : ra::Color{160, 160, 160, 255});

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
        engine.fillRect({miniX, miniY, miniW, miniH}, {38, 58, 41, 255});

        for (const auto& u : world_.units()) {
            const float ux = miniX + (u.position.x / worldW_) * miniW;
            const float uy = miniY + (u.position.y / worldH_) * miniH;
            engine.fillRect({ux - 1.0f, uy - 1.0f, 3.0f, 3.0f}, u.selected ? ra::Color{103, 235, 120, 255} : ra::Color{218, 220, 198, 255});
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
