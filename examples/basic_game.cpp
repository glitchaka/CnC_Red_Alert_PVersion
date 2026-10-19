#include <ra/Engine.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <iostream>
#include <vector>

namespace {

struct Unit {
    float x;
    float y;
    float targetX;
    float targetY;
    bool selected{false};
};

float distanceSquared(float ax, float ay, float bx, float by)
{
    const float dx = ax - bx;
    const float dy = ay - by;
    return dx * dx + dy * dy;
}

} // namespace

class SandboxGame final : public ra::Game {
public:
    void onStart(ra::Engine& engine) override
    {
        units_ = {
            {170.0f, 150.0f, 170.0f, 150.0f},
            {235.0f, 190.0f, 235.0f, 190.0f},
            {320.0f, 145.0f, 320.0f, 145.0f},
            {430.0f, 225.0f, 430.0f, 225.0f},
            {520.0f, 175.0f, 520.0f, 175.0f},
            {610.0f, 250.0f, 610.0f, 250.0f},
            {260.0f, 335.0f, 260.0f, 335.0f},
            {380.0f, 390.0f, 380.0f, 390.0f},
            {525.0f, 360.0f, 525.0f, 360.0f},
            {680.0f, 410.0f, 680.0f, 410.0f},
        };

        try {
            hudBar_ = engine.loadTexture("assets/kenney/hud_bar.png");
        } catch (const std::exception& error) {
            std::cerr << "HUD sample asset not loaded: " << error.what() << '\n';
        }
    }

    void onUpdate(ra::Engine& engine, float dt) override
    {
        if (engine.keyDown(ra::Key::Escape)) {
            engine.quit();
        }

        if (engine.keyPressed(ra::Key::Space)) {
            showGrid_ = !showGrid_;
        }

        if (engine.mousePressed(ra::MouseButton::Left)) {
            selectUnitAt(engine.mouseX(), engine.mouseY());
        }

        if (engine.mousePressed(ra::MouseButton::Right)) {
            issueMoveCommand(engine.mouseX(), engine.mouseY(), engine.windowWidth());
        }

        float keyboardX = 0.0f;
        float keyboardY = 0.0f;
        if (engine.keyDown(ra::Key::A) || engine.keyDown(ra::Key::Left)) keyboardX -= 1.0f;
        if (engine.keyDown(ra::Key::D) || engine.keyDown(ra::Key::Right)) keyboardX += 1.0f;
        if (engine.keyDown(ra::Key::W) || engine.keyDown(ra::Key::Up)) keyboardY -= 1.0f;
        if (engine.keyDown(ra::Key::S) || engine.keyDown(ra::Key::Down)) keyboardY += 1.0f;

        const float keyboardSpeed = 150.0f * dt;
        for (auto& unit : units_) {
            if (unit.selected && (keyboardX != 0.0f || keyboardY != 0.0f)) {
                unit.targetX += keyboardX * keyboardSpeed;
                unit.targetY += keyboardY * keyboardSpeed;
            }

            const float dx = unit.targetX - unit.x;
            const float dy = unit.targetY - unit.y;
            const float distance = std::sqrt(dx * dx + dy * dy);
            if (distance > 1.0f) {
                const float step = std::min(distance, 105.0f * dt);
                unit.x += dx / distance * step;
                unit.y += dy / distance * step;
            }
        }
    }

    void onRender(ra::Engine& engine) override
    {
        const float width = static_cast<float>(engine.windowWidth());
        const float height = static_cast<float>(engine.windowHeight());
        const float hudWidth = 270.0f;
        const float mapLeft = 18.0f;
        const float mapTop = 18.0f;
        const float mapRight = std::max(mapLeft + 200.0f, width - hudWidth - 18.0f);
        const float mapBottom = std::max(mapTop + 200.0f, height - 18.0f);

        engine.clear({11, 14, 17, 255});

        // Playfield.
        engine.fillRect({mapLeft, mapTop, mapRight - mapLeft, mapBottom - mapTop}, {50, 67, 48, 255});

        if (showGrid_) {
            constexpr float tile = 48.0f;
            for (float y = mapTop; y < mapBottom; y += tile) {
                for (float x = mapLeft; x < mapRight; x += tile) {
                    const int ix = static_cast<int>((x - mapLeft) / tile);
                    const int iy = static_cast<int>((y - mapTop) / tile);
                    const ra::Color color = ((ix + iy) & 1)
                        ? ra::Color{59, 78, 56, 255}
                        : ra::Color{54, 72, 52, 255};
                    engine.fillRect({x, y, tile - 1.0f, tile - 1.0f}, color);
                }
            }
        }

        // A road and a water strip make the map visually readable.
        engine.fillRect({mapLeft + 20.0f, mapTop + 470.0f, mapRight - mapLeft - 40.0f, 72.0f}, {82, 77, 66, 255});
        engine.fillRect({mapLeft + 720.0f, mapTop + 20.0f, 90.0f, std::max(100.0f, mapBottom - mapTop - 40.0f)}, {35, 66, 82, 255});

        // Units and selection feedback.
        for (const auto& unit : units_) {
            if (unit.selected) {
                engine.drawRect({unit.x - 18.0f, unit.y - 18.0f, 36.0f, 36.0f}, {114, 235, 118, 255});
                engine.drawRect({unit.x - 20.0f, unit.y - 20.0f, 40.0f, 40.0f}, {40, 130, 55, 255});
            }

            engine.fillRect({unit.x - 12.0f, unit.y - 10.0f, 24.0f, 20.0f}, {181, 187, 162, 255});
            engine.fillRect({unit.x - 3.0f, unit.y - 16.0f, 6.0f, 13.0f}, {211, 215, 196, 255});
            engine.fillRect({unit.x - 8.0f, unit.y + 10.0f, 16.0f, 3.0f}, {31, 37, 32, 255});

            if (unit.selected && distanceSquared(unit.x, unit.y, unit.targetX, unit.targetY) > 9.0f) {
                engine.drawRect({unit.targetX - 5.0f, unit.targetY - 5.0f, 10.0f, 10.0f}, {111, 205, 255, 255});
            }
        }

        // HUD panel.
        const float hudX = width - hudWidth;
        engine.fillRect({hudX, 0.0f, hudWidth, height}, {18, 24, 29, 255});
        engine.fillRect({hudX + 12.0f, 12.0f, hudWidth - 24.0f, 52.0f}, {25, 38, 48, 255});

        if (hudBar_) {
            engine.drawTexture(hudBar_, {hudX + 20.0f, 24.0f, hudWidth - 40.0f, 28.0f});
            engine.drawTexture(hudBar_, {hudX + 20.0f, 104.0f, hudWidth - 40.0f, 24.0f});
            engine.drawTexture(hudBar_, {hudX + 20.0f, 228.0f, hudWidth - 40.0f, 24.0f});
        }

        // Selected-unit count as lit slots.
        int selectedCount = 0;
        for (const auto& unit : units_) {
            if (unit.selected) ++selectedCount;
        }
        for (int i = 0; i < 10; ++i) {
            engine.fillRect(
                {hudX + 22.0f + static_cast<float>(i % 5) * 42.0f,
                 146.0f + static_cast<float>(i / 5) * 38.0f,
                 30.0f,
                 26.0f},
                i < selectedCount ? ra::Color{92, 181, 105, 255} : ra::Color{42, 54, 61, 255});
        }

        // Live input feedback: LMB, RMB, keyboard movement.
        const bool left = engine.mouseDown(ra::MouseButton::Left);
        const bool right = engine.mouseDown(ra::MouseButton::Right);
        const bool moving = engine.keyDown(ra::Key::W) || engine.keyDown(ra::Key::A) ||
                            engine.keyDown(ra::Key::S) || engine.keyDown(ra::Key::D) ||
                            engine.keyDown(ra::Key::Up) || engine.keyDown(ra::Key::Down) ||
                            engine.keyDown(ra::Key::Left) || engine.keyDown(ra::Key::Right);

        engine.fillRect({hudX + 24.0f, 276.0f, 62.0f, 28.0f}, left ? ra::Color{76, 196, 99, 255} : ra::Color{48, 58, 64, 255});
        engine.fillRect({hudX + 102.0f, 276.0f, 62.0f, 28.0f}, right ? ra::Color{87, 151, 224, 255} : ra::Color{48, 58, 64, 255});
        engine.fillRect({hudX + 180.0f, 276.0f, 62.0f, 28.0f}, moving ? ra::Color{226, 181, 76, 255} : ra::Color{48, 58, 64, 255});

        // Mouse marker on the playfield.
        if (engine.mouseX() < hudX) {
            engine.fillRect({engine.mouseX() - 7.0f, engine.mouseY() - 1.0f, 14.0f, 2.0f}, {240, 240, 220, 255});
            engine.fillRect({engine.mouseX() - 1.0f, engine.mouseY() - 7.0f, 2.0f, 14.0f}, {240, 240, 220, 255});
        }
    }

    void onStop(ra::Engine& engine) override
    {
        engine.destroyTexture(hudBar_);
    }

private:
    void selectUnitAt(float x, float y)
    {
        int hit = -1;
        float best = 24.0f * 24.0f;
        for (int i = 0; i < static_cast<int>(units_.size()); ++i) {
            const float d = distanceSquared(x, y, units_[i].x, units_[i].y);
            if (d <= best) {
                best = d;
                hit = i;
            }
        }

        for (auto& unit : units_) {
            unit.selected = false;
        }
        if (hit >= 0) {
            units_[hit].selected = true;
        }
    }

    void issueMoveCommand(float x, float y, int windowWidth)
    {
        const float hudX = static_cast<float>(windowWidth) - 270.0f;
        if (x >= hudX) {
            return;
        }

        int order = 0;
        for (auto& unit : units_) {
            if (!unit.selected) {
                continue;
            }
            const int column = order % 3;
            const int row = order / 3;
            unit.targetX = x + static_cast<float>(column - 1) * 30.0f;
            unit.targetY = y + static_cast<float>(row) * 30.0f;
            ++order;
        }
    }

    std::vector<Unit> units_;
    ra::Texture hudBar_{};
    bool showGrid_{true};
};

int main()
{
    try {
        ra::EngineConfig config;
        config.title = "RA Framework - Minimal RTS Test";
        config.width = 1280;
        config.height = 720;
        config.resizable = true;
        config.vsync = true;

        ra::Engine engine(config);
        SandboxGame game;
        return engine.run(game);
    } catch (const std::exception& error) {
        std::cerr << "Framework startup failed: " << error.what() << '\n';
        return 1;
    }
}
