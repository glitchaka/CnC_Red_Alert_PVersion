#include <ra/Engine.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

namespace {

struct Unit {
    float x{0.0f};
    float y{0.0f};
    float tx{0.0f};
    float ty{0.0f};
    float speed{80.0f};
    bool selected{false};
};

enum class Screen { Splash, Menu, Stress };

float clampf(float v, float lo, float hi) { return std::max(lo, std::min(v, hi)); }
bool inside(float x, float y, float l, float t, float r, float b) { return x >= l && x <= r && y >= t && y <= b; }

std::string fmt(const char* pattern, double value)
{
    char buffer[96]{};
    std::snprintf(buffer, sizeof(buffer), pattern, value);
    return buffer;
}

} // namespace

class StressApp final : public ra::Game {
public:
    void onStart(ra::Engine& engine) override
    {
        engine.setWindowTitle("RA Framework - Loading");
        setLoad(0);
    }

    void onUpdate(ra::Engine& engine, float dt) override
    {
        if (screen_ == Screen::Splash) {
            splashTime_ += dt;
            if (splashTime_ >= 1.4f || engine.keyPressed(ra::Key::Enter) || engine.mousePressed(ra::MouseButton::Left)) {
                screen_ = Screen::Menu;
                engine.setWindowTitle("RA Framework - Main Menu");
            }
            return;
        }

        if (screen_ == Screen::Menu) {
            updateMenu(engine);
            return;
        }

        updateStress(engine, dt);
    }

    void onRender(ra::Engine& engine) override
    {
        if (screen_ == Screen::Splash) renderSplash(engine);
        else if (screen_ == Screen::Menu) renderMenu(engine);
        else renderStress(engine);
    }

private:
    void updateMenu(ra::Engine& engine)
    {
        if (engine.keyPressed(ra::Key::Escape)) {
            engine.quit();
            return;
        }
        if (engine.keyPressed(ra::Key::Space)) setLoad((loadIndex_ + 1) % loadLevels_.size());
        if (engine.keyPressed(ra::Key::Enter)) enterStress(engine);

        const float w = static_cast<float>(engine.windowWidth());
        const float h = static_cast<float>(engine.windowHeight());
        const float bx = w * 0.5f - 210.0f;
        const float by = h * 0.48f;
        if (engine.mousePressed(ra::MouseButton::Left)) {
            const float mx = engine.mouseX(), my = engine.mouseY();
            if (inside(mx, my, bx, by, bx + 420.0f, by + 64.0f)) enterStress(engine);
            else if (inside(mx, my, bx, by + 82.0f, bx + 420.0f, by + 146.0f)) setLoad((loadIndex_ + 1) % loadLevels_.size());
            else if (inside(mx, my, bx, by + 164.0f, bx + 420.0f, by + 228.0f)) engine.quit();
        }
    }

    void enterStress(ra::Engine& engine)
    {
        screen_ = Screen::Stress;
        engine.setWindowTitle("RA Framework - RTS Stress Test");
    }

    void updateStress(ra::Engine& engine, float dt)
    {
        if (engine.keyPressed(ra::Key::Escape)) {
            screen_ = Screen::Menu;
            engine.setWindowTitle("RA Framework - Main Menu");
            return;
        }
        if (engine.keyPressed(ra::Key::Space)) setLoad((loadIndex_ + 1) % loadLevels_.size());
        if (engine.keyPressed(ra::Key::Enter)) for (auto& u : units_) u.selected = true;

        const float wheel = engine.mouseWheelY();
        if (wheel != 0.0f) zoom_ = clampf(zoom_ + wheel * 0.1f, 0.35f, 3.0f);
        if (engine.keyDown(ra::Key::Q)) zoom_ = clampf(zoom_ - dt, 0.35f, 3.0f);
        if (engine.keyDown(ra::Key::E)) zoom_ = clampf(zoom_ + dt, 0.35f, 3.0f);

        float dx = 0.0f, dy = 0.0f;
        if (engine.keyDown(ra::Key::A) || engine.keyDown(ra::Key::Left)) dx -= 1.0f;
        if (engine.keyDown(ra::Key::D) || engine.keyDown(ra::Key::Right)) dx += 1.0f;
        if (engine.keyDown(ra::Key::W) || engine.keyDown(ra::Key::Up)) dy -= 1.0f;
        if (engine.keyDown(ra::Key::S) || engine.keyDown(ra::Key::Down)) dy += 1.0f;

        const float viewW = hudX(engine);
        const float viewH = static_cast<float>(engine.windowHeight());
        const float mx = engine.mouseX(), my = engine.mouseY();
        if (mx >= 0.0f && mx < 12.0f) dx -= 1.0f;
        if (mx < viewW && mx > viewW - 12.0f) dx += 1.0f;
        if (my >= 0.0f && my < 12.0f) dy -= 1.0f;
        if (my > viewH - 12.0f) dy += 1.0f;

        const float cameraSpeed = (700.0f / zoom_) * dt;
        cameraX_ += dx * cameraSpeed;
        cameraY_ += dy * cameraSpeed;
        clampCamera(engine);

        if (engine.mousePressed(ra::MouseButton::Left) && mx < viewW) {
            dragging_ = true;
            dragX0_ = dragX1_ = mx;
            dragY0_ = dragY1_ = my;
        }
        if (dragging_ && engine.mouseDown(ra::MouseButton::Left)) {
            dragX1_ = mx;
            dragY1_ = my;
        }
        if (dragging_ && engine.mouseReleased(ra::MouseButton::Left)) {
            dragX1_ = mx;
            dragY1_ = my;
            finishSelection();
            dragging_ = false;
        }
        if (engine.mousePressed(ra::MouseButton::Right) && mx < viewW) {
            issueMove(screenToWorldX(mx), screenToWorldY(my));
        }

        for (std::size_t i = 0; i < units_.size(); ++i) {
            Unit& u = units_[i];
            const float ux = u.tx - u.x;
            const float uy = u.ty - u.y;
            const float d2 = ux * ux + uy * uy;
            if (d2 < 9.0f) {
                if (!u.selected) retarget(u, i);
                continue;
            }
            const float d = std::sqrt(d2);
            const float step = std::min(d, u.speed * dt);
            u.x += ux / d * step;
            u.y += uy / d * step;
        }

        const float ms = dt * 1000.0f;
        avgMs_ = avgMs_ == 0.0f ? ms : avgMs_ * 0.94f + ms * 0.06f;
        peakMs_ = std::max(ms, peakMs_ * 0.997f);
    }

    void renderSplash(ra::Engine& engine)
    {
        const float w = static_cast<float>(engine.windowWidth());
        const float h = static_cast<float>(engine.windowHeight());
        const float p = clampf(splashTime_ / 1.4f, 0.0f, 1.0f);
        engine.clear({7, 10, 13, 255});
        engine.drawText(w * 0.5f - 92.0f, h * 0.42f, "RED ALERT FRAMEWORK", {230, 235, 232, 255});
        engine.drawText(w * 0.5f - 68.0f, h * 0.46f, "RTS ENGINE TESTBED", {145, 160, 168, 255});
        engine.fillRect({w * 0.5f - 220.0f, h * 0.58f, 440.0f, 16.0f}, {35, 43, 48, 255});
        engine.fillRect({w * 0.5f - 220.0f, h * 0.58f, 440.0f * p, 16.0f}, {177, 55, 49, 255});
        engine.drawText(w * 0.5f - 36.0f, h * 0.62f, "LOADING...", {200, 207, 204, 255});
    }

    void renderMenu(ra::Engine& engine)
    {
        const float w = static_cast<float>(engine.windowWidth());
        const float h = static_cast<float>(engine.windowHeight());
        engine.clear({9, 13, 16, 255});
        const float px = w * 0.5f - 280.0f;
        const float py = h * 0.18f;
        engine.fillRect({px, py, 560.0f, 560.0f}, {18, 25, 30, 255});
        engine.drawRect({px, py, 560.0f, 560.0f}, {75, 90, 100, 255});
        engine.drawText(px + 182.0f, py + 54.0f, "RED ALERT FRAMEWORK", {230, 235, 232, 255});
        engine.drawText(px + 192.0f, py + 84.0f, "RTS STRESS TEST", {145, 160, 168, 255});

        const float bx = w * 0.5f - 210.0f;
        const float by = h * 0.48f;
        button(engine, bx, by, "START STRESS TEST", {52, 109, 72, 255});
        button(engine, bx, by + 82.0f, ("LOAD: " + std::to_string(loadLevels_[loadIndex_]) + " UNITS").c_str(), {48, 78, 112, 255});
        button(engine, bx, by + 164.0f, "EXIT", {112, 53, 53, 255});

        engine.drawText(px + 72.0f, py + 500.0f, "ENTER Start   SPACE Load preset   ESC Exit", {140, 154, 162, 255});
    }

    void button(ra::Engine& engine, float x, float y, const char* label, ra::Color color)
    {
        const bool hot = inside(engine.mouseX(), engine.mouseY(), x, y, x + 420.0f, y + 64.0f);
        if (hot) {
            color.r = static_cast<unsigned char>(std::min(255, static_cast<int>(color.r) + 25));
            color.g = static_cast<unsigned char>(std::min(255, static_cast<int>(color.g) + 25));
            color.b = static_cast<unsigned char>(std::min(255, static_cast<int>(color.b) + 25));
        }
        engine.fillRect({x, y, 420.0f, 64.0f}, color);
        engine.drawRect({x, y, 420.0f, 64.0f}, {150, 160, 165, 255});
        engine.drawText(x + 24.0f, y + 28.0f, label, {245, 247, 245, 255});
    }

    void renderStress(ra::Engine& engine)
    {
        const float w = static_cast<float>(engine.windowWidth());
        const float h = static_cast<float>(engine.windowHeight());
        const float panelX = hudX(engine);
        engine.clear({8, 11, 13, 255});
        drawWorld(engine, panelX, h);
        drawUnits(engine, panelX, h);
        if (dragging_) {
            const float l = std::min(dragX0_, dragX1_), r = std::max(dragX0_, dragX1_);
            const float t = std::min(dragY0_, dragY1_), b = std::max(dragY0_, dragY1_);
            engine.drawRect({l, t, r - l, b - t}, {103, 235, 120, 255});
        }
        drawHud(engine, w, h, panelX);
    }

    void drawWorld(ra::Engine& engine, float viewW, float viewH)
    {
        engine.fillRect({0.0f, 0.0f, viewW, viewH}, {43, 60, 41, 255});
        const float tile = 64.0f * zoom_;
        const int firstX = static_cast<int>(cameraX_ / 64.0f);
        const int firstY = static_cast<int>(cameraY_ / 64.0f);
        const int nx = static_cast<int>(viewW / tile) + 3;
        const int ny = static_cast<int>(viewH / tile) + 3;
        for (int y = firstY; y < firstY + ny; ++y) {
            for (int x = firstX; x < firstX + nx; ++x) {
                const float sx = (x * 64.0f - cameraX_) * zoom_;
                const float sy = (y * 64.0f - cameraY_) * zoom_;
                const ra::Color c = ((x + y) & 1) ? ra::Color{48, 66, 45, 255} : ra::Color{43, 60, 41, 255};
                engine.fillRect({sx, sy, tile - 1.0f, tile - 1.0f}, c);
            }
        }
        worldRect(engine, 260.0f, 1120.0f, 2900.0f, 130.0f, {84, 76, 61, 255});
        worldRect(engine, 2460.0f, 180.0f, 190.0f, 2050.0f, {31, 65, 82, 255});
        worldRect(engine, 960.0f, 420.0f, 640.0f, 480.0f, {58, 73, 43, 255});
    }

    void worldRect(ra::Engine& engine, float x, float y, float w, float h, ra::Color c)
    {
        engine.fillRect({(x - cameraX_) * zoom_, (y - cameraY_) * zoom_, w * zoom_, h * zoom_}, c);
    }

    void drawUnits(ra::Engine& engine, float viewW, float viewH)
    {
        const float size = std::max(2.0f, 4.0f * zoom_);
        for (const Unit& u : units_) {
            const float sx = (u.x - cameraX_) * zoom_;
            const float sy = (u.y - cameraY_) * zoom_;
            if (sx < -size || sy < -size || sx > viewW + size || sy > viewH + size) continue;
            const ra::Color c = u.selected ? ra::Color{95, 235, 118, 255} : ra::Color{205, 211, 188, 255};
            engine.fillRect({sx - size * 0.5f, sy - size * 0.5f, size, size}, c);
        }
    }

    void drawHud(ra::Engine& engine, float w, float h, float x)
    {
        const float panelW = w - x;
        engine.fillRect({x, 0.0f, panelW, h}, {17, 23, 28, 255});
        engine.drawText(x + 22.0f, 22.0f, "RTS STRESS TEST", {235, 238, 236, 255});
        engine.drawText(x + 22.0f, 50.0f, ("Entities: " + std::to_string(units_.size())).c_str(), {190, 201, 205, 255});

        std::size_t selected = 0;
        for (const Unit& u : units_) if (u.selected) ++selected;
        engine.drawText(x + 22.0f, 72.0f, ("Selected: " + std::to_string(selected)).c_str(), {190, 201, 205, 255});
        engine.drawText(x + 22.0f, 94.0f, fmt("Frame: %.2f ms", avgMs_), {190, 201, 205, 255});
        const double fps = avgMs_ > 0.001f ? 1000.0 / avgMs_ : 0.0;
        engine.drawText(x + 22.0f, 116.0f, fmt("FPS: %.1f", fps), {190, 201, 205, 255});
        engine.drawText(x + 22.0f, 138.0f, fmt("Peak: %.2f ms", peakMs_), {190, 201, 205, 255});
        engine.drawText(x + 22.0f, 160.0f, fmt("Zoom: %.2fx", zoom_), {190, 201, 205, 255});
        engine.drawText(x + 22.0f, 182.0f, ("Camera X: " + std::to_string(static_cast<int>(cameraX_))).c_str(), {190, 201, 205, 255});
        engine.drawText(x + 22.0f, 204.0f, ("Camera Y: " + std::to_string(static_cast<int>(cameraY_))).c_str(), {190, 201, 205, 255});

        meter(engine, x + 22.0f, 244.0f, panelW - 44.0f, "ENTITY LOAD", static_cast<float>(units_.size()) / 25000.0f, {78, 157, 224, 255});
        meter(engine, x + 22.0f, 298.0f, panelW - 44.0f, "FRAME COST (33ms=max)", avgMs_ / 33.3f, {225, 148, 65, 255});
        meter(engine, x + 22.0f, 352.0f, panelW - 44.0f, "PEAK FRAME (66ms=max)", peakMs_ / 66.0f, {214, 76, 68, 255});

        engine.drawText(x + 22.0f, 430.0f, "CONTROLS", {235, 238, 236, 255});
        engine.drawText(x + 22.0f, 454.0f, "WASD/Arrows  Pan camera", {165, 177, 183, 255});
        engine.drawText(x + 22.0f, 476.0f, "Mouse wheel/Q/E  Zoom", {165, 177, 183, 255});
        engine.drawText(x + 22.0f, 498.0f, "Drag LMB  Select units", {165, 177, 183, 255});
        engine.drawText(x + 22.0f, 520.0f, "RMB  Move selected", {165, 177, 183, 255});
        engine.drawText(x + 22.0f, 542.0f, "ENTER  Select all", {165, 177, 183, 255});
        engine.drawText(x + 22.0f, 564.0f, "SPACE  Change load", {165, 177, 183, 255});
        engine.drawText(x + 22.0f, 586.0f, "ESC  Main menu", {165, 177, 183, 255});

        const float miniX = x + 22.0f, miniY = h - 210.0f, miniW = panelW - 44.0f, miniH = 150.0f;
        engine.drawText(miniX, miniY - 22.0f, "MINIMAP / CAMERA", {235, 238, 236, 255});
        engine.fillRect({miniX, miniY, miniW, miniH}, {34, 52, 39, 255});
        const float vx = cameraX_ / worldW_ * miniW;
        const float vy = cameraY_ / worldH_ * miniH;
        const float vw = std::min(miniW, (hudX(engine) / zoom_) / worldW_ * miniW);
        const float vh = std::min(miniH, (static_cast<float>(engine.windowHeight()) / zoom_) / worldH_ * miniH);
        engine.drawRect({miniX + vx, miniY + vy, std::max(6.0f, vw), std::max(6.0f, vh)}, {236, 238, 213, 255});
    }

    void meter(ra::Engine& engine, float x, float y, float w, const char* label, float ratio, ra::Color color)
    {
        engine.drawText(x, y, label, {170, 181, 187, 255});
        engine.fillRect({x, y + 20.0f, w, 16.0f}, {43, 51, 56, 255});
        engine.fillRect({x, y + 20.0f, w * clampf(ratio, 0.0f, 1.0f), 16.0f}, color);
    }

    void setLoad(std::size_t index)
    {
        loadIndex_ = index;
        units_.clear();
        units_.reserve(loadLevels_[loadIndex_]);
        std::mt19937 rng(0xC0FFEEu + static_cast<unsigned>(loadLevels_[loadIndex_]));
        std::uniform_real_distribution<float> xd(20.0f, worldW_ - 20.0f);
        std::uniform_real_distribution<float> yd(20.0f, worldH_ - 20.0f);
        std::uniform_real_distribution<float> sd(45.0f, 120.0f);
        for (std::size_t i = 0; i < loadLevels_[loadIndex_]; ++i) {
            Unit u;
            u.x = xd(rng); u.y = yd(rng); u.tx = xd(rng); u.ty = yd(rng); u.speed = sd(rng);
            units_.push_back(u);
        }
        avgMs_ = peakMs_ = 0.0f;
    }

    void retarget(Unit& u, std::size_t i)
    {
        const float a = static_cast<float>((i * 37u) % 628u) * 0.01f + phase_;
        const float b = static_cast<float>((i * 53u) % 628u) * 0.01f + phase_;
        u.tx = clampf(u.x + std::cos(a) * 340.0f, 8.0f, worldW_ - 8.0f);
        u.ty = clampf(u.y + std::sin(b) * 280.0f, 8.0f, worldH_ - 8.0f);
        phase_ += 0.00015f;
    }

    float hudX(const ra::Engine& engine) const { return static_cast<float>(engine.windowWidth()) - 330.0f; }
    float screenToWorldX(float sx) const { return cameraX_ + sx / zoom_; }
    float screenToWorldY(float sy) const { return cameraY_ + sy / zoom_; }

    void clampCamera(const ra::Engine& engine)
    {
        const float visibleW = hudX(engine) / zoom_;
        const float visibleH = static_cast<float>(engine.windowHeight()) / zoom_;
        cameraX_ = clampf(cameraX_, 0.0f, std::max(0.0f, worldW_ - visibleW));
        cameraY_ = clampf(cameraY_, 0.0f, std::max(0.0f, worldH_ - visibleH));
    }

    void finishSelection()
    {
        const float l = screenToWorldX(std::min(dragX0_, dragX1_));
        const float r = screenToWorldX(std::max(dragX0_, dragX1_));
        const float t = screenToWorldY(std::min(dragY0_, dragY1_));
        const float b = screenToWorldY(std::max(dragY0_, dragY1_));
        for (Unit& u : units_) u.selected = inside(u.x, u.y, l, t, r, b);
    }

    void issueMove(float wx, float wy)
    {
        std::size_t count = 0;
        for (const Unit& u : units_) if (u.selected) ++count;
        if (count == 0) return;
        const int cols = std::max(1, static_cast<int>(std::sqrt(static_cast<float>(count))));
        std::size_t n = 0;
        for (Unit& u : units_) {
            if (!u.selected) continue;
            const int col = static_cast<int>(n % static_cast<std::size_t>(cols));
            const int row = static_cast<int>(n / static_cast<std::size_t>(cols));
            u.tx = clampf(wx + (col - cols / 2) * 10.0f, 5.0f, worldW_ - 5.0f);
            u.ty = clampf(wy + row * 10.0f, 5.0f, worldH_ - 5.0f);
            ++n;
        }
    }

    static constexpr std::array<std::size_t, 4> loadLevels_{1000, 5000, 10000, 25000};
    Screen screen_{Screen::Splash};
    float splashTime_{0.0f};
    std::size_t loadIndex_{0};
    std::vector<Unit> units_;
    float worldW_{4200.0f}, worldH_{2600.0f};
    float cameraX_{0.0f}, cameraY_{0.0f}, zoom_{1.0f};
    float phase_{0.0f};
    float avgMs_{0.0f}, peakMs_{0.0f};
    bool dragging_{false};
    float dragX0_{0.0f}, dragY0_{0.0f}, dragX1_{0.0f}, dragY1_{0.0f};
};

int main()
{
    ra::EngineConfig config;
    config.title = "RA Framework";
    config.width = 1440;
    config.height = 900;
    config.resizable = true;
    config.vsync = true;

    try {
        ra::Engine engine(config);
        StressApp app;
        return engine.run(app);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Framework startup failed: %s\n", e.what());
        return 1;
    }
}
