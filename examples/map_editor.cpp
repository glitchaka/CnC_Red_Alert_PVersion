#include <ra/Engine.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr int MapW = 96;
constexpr int MapH = 64;
constexpr float Tile = 32.0f;
constexpr int PaletteCols = 5;
constexpr int PaletteRows = 7;
constexpr int PalettePerPage = PaletteCols * PaletteRows;

bool inside(float x, float y, float l, float t, float r, float b)
{
    return x >= l && x <= r && y >= t && y <= b;
}

ra::Rect src16(int x, int y)
{
    return {x * 16.0f, y * 16.0f, 16.0f, 16.0f};
}

struct PaletteTile {
    int asset{0};
    ra::Rect src{};
    std::string name;
};

struct Cell {
    int paletteIndex{0};
};

} // namespace

class MapEditor final : public ra::Game {
public:
    void onStart(ra::Engine& e) override
    {
        e.setWindowTitle("Click Command - Mystic Woods Map Editor");
        grass_ = e.loadTexture("assets/mystic/tilesets/grass.png");
        plains_ = e.loadTexture("assets/mystic/plains.png");
        decor_ = e.loadTexture("assets/mystic/tilesets/decor_16x16.png");
        fences_ = e.loadTexture("assets/mystic/tilesets/fences.png");
        walls_ = e.loadTexture("assets/mystic/tilesets/walls.png");
        flooring_ = e.loadTexture("assets/mystic/tilesets/flooring.png");
        wooden_ = e.loadTexture("assets/mystic/tilesets/wooden.png");
        chest_ = e.loadTexture("assets/mystic/objects/chest_01.png");
        player_ = e.loadTexture("assets/mystic/characters/player.png");
        slime_ = e.loadTexture("assets/mystic/characters/slime.png");

        buildPalette();
        cells_.assign(MapW * MapH, Cell{});
        status_ = "LEFT: PAINT  RIGHT: ERASE  WASD: PAN  WHEEL: ZOOM";
    }

    void onUpdate(ra::Engine& e, float dt) override
    {
        if (e.keyPressed(ra::Key::Escape)) {
            e.quit();
            return;
        }

        const float panelX = static_cast<float>(e.windowWidth()) - panelWidth_;
        const float mx = e.mouseX();
        const float my = e.mouseY();

        if (e.keyPressed(ra::Key::Space)) showGrid_ = !showGrid_;

        float moveX = 0.0f;
        float moveY = 0.0f;
        if (e.keyDown(ra::Key::A) || e.keyDown(ra::Key::Left)) moveX -= 1.0f;
        if (e.keyDown(ra::Key::D) || e.keyDown(ra::Key::Right)) moveX += 1.0f;
        if (e.keyDown(ra::Key::W) || e.keyDown(ra::Key::Up)) moveY -= 1.0f;
        if (e.keyDown(ra::Key::S) || e.keyDown(ra::Key::Down)) moveY += 1.0f;

        cameraX_ += moveX * 620.0f * dt / zoom_;
        cameraY_ += moveY * 620.0f * dt / zoom_;

        if (mx < panelX) {
            const float wheel = e.mouseWheelY();
            if (wheel != 0.0f) zoom_ = std::clamp(zoom_ + wheel * 0.12f, 0.5f, 3.0f);
        }
        clampCamera(e);

        if (e.mousePressed(ra::MouseButton::Left) && mx >= panelX) {
            handlePanelClick(e, panelX, mx, my);
            return;
        }

        if (mx < panelX) {
            if (e.mouseDown(ra::MouseButton::Left)) paintAt(mx, my, selected_);
            if (e.mouseDown(ra::MouseButton::Right)) paintAt(mx, my, 0);
        }
    }

    void onRender(ra::Engine& e) override
    {
        e.clear({8, 12, 9, 255});
        const float panelX = static_cast<float>(e.windowWidth()) - panelWidth_;
        drawMap(e, panelX);
        drawPanel(e, panelX);
    }

private:
    enum AssetId {
        Grass,
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

    const ra::Texture& textureFor(int asset) const
    {
        switch (asset) {
        case Grass: return grass_;
        case Plains: return plains_;
        case Decor: return decor_;
        case Fences: return fences_;
        case Walls: return walls_;
        case Flooring: return flooring_;
        case Wooden: return wooden_;
        case Chest: return chest_;
        case Player: return player_;
        default: return slime_;
        }
    }

    void addSheet(int asset, const ra::Texture& texture, int cellW, int cellH, const std::string& name, int maxRows = -1)
    {
        const int cols = std::max(1, texture.width / cellW);
        int rows = std::max(1, texture.height / cellH);
        if (maxRows > 0) rows = std::min(rows, maxRows);

        for (int y = 0; y < rows; ++y) {
            for (int x = 0; x < cols; ++x) {
                PaletteTile t;
                t.asset = asset;
                t.src = {x * static_cast<float>(cellW), y * static_cast<float>(cellH), static_cast<float>(cellW), static_cast<float>(cellH)};
                t.name = name + " " + std::to_string(x) + "," + std::to_string(y);
                palette_.push_back(std::move(t));
            }
        }
    }

    void buildPalette()
    {
        palette_.clear();

        addSheet(Grass, grass_, 16, 16, "GRASS", 1);

        // Mystic Woods plains is a 16x16 atlas. The lower Premium-labelled rows are deliberately excluded.
        addSheet(Plains, plains_, 16, 16, "PLAINS", 7);

        addSheet(Decor, decor_, 16, 16, "DECOR");
        addSheet(Fences, fences_, 16, 16, "FENCE");
        addSheet(Walls, walls_, 16, 16, "WALL");
        addSheet(Flooring, flooring_, 16, 16, "FLOOR");
        addSheet(Wooden, wooden_, 16, 16, "WOOD");
        addSheet(Chest, chest_, 16, 16, "CHEST");
        addSheet(Player, player_, 48, 48, "PLAYER", 1);
        addSheet(Slime, slime_, 32, 32, "SLIME", 1);

        if (palette_.empty()) palette_.push_back({Grass, src16(0, 0), "GRASS"});
        selected_ = 0;
    }

    void paintAt(float sx, float sy, int paletteIndex)
    {
        const float wx = cameraX_ + sx / zoom_;
        const float wy = cameraY_ + sy / zoom_;
        const int tx = static_cast<int>(std::floor(wx / Tile));
        const int ty = static_cast<int>(std::floor(wy / Tile));
        if (tx < 0 || ty < 0 || tx >= MapW || ty >= MapH) return;
        cells_[ty * MapW + tx].paletteIndex = std::clamp(paletteIndex, 0, static_cast<int>(palette_.size()) - 1);
        dirty_ = true;
    }

    void handlePanelClick(ra::Engine& e, float panelX, float mx, float my)
    {
        const float x = panelX + 14.0f;

        if (inside(mx, my, x, 44, x + 82, 74)) { saveMap(); return; }
        if (inside(mx, my, x + 90, 44, x + 172, 74)) { loadMap(); return; }
        if (inside(mx, my, x + 180, 44, x + 262, 74)) { clearMap(); return; }

        if (inside(mx, my, x, 82, x + 82, 112)) {
            page_ = std::max(0, page_ - 1);
            return;
        }
        if (inside(mx, my, x + 90, 82, x + 172, 112)) {
            const int pages = std::max(1, static_cast<int>((palette_.size() + PalettePerPage - 1) / PalettePerPage));
            page_ = std::min(pages - 1, page_ + 1);
            return;
        }

        const float startY = 132.0f;
        const float cell = 50.0f;
        const int col = static_cast<int>((mx - x) / cell);
        const int row = static_cast<int>((my - startY) / cell);
        if (col < 0 || col >= PaletteCols || row < 0 || row >= PaletteRows) return;

        const int index = page_ * PalettePerPage + row * PaletteCols + col;
        if (index >= 0 && index < static_cast<int>(palette_.size())) {
            selected_ = index;
            status_ = palette_[selected_].name;
        }
    }

    void saveMap()
    {
        std::filesystem::create_directories("maps");
        std::ofstream out("maps/editor_map.txt", std::ios::trunc);
        if (!out) {
            status_ = "SAVE ERROR";
            return;
        }

        out << "MYSTIC_MAP 1\n";
        out << MapW << ' ' << MapH << '\n';
        for (int y = 0; y < MapH; ++y) {
            for (int x = 0; x < MapW; ++x) {
                out << cells_[y * MapW + x].paletteIndex;
                if (x + 1 < MapW) out << ' ';
            }
            out << '\n';
        }
        dirty_ = false;
        status_ = "SAVED maps/editor_map.txt";
    }

    void loadMap()
    {
        std::ifstream in("maps/editor_map.txt");
        if (!in) {
            status_ = "NO maps/editor_map.txt";
            return;
        }

        std::string magic;
        int version = 0, w = 0, h = 0;
        in >> magic >> version >> w >> h;
        if (!in || magic != "MYSTIC_MAP" || version != 1 || w != MapW || h != MapH) {
            status_ = "INVALID MAP FILE";
            return;
        }

        for (auto& cell : cells_) {
            int index = 0;
            in >> index;
            if (!in) {
                status_ = "INVALID MAP DATA";
                return;
            }
            cell.paletteIndex = std::clamp(index, 0, static_cast<int>(palette_.size()) - 1);
        }
        dirty_ = false;
        status_ = "LOADED maps/editor_map.txt";
    }

    void clearMap()
    {
        for (auto& cell : cells_) cell.paletteIndex = 0;
        dirty_ = true;
        status_ = "NEW GRASS MAP";
    }

    void drawMap(ra::Engine& e, float panelX)
    {
        const int firstX = std::max(0, static_cast<int>(std::floor(cameraX_ / Tile)) - 1);
        const int firstY = std::max(0, static_cast<int>(std::floor(cameraY_ / Tile)) - 1);
        const int lastX = std::min(MapW - 1, firstX + static_cast<int>(panelX / (Tile * zoom_)) + 3);
        const int lastY = std::min(MapH - 1, firstY + static_cast<int>(e.windowHeight() / (Tile * zoom_)) + 3);

        const float drawSize = Tile * zoom_;
        for (int y = firstY; y <= lastY; ++y) {
            for (int x = firstX; x <= lastX; ++x) {
                const float dx = (x * Tile - cameraX_) * zoom_;
                const float dy = (y * Tile - cameraY_) * zoom_;
                const int paletteIndex = std::clamp(cells_[y * MapW + x].paletteIndex, 0, static_cast<int>(palette_.size()) - 1);
                const auto& p = palette_[paletteIndex];
                e.drawTextureRegion(textureFor(p.asset), p.src, {dx, dy, drawSize, drawSize});
                if (showGrid_ && zoom_ >= 0.8f) e.drawRect({dx, dy, drawSize, drawSize}, {20, 40, 24, 80});
            }
        }

        const float mx = e.mouseX();
        const float my = e.mouseY();
        if (mx < panelX) {
            const int tx = static_cast<int>(std::floor((cameraX_ + mx / zoom_) / Tile));
            const int ty = static_cast<int>(std::floor((cameraY_ + my / zoom_) / Tile));
            if (tx >= 0 && ty >= 0 && tx < MapW && ty < MapH) {
                const float dx = (tx * Tile - cameraX_) * zoom_;
                const float dy = (ty * Tile - cameraY_) * zoom_;
                e.drawRect({dx, dy, drawSize, drawSize}, {255, 236, 120, 255});
            }
        }
    }

    void drawButton(ra::Engine& e, float x, float y, float w, const char* text)
    {
        const bool hot = inside(e.mouseX(), e.mouseY(), x, y, x + w, y + 30.0f);
        e.fillRect({x, y, w, 30}, hot ? ra::Color{65, 100, 69, 255} : ra::Color{38, 61, 42, 255});
        e.drawRect({x, y, w, 30}, {116, 145, 119, 255});
        e.drawText(x + 10, y + 11, text, {235, 238, 224, 255});
    }

    void drawPanel(ra::Engine& e, float panelX)
    {
        const float w = static_cast<float>(e.windowWidth()) - panelX;
        e.fillRect({panelX, 0, w, static_cast<float>(e.windowHeight())}, {13, 19, 15, 255});
        e.drawText(panelX + 14, 16, "MYSTIC WOODS MAP EDITOR", {245, 239, 210, 255});

        drawButton(e, panelX + 14, 44, 82, "SAVE");
        drawButton(e, panelX + 104, 44, 82, "LOAD");
        drawButton(e, panelX + 194, 44, 82, "NEW");
        drawButton(e, panelX + 14, 82, 82, "PREV");
        drawButton(e, panelX + 104, 82, 82, "NEXT");

        const int pages = std::max(1, static_cast<int>((palette_.size() + PalettePerPage - 1) / PalettePerPage));
        e.drawText(panelX + 204, 94, (std::to_string(page_ + 1) + "/" + std::to_string(pages)).c_str(), {170, 185, 172, 255});

        const float startX = panelX + 14.0f;
        const float startY = 132.0f;
        const float cell = 50.0f;
        const int first = page_ * PalettePerPage;

        for (int i = 0; i < PalettePerPage; ++i) {
            const int index = first + i;
            if (index >= static_cast<int>(palette_.size())) break;
            const int col = i % PaletteCols;
            const int row = i / PaletteCols;
            const float x = startX + col * cell;
            const float y = startY + row * cell;
            const auto& p = palette_[index];

            e.fillRect({x, y, 42, 42}, {24, 34, 27, 255});
            e.drawTextureRegion(textureFor(p.asset), p.src, {x + 5, y + 5, 32, 32});
            e.drawRect({x, y, 42, 42}, index == selected_ ? ra::Color{255, 224, 96, 255} : ra::Color{76, 99, 80, 255});
        }

        const float bottom = static_cast<float>(e.windowHeight()) - 104.0f;
        e.drawText(panelX + 14, bottom, (dirty_ ? "* " : "  ") + status_, {185, 202, 188, 255});
        e.drawText(panelX + 14, bottom + 24, "LMB PAINT   RMB GRASS", {150, 168, 153, 255});
        e.drawText(panelX + 14, bottom + 42, "WASD/ARROWS PAN", {150, 168, 153, 255});
        e.drawText(panelX + 14, bottom + 60, "WHEEL ZOOM   SPACE GRID", {150, 168, 153, 255});
        e.drawText(panelX + 14, bottom + 78, "ESC EXIT", {150, 168, 153, 255});
    }

    void clampCamera(const ra::Engine& e)
    {
        const float viewW = (static_cast<float>(e.windowWidth()) - panelWidth_) / zoom_;
        const float viewH = static_cast<float>(e.windowHeight()) / zoom_;
        cameraX_ = std::clamp(cameraX_, 0.0f, std::max(0.0f, MapW * Tile - viewW));
        cameraY_ = std::clamp(cameraY_, 0.0f, std::max(0.0f, MapH * Tile - viewH));
    }

    const float panelWidth_ = 292.0f;
    float cameraX_{0.0f};
    float cameraY_{0.0f};
    float zoom_{1.0f};
    bool showGrid_{true};
    bool dirty_{false};
    int selected_{0};
    int page_{0};
    std::string status_;

    std::vector<PaletteTile> palette_;
    std::vector<Cell> cells_;

    ra::Texture grass_;
    ra::Texture plains_;
    ra::Texture decor_;
    ra::Texture fences_;
    ra::Texture walls_;
    ra::Texture flooring_;
    ra::Texture wooden_;
    ra::Texture chest_;
    ra::Texture player_;
    ra::Texture slime_;
};

int main()
{
    ra::EngineConfig config;
    config.title = "Click Command - Mystic Woods Map Editor";
    config.width = 1440;
    config.height = 900;
    config.resizable = true;
    config.vsync = true;

    ra::Engine engine(config);
    MapEditor editor;
    return engine.run(editor);
}
