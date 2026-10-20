#include <ra/Engine.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <random>
#include <vector>

namespace {

struct Unit {
    float x{0.0f};
    float y{0.0f};
    float targetX{0.0f};
    float targetY{0.0f};
    float speed{70.0f};
    bool selected{false};
};

float clampf(float value, float lo, float hi)
{
    return std::max(lo, std::min(value, hi));
}

float distanceSquared(float ax, float ay, float bx, float by)
{
    const float dx = ax - bx;
    const float dy = ay - by;
    return dx * dx + dy * dy;
}

bool inside(float x, float y, float l, float t, float r, float b)
{
    return x >= l && x <= r && y >= t && y <= b;
}

} // namespace

class SandboxGame final : public ra::Game {
public:
    void onStart(ra::Engine& engine) override
    {
        worldWidth_ = 4200.0f;
        worldHeight_ = 2600.0f;
        spawnUnits(stressLevels_[stressIndex_]);

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
            return;
        }

        if (engine.keyPressed(ra::Key::Space)) {
            stressIndex_ = (stressIndex_ + 1) % stressLevels_.size();
            spawnUnits(stressLevels_[stressIndex_]);
        }

        if (engine.keyPressed(ra::Key::Enter)) {
            for (auto& unit : units_) {
                unit.selected = true;
            }
        }

        const float cameraSpeed = 650.0f * dt;
        if (engine.keyDown(ra::Key::A) || engine.keyDown(ra::Key::Left)) cameraX_ -= cameraSpeed;
        if (engine.keyDown(ra::Key::D) || engine.keyDown(ra::Key::Right)) cameraX_ += cameraSpeed;
        if (engine.keyDown(ra::Key::W) || engine.keyDown(ra::Key::Up)) cameraY_ -= cameraSpeed;
        if (engine.keyDown(ra::Key::S) || engine.keyDown(ra::Key::Down)) cameraY_ += cameraSpeed;

        cameraX_ = clampf(cameraX_, 0.0f, std::max(0.0f, worldWidth_ - viewWidth(engine)));
        cameraY_ = clampf(cameraY_, 0.0f, std::max(0.0f, worldHeight_ - viewHeight(engine)));

        if (engine.mousePressed(ra::MouseButton::Left) && engine.mouseX() < hudX(engine)) {
            draggingSelection_ = true;
            selectionStartX_ = engine.mouseX();
            selectionStartY_ = engine.mouseY();
            selectionEndX_ = selectionStartX_;
            selectionEndY_ = selectionStartY_;
        }

        if (draggingSelection_ && engine.mouseDown(ra::MouseButton::Left)) {
            selectionEndX_ = engine.mouseX();
            selectionEndY_ = engine.mouseY();
        }

        if (draggingSelection_ && engine.mouseReleased(ra::MouseButton::Left)) {
            selectionEndX_ = engine.mouseX();
            selectionEndY_ = engine.mouseY();
            finalizeSelection();
            draggingSelection_ = false;
        }

        if (engine.mousePressed(ra::MouseButton::Right) && engine.mouseX() < hudX(engine)) {
            issueMoveCommand(screenToWorldX(engine.mouseX()), screenToWorldY(engine.mouseY()));
        }

        for (std::size_t i = 0; i < units_.size(); ++i) {
            auto& unit = units_[i];
            const float dx = unit.targetX - unit.x;
            const float dy = unit.targetY - unit.y;
            const float distSq = dx * dx + dy * dy;

            if (distSq < 16.0f) {
                if (!unit.selected) {
                    retargetWander(unit, i);
                }
                continue;
            }

            const float distance = std::sqrt(distSq);
            const float step = std::min(distance, unit.speed * dt);
            unit.x += dx / distance * step;
            unit.y += dy / distance * step;
        }

        // Smoothed frame metrics for the HUD.
        const float frameMs = dt * 1000.0f;
        smoothedFrameMs_ = smoothedFrameMs_ * 0.92f + frameMs * 0.08f;
        peakFrameMs_ = std::max(frameMs, peakFrameMs_ * 0.995f);
    }

    void onRender(ra::Engine& engine) override
    {
        const float width = static_cast<float>(engine.windowWidth());
        const float height = static_cast<float>(engine.windowHeight());
        const float hudWidth = 300.0f;
        const float mapRight = width - hudWidth;

        engine.clear({8, 11, 13, 255});
        engine.fillRect({0.0f, 0.0f, mapRight, height}, {42, 57, 40, 255});

        drawWorld(engine, mapRight, height);
        drawUnits(engine, mapRight, height);
        drawSelection(engine);
        drawHud(engine, hudWidth, mapRight, height);
    }

    void onStop(ra::Engine& engine) override
    {
        engine.destroyTexture(hudBar_);
    }

private:
    float hudX(const ra::Engine& engine) const
    {
        return static_cast<float>(engine.windowWidth()) - 300.0f;
    }

    float viewWidth(const ra::Engine& engine) const
    {
        return std::max(320.0f, hudX(engine));
    }

    float viewHeight(const ra::Engine& engine) const
    {
        return static_cast<float>(engine.windowHeight());
    }

    float screenToWorldX(float x) const { return x + cameraX_; }
    float screenToWorldY(float y) const { return y + cameraY_; }

    void spawnUnits(std::size_t count)
    {
        units_.clear();
        units_.reserve(count);

        std::mt19937 rng(0xC0FFEEu + static_cast<unsigned>(count));
        std::uniform_real_distribution<float> xDist(24.0f, worldWidth_ - 24.0f);
        std::uniform_real_distribution<float> yDist(24.0f, worldHeight_ - 24.0f);
        std::uniform_real_distribution<float> speedDist(45.0f, 115.0f);

        for (std::size_t i = 0; i < count; ++i) {
            Unit unit;
            unit.x = xDist(rng);
            unit.y = yDist(rng);
            unit.targetX = xDist(rng);
            unit.targetY = yDist(rng);
            unit.speed = speedDist(rng);
            units_.push_back(unit);
        }

        peakFrameMs_ = 0.0f;
        smoothedFrameMs_ = 0.0f;
    }

    void retargetWander(Unit& unit, std::size_t index)
    {
        const float a = static_cast<float>((index * 37u) % 628u) * 0.01f;
        const float b = static_cast<float>((index * 53u) % 628u) * 0.01f;
        unit.targetX = clampf(unit.x + std::cos(a + wanderPhase_) * 320.0f, 8.0f, worldWidth_ - 8.0f);
        unit.targetY = clampf(unit.y + std::sin(b + wanderPhase_) * 260.0f, 8.0f, worldHeight_ - 8.0f);
        wanderPhase_ += 0.00017f;
    }

    void finalizeSelection()
    {
        const float l = std::min(selectionStartX_, selectionEndX_);
        const float r = std::max(selectionStartX_, selectionEndX_);
        const float t = std::min(selectionStartY_, selectionEndY_);
        const float b = std::max(selectionStartY_, selectionEndY_);
        const float worldL = l + cameraX_;
        const float worldR = r + cameraX_;
        const float worldT = t + cameraY_;
        const float worldB = b + cameraY_;

        const bool tiny = (r - l) < 6.0f && (b - t) < 6.0f;
        if (tiny) {
            int hit = -1;
            float best = 18.0f * 18.0f;
            const float wx = selectionEndX_ + cameraX_;
            const float wy = selectionEndY_ + cameraY_;
            for (int i = 0; i < static_cast<int>(units_.size()); ++i) {
                const float d = distanceSquared(wx, wy, units_[i].x, units_[i].y);
                if (d < best) {
                    best = d;
                    hit = i;
                }
            }
            for (auto& unit : units_) unit.selected = false;
            if (hit >= 0) units_[hit].selected = true;
            return;
        }

        for (auto& unit : units_) {
            unit.selected = inside(unit.x, unit.y, worldL, worldT, worldR, worldB);
        }
    }

    void issueMoveCommand(float worldX, float worldY)
    {
        std::size_t selectedCount = 0;
        for (const auto& unit : units_) if (unit.selected) ++selectedCount;
        if (selectedCount == 0) return;

        const int columns = std::max(1, static_cast<int>(std::sqrt(static_cast<float>(selectedCount))));
        std::size_t order = 0;
        for (auto& unit : units_) {
            if (!unit.selected) continue;
            const int col = static_cast<int>(order % static_cast<std::size_t>(columns));
            const int row = static_cast<int>(order / static_cast<std::size_t>(columns));
            unit.targetX = clampf(worldX + (col - columns / 2) * 9.0f, 5.0f, worldWidth_ - 5.0f);
            unit.targetY = clampf(worldY + row * 9.0f, 5.0f, worldHeight_ - 5.0f);
            ++order;
        }
    }

    void drawWorld(ra::Engine& engine, float mapRight, float height)
    {
        constexpr float tile = 64.0f;
        const int firstX = static_cast<int>(cameraX_ / tile);
        const int firstY = static_cast<int>(cameraY_ / tile);
        const int lastX = firstX + static_cast<int>(mapRight / tile) + 2;
        const int lastY = firstY + static_cast<int>(height / tile) + 2;

        for (int gy = firstY; gy < lastY; ++gy) {
            for (int gx = firstX; gx < lastX; ++gx) {
                const float sx = gx * tile - cameraX_;
                const float sy = gy * tile - cameraY_;
                const bool alternate = ((gx + gy) & 1) != 0;
                engine.fillRect({sx, sy, tile - 1.0f, tile - 1.0f},
                    alternate ? ra::Color{48, 66, 45, 255} : ra::Color{43, 60, 41, 255});
            }
        }

        // Large terrain features force significant overdraw and make camera motion obvious.
        engine.fillRect({200.0f - cameraX_, 1250.0f - cameraY_, 3200.0f, 120.0f}, {83, 76, 61, 255});
        engine.fillRect({2450.0f - cameraX_, 180.0f - cameraY_, 180.0f, 2050.0f}, {31, 65, 82, 255});
        engine.fillRect({980.0f - cameraX_, 460.0f - cameraY_, 640.0f, 460.0f}, {58, 72, 43, 255});
    }

    void drawUnits(ra::Engine& engine, float mapRight, float height)
    {
        for (const auto& unit : units_) {
            const float x = unit.x - cameraX_;
            const float y = unit.y - cameraY_;
            if (x < -8.0f || y < -8.0f || x > mapRight + 8.0f || y > height + 8.0f) continue;

            if (unit.selected) {
                engine.fillRect({x - 4.0f, y - 4.0f, 8.0f, 8.0f}, {95, 234, 118, 255});
            } else {
                engine.fillRect({x - 2.0f, y - 2.0f, 4.0f, 4.0f}, {199, 204, 181, 255});
            }
        }
    }

    void drawSelection(ra::Engine& engine)
    {
        if (!draggingSelection_) return;
        const float l = std::min(selectionStartX_, selectionEndX_);
        const float r = std::max(selectionStartX_, selectionEndX_);
        const float t = std::min(selectionStartY_, selectionEndY_);
        const float b = std::max(selectionStartY_, selectionEndY_);
        engine.drawRect({l, t, r - l, b - t}, {93, 230, 116, 255});
    }

    void drawHud(ra::Engine& engine, float hudWidth, float hudXValue, float height)
    {
        engine.fillRect({hudXValue, 0.0f, hudWidth, height}, {17, 23, 28, 255});
        engine.fillRect({hudXValue + 14.0f, 14.0f, hudWidth - 28.0f, 58.0f}, {27, 37, 44, 255});

        if (hudBar_) {
            engine.drawTexture(hudBar_, {hudXValue + 24.0f, 28.0f, hudWidth - 48.0f, 28.0f});
        }

        // Four load-level indicators. The active one is bright.
        for (int i = 0; i < 4; ++i) {
            const bool active = static_cast<std::size_t>(i) == stressIndex_;
            engine.fillRect({hudXValue + 24.0f, 96.0f + i * 42.0f, 252.0f, 28.0f},
                active ? ra::Color{64, 159, 83, 255} : ra::Color{45, 55, 61, 255});
        }

        std::size_t selectedCount = 0;
        for (const auto& unit : units_) if (unit.selected) ++selectedCount;

        // Load meter: 25k entities = full bar.
        const float entityRatio = clampf(static_cast<float>(units_.size()) / 25000.0f, 0.0f, 1.0f);
        engine.fillRect({hudXValue + 24.0f, 286.0f, 252.0f, 18.0f}, {40, 47, 52, 255});
        engine.fillRect({hudXValue + 24.0f, 286.0f, 252.0f * entityRatio, 18.0f}, {79, 151, 216, 255});

        // Frame-time meter: full at 33.3ms.
        const float frameRatio = clampf(smoothedFrameMs_ / 33.3f, 0.0f, 1.0f);
        engine.fillRect({hudXValue + 24.0f, 322.0f, 252.0f, 18.0f}, {40, 47, 52, 255});
        engine.fillRect({hudXValue + 24.0f, 322.0f, 252.0f * frameRatio, 18.0f},
            frameRatio < 0.5f ? ra::Color{82, 186, 98, 255} : ra::Color{220, 139, 60, 255});

        // Peak-frame meter: full at 66ms.
        const float peakRatio = clampf(peakFrameMs_ / 66.0f, 0.0f, 1.0f);
        engine.fillRect({hudXValue + 24.0f, 358.0f, 252.0f, 18.0f}, {40, 47, 52, 255});
        engine.fillRect({hudXValue + 24.0f, 358.0f, 252.0f * peakRatio, 18.0f}, {204, 78, 68, 255});

        // Selection ratio meter.
        const float selectedRatio = units_.empty() ? 0.0f : static_cast<float>(selectedCount) / static_cast<float>(units_.size());
        engine.fillRect({hudXValue + 24.0f, 394.0f, 252.0f, 18.0f}, {40, 47, 52, 255});
        engine.fillRect({hudXValue + 24.0f, 394.0f, 252.0f * selectedRatio, 18.0f}, {114, 223, 141, 255});

        // Camera position minimap.
        const float miniX = hudXValue + 24.0f;
        const float miniY = 446.0f;
        const float miniW = 252.0f;
        const float miniH = 156.0f;
        engine.fillRect({miniX, miniY, miniW, miniH}, {35, 52, 39, 255});
        const float cameraMarkerX = miniX + (cameraX_ / worldWidth_) * miniW;
        const float cameraMarkerY = miniY + (cameraY_ / worldHeight_) * miniH;
        const float cameraMarkerW = std::max(8.0f, (viewWidth(engine) / worldWidth_) * miniW);
        const float cameraMarkerH = std::max(8.0f, (viewHeight(engine) / worldHeight_) * miniH);
        engine.drawRect({cameraMarkerX, cameraMarkerY, cameraMarkerW, cameraMarkerH}, {224, 226, 201, 255});
    }

    static constexpr std::array<std::size_t, 4> stressLevels_{1000, 5000, 10000, 25000};
    std::size_t stressIndex_{0};
    std::vector<Unit> units_;
    ra::Texture hudBar_{};

    float worldWidth_{4200.0f};
    float worldHeight_{2600.0f};
    float cameraX_{0.0f};
    float cameraY_{0.0f};
    float wanderPhase_{0.0f};

    bool draggingSelection_{false};
    float selectionStartX_{0.0f};
    float selectionStartY_{0.0f};
    float selectionEndX_{0.0f};
    float selectionEndY_{0.0f};

    float smoothedFrameMs_{0.0f};
    float peakFrameMs_{0.0f};
};

int main()
{
    try {
        ra::EngineConfig config;
        config.title = "RA Framework - RTS Stress Test";
        config.width = 1440;
        config.height = 900;
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
