#define NOMINMAX
#include "AssetModel.h"

#include <ra/Engine.h>
#include <SDL3/SDL.h>

#include <windows.h>
#include <shobjidl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr float kPi = 3.14159265358979323846f;

const ra::Color kBackground{10, 14, 19, 255};
const ra::Color kTopBar{14, 20, 27, 255};
const ra::Color kSidebar{12, 18, 25, 255};
const ra::Color kPanel{18, 26, 35, 255};
const ra::Color kPanelRaised{24, 34, 45, 255};
const ra::Color kBorder{43, 58, 74, 255};
const ra::Color kBorderSoft{31, 43, 56, 255};
const ra::Color kText{222, 230, 238, 255};
const ra::Color kMuted{124, 141, 157, 255};
const ra::Color kAccent{78, 166, 223, 255};
const ra::Color kAccentSoft{35, 86, 119, 255};
const ra::Color kGreen{61, 225, 95, 255};

bool contains(ra::Rect r, float x, float y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string shortText(std::string value, int pixels)
{
    const int maxChars = std::max(1, pixels / 8);
    if (static_cast<int>(value.size()) <= maxChars) return value;
    if (maxChars <= 3) return value.substr(0, static_cast<std::size_t>(maxChars));
    return value.substr(0, static_cast<std::size_t>(maxChars - 3)) + "...";
}

std::string humanSize(std::uintmax_t bytes)
{
    std::ostringstream out;
    if (bytes < 1024) out << bytes << " B";
    else if (bytes < 1024ull * 1024ull) out << (bytes / 1024ull) << " KB";
    else if (bytes < 1024ull * 1024ull * 1024ull) out << (bytes / (1024ull * 1024ull)) << " MB";
    else out << (bytes / (1024ull * 1024ull * 1024ull)) << " GB";
    return out.str();
}

std::optional<fs::path> chooseFolder()
{
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return std::nullopt;

    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(L"Seleccionar biblioteca de assets");

    std::optional<fs::path> result;
    if (SUCCEEDED(dialog->Show(nullptr))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                result = fs::path(path);
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
    return result;
}

enum class AssetKind { Texture, Obj, Material, Bch };
enum class Filter { All, Textures, Models, Materials, Bch };

struct AssetEntry {
    fs::path path;
    std::string name;
    AssetKind kind{AssetKind::Texture};
    std::uintmax_t size{};
};

const char* kindName(AssetKind kind)
{
    switch (kind) {
    case AssetKind::Texture: return "TGA TEXTURE";
    case AssetKind::Obj: return "OBJ MODEL";
    case AssetKind::Material: return "MTL MATERIAL";
    case AssetKind::Bch: return "BCH MODEL";
    }
    return "ASSET";
}

ra::Color kindColor(AssetKind kind)
{
    switch (kind) {
    case AssetKind::Texture: return {72, 173, 215, 255};
    case AssetKind::Obj: return {110, 191, 139, 255};
    case AssetKind::Material: return {214, 165, 85, 255};
    case AssetKind::Bch: return {174, 111, 211, 255};
    }
    return kAccent;
}

bool passesFilter(AssetKind kind, Filter filter)
{
    if (filter == Filter::All) return true;
    if (filter == Filter::Textures) return kind == AssetKind::Texture;
    if (filter == Filter::Models) return kind == AssetKind::Obj || kind == AssetKind::Bch;
    if (filter == Filter::Materials) return kind == AssetKind::Material;
    return kind == AssetKind::Bch;
}

struct Layout {
    float width{};
    float height{};
    float top{72.0f};
    float bottom{28.0f};
    float sidebar{220.0f};
    float preview{500.0f};
    ra::Rect openButton{};
    ra::Rect refreshButton{};
    ra::Rect grid{};
    ra::Rect previewPane{};
    std::array<ra::Rect, 5> filterButtons{};
};

Layout layoutFor(int width, int height)
{
    Layout l{};
    l.width = static_cast<float>(width);
    l.height = static_cast<float>(height);
    l.preview = std::clamp(l.width * 0.34f, 420.0f, 540.0f);
    l.openButton = {l.width - 154.0f, 18.0f, 134.0f, 36.0f};
    l.refreshButton = {l.width - 268.0f, 18.0f, 102.0f, 36.0f};
    l.grid = {l.sidebar, l.top, std::max(280.0f, l.width - l.sidebar - l.preview), l.height - l.top - l.bottom};
    l.previewPane = {l.width - l.preview, l.top, l.preview, l.height - l.top - l.bottom};
    for (std::size_t i = 0; i < l.filterButtons.size(); ++i) {
        l.filterButtons[i] = {18.0f, 126.0f + static_cast<float>(i) * 44.0f, l.sidebar - 36.0f, 34.0f};
    }
    return l;
}

SDL_Renderer* currentRenderer()
{
    int count = 0;
    SDL_Window** windows = SDL_GetWindows(&count);
    if (!windows || count <= 0) {
        if (windows) SDL_free(windows);
        return nullptr;
    }
    SDL_Renderer* renderer = SDL_GetRenderer(windows[0]);
    SDL_free(windows);
    return renderer;
}

struct Projected {
    float sx{};
    float sy{};
    float x{};
    float y{};
    float z{};
};

struct FaceDraw {
    std::uint32_t triangle{};
    float depth{};
    float shade{};
};

class AssetHub final : public ra::Game {
public:
    void onStart(ra::Engine& engine) override
    {
        renderer_ = currentRenderer();
        SDL_HideCursor();
        engine.setWindowTitle("RA Asset Hub - TGA / OBJ / MTL / BCH");
        status_ = "Selecciona una biblioteca para comenzar.";
    }

    void onStop(ra::Engine& engine) override
    {
        for (auto& [key, item] : textureCache_) {
            (void)key;
            if (item.texture) engine.destroyTexture(item.texture);
        }
        textureCache_.clear();
        SDL_ShowCursor();
    }

    void onUpdate(ra::Engine& engine, float dt) override
    {
        pulse_ += dt;
        ++frame_;
        const Layout l = layoutFor(engine.windowWidth(), engine.windowHeight());
        const float mx = engine.mouseX();
        const float my = engine.mouseY();

        if (engine.mouseWheelY() != 0.0f) {
            if (contains(modelViewRect(l), mx, my) && currentModel_) {
                zoom_ = std::clamp(zoom_ + engine.mouseWheelY() * 0.08f, 0.45f, 2.5f);
            } else if (contains(l.grid, mx, my)) {
                scroll_ -= engine.mouseWheelY() * 72.0f;
                clampScroll(l);
            }
        }

        if (engine.mousePressed(ra::MouseButton::Left)) {
            if (contains(l.openButton, mx, my)) {
                if (const auto folder = chooseFolder()) scanFolder(engine, *folder);
            } else if (contains(l.refreshButton, mx, my) && !folder_.empty()) {
                scanFolder(engine, folder_);
            } else {
                for (std::size_t i = 0; i < l.filterButtons.size(); ++i) {
                    if (contains(l.filterButtons[i], mx, my)) {
                        filter_ = static_cast<Filter>(i);
                        rebuildVisible();
                        scroll_ = 0.0f;
                        if (!visible_.empty() && std::find(visible_.begin(), visible_.end(), selected_) == visible_.end()) {
                            selectAsset(engine, visible_.front());
                        }
                        break;
                    }
                }

                if (contains(l.grid, mx, my)) {
                    const int hit = cardAt(l, mx, my);
                    if (hit >= 0) selectAsset(engine, hit);
                }

                if (contains(modelViewRect(l), mx, my) && currentModel_) {
                    dragging_ = true;
                }
            }
        }

        if (engine.mouseReleased(ra::MouseButton::Left)) dragging_ = false;
        if (dragging_ && engine.mouseDown(ra::MouseButton::Left)) {
            const float dx = mx - previousMouseX_;
            const float dy = my - previousMouseY_;
            yaw_ += dx * 0.010f;
            pitch_ = std::clamp(pitch_ + dy * 0.010f, -1.45f, 1.45f);
        }

        if (engine.keyPressed(ra::Key::Up)) moveSelection(engine, -1);
        if (engine.keyPressed(ra::Key::Down)) moveSelection(engine, 1);
        if (engine.keyPressed(ra::Key::Escape)) engine.quit();

        previousMouseX_ = mx;
        previousMouseY_ = my;
    }

    void onRender(ra::Engine& engine) override
    {
        const Layout l = layoutFor(engine.windowWidth(), engine.windowHeight());
        engine.clear(kBackground);
        drawChrome(engine, l);
        drawSidebar(engine, l);
        drawLibrary(engine, l);
        drawPreview(engine, l);
        drawStatus(engine, l);
        drawCursor(engine);
    }

private:
    struct CachedTexture {
        ra::Texture texture{};
        std::uint64_t lastUse{};
        bool failed{};
    };

    void scanFolder(ra::Engine& engine, const fs::path& folder)
    {
        (void)engine;
        folder_ = folder;
        assets_.clear();
        selected_ = -1;
        currentModel_ = {};
        currentMaterials_.clear();
        loadError_.clear();
        scroll_ = 0.0f;

        std::error_code ec;
        for (fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
             it != end; it.increment(ec)) {
            if (ec) { ec.clear(); continue; }
            if (!it->is_regular_file(ec)) continue;

            std::string ext = lower(it->path().extension().u8string());
            AssetKind kind{};
            if (ext == ".tga") kind = AssetKind::Texture;
            else if (ext == ".obj") kind = AssetKind::Obj;
            else if (ext == ".mtl") kind = AssetKind::Material;
            else if (ext == ".bch") kind = AssetKind::Bch;
            else continue;

            AssetEntry asset{};
            asset.path = it->path();
            asset.name = it->path().filename().u8string();
            asset.kind = kind;
            asset.size = it->file_size(ec);
            if (ec) { asset.size = 0; ec.clear(); }
            assets_.push_back(std::move(asset));
        }

        std::sort(assets_.begin(), assets_.end(), [](const AssetEntry& a, const AssetEntry& b) {
            const std::string an = lower(a.name);
            const std::string bn = lower(b.name);
            if (an != bn) return an < bn;
            return a.path.u8string() < b.path.u8string();
        });

        rebuildVisible();
        if (!visible_.empty()) selectAsset(engine, visible_.front());
        status_ = std::to_string(assets_.size()) + " assets indexados.";
    }

    void rebuildVisible()
    {
        visible_.clear();
        for (int i = 0; i < static_cast<int>(assets_.size()); ++i) {
            if (passesFilter(assets_[static_cast<std::size_t>(i)].kind, filter_)) visible_.push_back(i);
        }
    }

    void selectAsset(ra::Engine& engine, int index)
    {
        if (index < 0 || index >= static_cast<int>(assets_.size())) return;
        selected_ = index;
        currentModel_ = {};
        currentMaterials_.clear();
        loadError_.clear();
        yaw_ = 0.65f;
        pitch_ = -0.28f;
        zoom_ = 1.0f;

        const AssetEntry& asset = assets_[static_cast<std::size_t>(selected_)];
        if (asset.kind == AssetKind::Obj) {
            if (!rahub::loadObj(asset.path, currentModel_, loadError_)) status_ = loadError_;
            else status_ = "OBJ cargado: " + std::to_string(currentModel_.triangles.size()) + " triangulos.";
        } else if (asset.kind == AssetKind::Bch) {
            if (!rahub::loadBch(asset.path, currentModel_, loadError_)) status_ = loadError_;
            else status_ = "BCH cargado: " + std::to_string(currentModel_.triangles.size()) + " triangulos.";
        } else if (asset.kind == AssetKind::Material) {
            currentMaterials_ = rahub::loadMtl(asset.path, &loadError_);
            if (currentMaterials_.empty()) status_ = loadError_;
            else status_ = "MTL cargado: " + std::to_string(currentMaterials_.size()) + " materiales.";
        } else {
            textureFor(engine, asset.path);
            status_ = "TGA listo para previsualizar.";
        }
    }

    void moveSelection(ra::Engine& engine, int delta)
    {
        if (visible_.empty()) return;
        auto it = std::find(visible_.begin(), visible_.end(), selected_);
        int position = it == visible_.end() ? 0 : static_cast<int>(std::distance(visible_.begin(), it));
        position = std::clamp(position + delta, 0, static_cast<int>(visible_.size()) - 1);
        selectAsset(engine, visible_[static_cast<std::size_t>(position)]);
    }

    ra::Texture textureFor(ra::Engine& engine, const fs::path& path)
    {
        const std::string key = path.u8string();
        auto found = textureCache_.find(key);
        if (found != textureCache_.end()) {
            found->second.lastUse = frame_;
            return found->second.texture;
        }

        if (textureCache_.size() >= 40) {
            auto oldest = textureCache_.end();
            for (auto it = textureCache_.begin(); it != textureCache_.end(); ++it) {
                if (oldest == textureCache_.end() || it->second.lastUse < oldest->second.lastUse) oldest = it;
            }
            if (oldest != textureCache_.end()) {
                if (oldest->second.texture) engine.destroyTexture(oldest->second.texture);
                textureCache_.erase(oldest);
            }
        }

        CachedTexture cached{};
        cached.lastUse = frame_;
        try {
            cached.texture = engine.loadTexture(key);
        } catch (...) {
            cached.failed = true;
        }
        const ra::Texture result = cached.texture;
        textureCache_[key] = cached;
        return result;
    }

    void drawChrome(ra::Engine& engine, const Layout& l)
    {
        engine.fillRect({0, 0, l.width, l.top}, kTopBar);
        engine.drawLine(0, l.top - 1, l.width, l.top - 1, kBorder);

        engine.drawText(22, 18, "RA ASSET HUB", kText);
        engine.drawText(22, 38, "TGA  /  OBJ  /  MTL  /  BCH", kMuted);

        if (!folder_.empty()) {
            engine.drawText(220, 26, shortText(folder_.u8string(), static_cast<int>(l.width - l.preview - 500)), kMuted);
        } else {
            engine.drawText(220, 26, "Ninguna biblioteca abierta", kMuted);
        }

        drawButton(engine, l.refreshButton, "REFRESH", !folder_.empty());
        drawButton(engine, l.openButton, "OPEN LIBRARY", true);
    }

    void drawButton(ra::Engine& engine, ra::Rect rect, const std::string& label, bool enabled)
    {
        const bool hover = enabled && contains(rect, engine.mouseX(), engine.mouseY());
        const ra::Color bg = hover ? kAccentSoft : kPanelRaised;
        engine.fillRect(rect, bg);
        engine.drawRect(rect, enabled ? (hover ? kAccent : kBorder) : kBorderSoft);
        const int width = static_cast<int>(label.size()) * 8;
        engine.drawText(rect.x + (rect.w - width) * 0.5f, rect.y + 14.0f, label, enabled ? kText : kMuted);
    }

    void drawSidebar(ra::Engine& engine, const Layout& l)
    {
        engine.fillRect({0, l.top, l.sidebar, l.height - l.top - l.bottom}, kSidebar);
        engine.drawLine(l.sidebar - 1, l.top, l.sidebar - 1, l.height - l.bottom, kBorderSoft);
        engine.drawText(18, 92, "LIBRARY", kMuted);

        static const std::array<const char*, 5> names{{"ALL ASSETS", "TEXTURES", "MODELS", "MATERIALS", "BCH"}};
        for (std::size_t i = 0; i < names.size(); ++i) {
            const bool active = static_cast<int>(filter_) == static_cast<int>(i);
            const bool hover = contains(l.filterButtons[i], engine.mouseX(), engine.mouseY());
            if (active) engine.fillRect(l.filterButtons[i], kAccentSoft);
            else if (hover) engine.fillRect(l.filterButtons[i], kPanel);
            if (active) engine.fillRect({l.filterButtons[i].x, l.filterButtons[i].y, 3, l.filterButtons[i].h}, kAccent);
            engine.drawText(l.filterButtons[i].x + 14, l.filterButtons[i].y + 13, names[i], active ? kText : kMuted);
        }

        const float statsY = l.height - l.bottom - 124.0f;
        engine.drawText(18, statsY, "INDEX", kMuted);
        engine.drawText(18, statsY + 24, std::to_string(assets_.size()) + " total", kText);
        engine.drawText(18, statsY + 44, std::to_string(visible_.size()) + " visibles", kMuted);
        const int tga = countKind(AssetKind::Texture);
        const int models = countKind(AssetKind::Obj) + countKind(AssetKind::Bch);
        engine.drawText(18, statsY + 66, std::to_string(tga) + " textures", kMuted);
        engine.drawText(18, statsY + 84, std::to_string(models) + " models", kMuted);
    }

    int countKind(AssetKind kind) const
    {
        return static_cast<int>(std::count_if(assets_.begin(), assets_.end(), [&](const AssetEntry& a) { return a.kind == kind; }));
    }

    void drawLibrary(ra::Engine& engine, const Layout& l)
    {
        engine.fillRect(l.grid, kBackground);
        if (folder_.empty()) {
            const float cx = l.grid.x + l.grid.w * 0.5f;
            const float cy = l.grid.y + l.grid.h * 0.42f;
            engine.drawText(cx - 88, cy - 28, "ASSET LIBRARY", kText);
            engine.drawText(cx - 180, cy, "Abre una carpeta para indexar texturas y modelos.", kMuted);
            drawButton(engine, {cx - 76, cy + 38, 152, 38}, "OPEN LIBRARY", true);
            return;
        }

        if (visible_.empty()) {
            engine.drawText(l.grid.x + 24, l.grid.y + 24, "No hay archivos para este filtro.", kMuted);
            return;
        }

        const float margin = 18.0f;
        const float gap = 12.0f;
        const float cardW = 168.0f;
        const float cardH = 142.0f;
        const int columns = std::max(1, static_cast<int>((l.grid.w - margin * 2 + gap) / (cardW + gap)));
        const float actualW = (l.grid.w - margin * 2 - gap * (columns - 1)) / columns;

        const float top = l.grid.y + margin - scroll_;
        for (std::size_t position = 0; position < visible_.size(); ++position) {
            const int row = static_cast<int>(position) / columns;
            const int col = static_cast<int>(position) % columns;
            ra::Rect card{
                l.grid.x + margin + col * (actualW + gap),
                top + row * (cardH + gap),
                actualW,
                cardH
            };
            if (card.y + card.h < l.grid.y || card.y > l.grid.y + l.grid.h) continue;
            drawCard(engine, card, assets_[static_cast<std::size_t>(visible_[position])], visible_[position] == selected_);
        }
    }

    void drawCard(ra::Engine& engine, ra::Rect card, const AssetEntry& asset, bool selected)
    {
        const bool hover = contains(card, engine.mouseX(), engine.mouseY());
        engine.fillRect(card, selected ? ra::Color{25, 43, 56, 255} : (hover ? kPanelRaised : kPanel));
        engine.drawRect(card, selected ? kAccent : (hover ? kBorder : kBorderSoft));

        ra::Rect thumb{card.x + 9, card.y + 9, card.w - 18, 82};
        engine.fillRect(thumb, {9, 14, 20, 255});
        if (asset.kind == AssetKind::Texture) {
            checker(engine, thumb, 10);
            const ra::Texture texture = textureFor(engine, asset.path);
            if (texture) drawTextureFit(engine, texture, thumb, 5.0f);
            else drawAssetGlyph(engine, thumb, asset.kind);
        } else {
            drawAssetGlyph(engine, thumb, asset.kind);
        }

        const ra::Color tag = kindColor(asset.kind);
        engine.fillRect({card.x + 9, card.y + 99, 5, 5}, tag);
        engine.drawText(card.x + 20, card.y + 97, shortText(asset.name, static_cast<int>(card.w - 28)), kText);
        engine.drawText(card.x + 9, card.y + 119, std::string(kindName(asset.kind)) + "  " + humanSize(asset.size), kMuted);
    }

    void drawAssetGlyph(ra::Engine& engine, ra::Rect r, AssetKind kind)
    {
        const ra::Color c = kindColor(kind);
        const float cx = r.x + r.w * 0.5f;
        const float cy = r.y + r.h * 0.5f;
        if (kind == AssetKind::Material) {
            engine.fillRect({cx - 28, cy - 20, 56, 40}, {31, 39, 48, 255});
            engine.drawRect({cx - 28, cy - 20, 56, 40}, c);
            engine.fillRect({cx - 20, cy - 12, 16, 16}, {181, 90, 71, 255});
            engine.fillRect({cx + 2, cy - 12, 16, 16}, {76, 151, 191, 255});
            engine.fillRect({cx - 20, cy + 8, 38, 5}, c);
        } else {
            const float s = 22.0f;
            engine.drawLine(cx, cy - s, cx + s, cy - 10, c);
            engine.drawLine(cx + s, cy - 10, cx + s, cy + 16, c);
            engine.drawLine(cx + s, cy + 16, cx, cy + s + 8, c);
            engine.drawLine(cx, cy + s + 8, cx - s, cy + 16, c);
            engine.drawLine(cx - s, cy + 16, cx - s, cy - 10, c);
            engine.drawLine(cx - s, cy - 10, cx, cy - s, c);
            engine.drawLine(cx, cy - s, cx, cy + 7, c);
            engine.drawLine(cx, cy + 7, cx + s, cy - 10, c);
            engine.drawLine(cx, cy + 7, cx - s, cy - 10, c);
        }
    }

    void drawPreview(ra::Engine& engine, const Layout& l)
    {
        engine.fillRect(l.previewPane, kPanel);
        engine.drawLine(l.previewPane.x, l.previewPane.y, l.previewPane.x, l.previewPane.y + l.previewPane.h, kBorderSoft);

        if (selected_ < 0 || selected_ >= static_cast<int>(assets_.size())) {
            engine.drawText(l.previewPane.x + 24, l.previewPane.y + 26, "PREVIEW", kMuted);
            engine.drawText(l.previewPane.x + 24, l.previewPane.y + 62, "Selecciona un asset de la biblioteca.", kMuted);
            return;
        }

        const AssetEntry& asset = assets_[static_cast<std::size_t>(selected_)];
        engine.drawText(l.previewPane.x + 22, l.previewPane.y + 18, shortText(asset.name, static_cast<int>(l.previewPane.w - 44)), kText);
        engine.drawText(l.previewPane.x + 22, l.previewPane.y + 40, std::string(kindName(asset.kind)) + "  /  " + humanSize(asset.size), kindColor(asset.kind));

        const ra::Rect view = modelViewRect(l);
        engine.fillRect(view, {8, 13, 18, 255});
        engine.drawRect(view, kBorderSoft);

        if (asset.kind == AssetKind::Texture) {
            checker(engine, view, 16);
            const ra::Texture texture = textureFor(engine, asset.path);
            if (texture) drawTextureFit(engine, texture, view, 18.0f);
            else drawCentered(engine, view, "No se pudo decodificar este TGA.", kMuted);
        } else if (asset.kind == AssetKind::Obj || asset.kind == AssetKind::Bch) {
            if (currentModel_) drawModel(engine, view, currentModel_);
            else drawCentered(engine, view, loadError_.empty() ? "Modelo sin geometria." : shortText(loadError_, static_cast<int>(view.w - 30)), kMuted);
        } else {
            drawMaterials(engine, view);
        }

        const float infoY = view.y + view.h + 18.0f;
        engine.drawText(l.previewPane.x + 22, infoY, "DETAILS", kMuted);
        engine.drawText(l.previewPane.x + 22, infoY + 24, shortText(asset.path.u8string(), static_cast<int>(l.previewPane.w - 44)), kText);

        if (currentModel_) {
            engine.drawText(l.previewPane.x + 22, infoY + 48,
                            std::to_string(currentModel_.vertices.size()) + " vertices  /  " +
                            std::to_string(currentModel_.triangles.size()) + " triangles", kMuted);
            engine.drawText(l.previewPane.x + 22, infoY + 68, "Drag: rotate   Wheel: zoom", kMuted);
            if (!currentModel_.warning.empty()) engine.drawText(l.previewPane.x + 22, infoY + 90, shortText(currentModel_.warning, static_cast<int>(l.previewPane.w - 44)), {207, 173, 91, 255});
        } else if (!currentMaterials_.empty()) {
            engine.drawText(l.previewPane.x + 22, infoY + 48, std::to_string(currentMaterials_.size()) + " materials", kMuted);
        }
    }

    ra::Rect modelViewRect(const Layout& l) const
    {
        return {l.previewPane.x + 18, l.previewPane.y + 72, l.previewPane.w - 36, std::max(260.0f, l.previewPane.h - 248.0f)};
    }

    void drawModel(ra::Engine& engine, ra::Rect view, const rahub::ModelData& model)
    {
        if (!renderer_ || model.vertices.empty() || model.triangles.empty()) return;

        const float cxModel = (model.minBounds.x + model.maxBounds.x) * 0.5f;
        const float cyModel = (model.minBounds.y + model.maxBounds.y) * 0.5f;
        const float czModel = (model.minBounds.z + model.maxBounds.z) * 0.5f;
        const float dx = model.maxBounds.x - model.minBounds.x;
        const float dy = model.maxBounds.y - model.minBounds.y;
        const float dz = model.maxBounds.z - model.minBounds.z;
        const float span = std::max({dx, dy, dz, 0.0001f});
        const float normalize = 1.75f / span;

        const float cy = std::cos(yaw_), sy = std::sin(yaw_);
        const float cp = std::cos(pitch_), sp = std::sin(pitch_);
        const float focal = std::min(view.w, view.h) * 0.88f;
        const float centerX = view.x + view.w * 0.5f;
        const float centerY = view.y + view.h * 0.52f;
        const float camera = 3.6f / zoom_;

        std::vector<Projected> projected(model.vertices.size());
        for (std::size_t i = 0; i < model.vertices.size(); ++i) {
            rahub::Vec3 p = model.vertices[i];
            p.x = (p.x - cxModel) * normalize;
            p.y = (p.y - cyModel) * normalize;
            p.z = (p.z - czModel) * normalize;

            const float x1 = p.x * cy + p.z * sy;
            const float z1 = -p.x * sy + p.z * cy;
            const float y2 = p.y * cp - z1 * sp;
            const float z2 = p.y * sp + z1 * cp;
            const float depth = std::max(0.25f, camera + z2);
            projected[i] = {centerX + x1 * focal / depth, centerY - y2 * focal / depth, x1, y2, z2};
        }

        for (int i = -5; i <= 5; ++i) {
            const float fade = static_cast<float>(5 - std::abs(i)) / 5.0f;
            const ra::Color grid{25, static_cast<unsigned char>(37 + fade * 10), static_cast<unsigned char>(48 + fade * 12), 255};
            engine.drawLine(view.x + 10, centerY + i * 18.0f, view.x + view.w - 10, centerY + i * 18.0f, grid);
        }

        std::vector<FaceDraw> faces;
        faces.reserve(model.triangles.size());
        for (std::uint32_t i = 0; i < model.triangles.size(); ++i) {
            const auto& t = model.triangles[i];
            if (t.a >= projected.size() || t.b >= projected.size() || t.c >= projected.size()) continue;
            const Projected& a = projected[t.a];
            const Projected& b = projected[t.b];
            const Projected& c = projected[t.c];
            const float ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
            const float vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
            const float nx = uy * vz - uz * vy;
            const float ny = uz * vx - ux * vz;
            const float nz = ux * vy - uy * vx;
            const float len = std::sqrt(nx * nx + ny * ny + nz * nz) + 0.0001f;
            const float light = std::clamp((nx * -0.25f + ny * 0.45f + nz * 0.85f) / len, -1.0f, 1.0f);
            faces.push_back({i, (a.z + b.z + c.z) / 3.0f, 0.48f + std::max(0.0f, light) * 0.52f});
        }
        std::sort(faces.begin(), faces.end(), [](const FaceDraw& a, const FaceDraw& b) { return a.depth > b.depth; });

        SDL_Rect clip{static_cast<int>(view.x + 1), static_cast<int>(view.y + 1), static_cast<int>(view.w - 2), static_cast<int>(view.h - 2)};
        SDL_SetRenderClipRect(renderer_, &clip);
        std::vector<SDL_Vertex> geometry;
        geometry.reserve(faces.size() * 3);
        for (const FaceDraw& face : faces) {
            const auto& t = model.triangles[face.triangle];
            const Projected* p[3]{&projected[t.a], &projected[t.b], &projected[t.c]};
            rahub::MaterialInfo material{};
            if (t.material >= 0 && t.material < static_cast<int>(model.materials.size())) material = model.materials[static_cast<std::size_t>(t.material)];
            const float r = std::clamp(material.r * face.shade, 0.0f, 1.0f);
            const float g = std::clamp(material.g * face.shade, 0.0f, 1.0f);
            const float b = std::clamp(material.b * face.shade, 0.0f, 1.0f);
            for (const Projected* v : p) {
                SDL_Vertex vertex{};
                vertex.position = {v->sx, v->sy};
                vertex.color = {r, g, b, 1.0f};
                vertex.tex_coord = {0.0f, 0.0f};
                geometry.push_back(vertex);
            }
        }
        if (!geometry.empty()) SDL_RenderGeometry(renderer_, nullptr, geometry.data(), static_cast<int>(geometry.size()), nullptr, 0);
        SDL_SetRenderClipRect(renderer_, nullptr);

        if (model.triangles.size() < 3500) {
            const ra::Color edge{18, 25, 31, 160};
            for (const auto& t : model.triangles) {
                if (t.a >= projected.size() || t.b >= projected.size() || t.c >= projected.size()) continue;
                const auto& a = projected[t.a]; const auto& b = projected[t.b]; const auto& c = projected[t.c];
                engine.drawLine(a.sx, a.sy, b.sx, b.sy, edge);
                engine.drawLine(b.sx, b.sy, c.sx, c.sy, edge);
                engine.drawLine(c.sx, c.sy, a.sx, a.sy, edge);
            }
        }
    }

    void drawMaterials(ra::Engine& engine, ra::Rect view)
    {
        if (currentMaterials_.empty()) {
            drawCentered(engine, view, loadError_.empty() ? "MTL sin materiales." : loadError_, kMuted);
            return;
        }
        engine.drawText(view.x + 18, view.y + 18, "MATERIAL PALETTE", kMuted);
        float y = view.y + 50;
        const std::size_t count = std::min<std::size_t>(currentMaterials_.size(), 12);
        for (std::size_t i = 0; i < count; ++i) {
            const auto& m = currentMaterials_[i];
            const ra::Color c{
                static_cast<unsigned char>(std::clamp(m.r, 0.0f, 1.0f) * 255),
                static_cast<unsigned char>(std::clamp(m.g, 0.0f, 1.0f) * 255),
                static_cast<unsigned char>(std::clamp(m.b, 0.0f, 1.0f) * 255), 255};
            engine.fillRect({view.x + 18, y, 42, 28}, c);
            engine.drawRect({view.x + 18, y, 42, 28}, kBorder);
            engine.drawText(view.x + 72, y + 5, shortText(m.name.empty() ? "Material" : m.name, static_cast<int>(view.w - 94)), kText);
            if (!m.diffuseTexture.empty()) engine.drawText(view.x + 72, y + 19, shortText(m.diffuseTexture.filename().u8string(), static_cast<int>(view.w - 94)), kMuted);
            y += 40;
            if (y > view.y + view.h - 36) break;
        }
    }

    void drawStatus(ra::Engine& engine, const Layout& l)
    {
        engine.fillRect({0, l.height - l.bottom, l.width, l.bottom}, {9, 13, 18, 255});
        engine.drawLine(0, l.height - l.bottom, l.width, l.height - l.bottom, kBorderSoft);
        engine.drawText(14, l.height - 18, shortText(status_, static_cast<int>(l.width - 210)), kMuted);
        engine.drawText(l.width - 176, l.height - 18, "NATIVE / SDL3", kMuted);
    }

    void checker(ra::Engine& engine, ra::Rect rect, int cell)
    {
        for (int y = 0; y < static_cast<int>(rect.h); y += cell) {
            for (int x = 0; x < static_cast<int>(rect.w); x += cell) {
                const bool alt = ((x / cell) + (y / cell)) & 1;
                engine.fillRect({rect.x + static_cast<float>(x), rect.y + static_cast<float>(y),
                                 static_cast<float>(std::min(cell, static_cast<int>(rect.w) - x)),
                                 static_cast<float>(std::min(cell, static_cast<int>(rect.h) - y))},
                                alt ? ra::Color{26, 34, 42, 255} : ra::Color{19, 26, 33, 255});
            }
        }
    }

    void drawTextureFit(ra::Engine& engine, const ra::Texture& texture, ra::Rect rect, float padding)
    {
        if (!texture) return;
        rect.x += padding; rect.y += padding; rect.w -= padding * 2; rect.h -= padding * 2;
        const float scale = std::min(rect.w / texture.width, rect.h / texture.height);
        const float w = texture.width * scale;
        const float h = texture.height * scale;
        engine.drawTexture(texture, {rect.x + (rect.w - w) * 0.5f, rect.y + (rect.h - h) * 0.5f, w, h});
    }

    void drawCentered(ra::Engine& engine, ra::Rect rect, const std::string& text, ra::Color color)
    {
        const std::string fitted = shortText(text, static_cast<int>(rect.w - 24));
        const float width = static_cast<float>(fitted.size() * 8);
        engine.drawText(rect.x + (rect.w - width) * 0.5f, rect.y + rect.h * 0.5f, fitted, color);
    }

    int cardAt(const Layout& l, float mx, float my) const
    {
        const float margin = 18.0f;
        const float gap = 12.0f;
        const float cardW = 168.0f;
        const float cardH = 142.0f;
        const int columns = std::max(1, static_cast<int>((l.grid.w - margin * 2 + gap) / (cardW + gap)));
        const float actualW = (l.grid.w - margin * 2 - gap * (columns - 1)) / columns;
        const float lx = mx - l.grid.x - margin;
        const float ly = my - l.grid.y - margin + scroll_;
        if (lx < 0 || ly < 0) return -1;
        const int col = static_cast<int>(lx / (actualW + gap));
        const int row = static_cast<int>(ly / (cardH + gap));
        if (col < 0 || col >= columns) return -1;
        const float withinX = lx - col * (actualW + gap);
        const float withinY = ly - row * (cardH + gap);
        if (withinX > actualW || withinY > cardH) return -1;
        const int position = row * columns + col;
        if (position < 0 || position >= static_cast<int>(visible_.size())) return -1;
        return visible_[static_cast<std::size_t>(position)];
    }

    void clampScroll(const Layout& l)
    {
        const float margin = 18.0f;
        const float gap = 12.0f;
        const float cardW = 168.0f;
        const float cardH = 142.0f;
        const int columns = std::max(1, static_cast<int>((l.grid.w - margin * 2 + gap) / (cardW + gap)));
        const int rows = (static_cast<int>(visible_.size()) + columns - 1) / columns;
        const float content = margin * 2 + rows * cardH + std::max(0, rows - 1) * gap;
        const float maximum = std::max(0.0f, content - l.grid.h);
        scroll_ = std::clamp(scroll_, 0.0f, maximum);
    }

    void drawCursor(ra::Engine& engine)
    {
        const float x = engine.mouseX();
        const float y = engine.mouseY();
        const float glow = 0.78f + 0.22f * std::sin(pulse_ * 7.0f);
        const ra::Color bright{static_cast<unsigned char>(70 + 70 * glow), 255, static_cast<unsigned char>(84 + 58 * glow), 255};
        const ra::Color dark{12, 74, 29, 255};
        const ra::Color shadow{0, 0, 0, 230};

        const std::array<std::pair<float, float>, 8> points{{
            {{0, 0}}, {{3, 25}}, {{8, 20}}, {{14, 31}}, {{19, 28}}, {{13, 17}}, {{25, 16}}, {{0, 0}}
        }};
        for (std::size_t i = 0; i + 1 < points.size(); ++i) {
            engine.drawLine(x + points[i].first + 2, y + points[i].second + 2,
                            x + points[i + 1].first + 2, y + points[i + 1].second + 2, shadow);
        }
        for (std::size_t i = 0; i + 1 < points.size(); ++i) {
            engine.drawLine(x + points[i].first, y + points[i].second,
                            x + points[i + 1].first, y + points[i + 1].second, bright);
        }
        engine.drawLine(x + 3, y + 5, x + 5, y + 19, dark);
        engine.drawLine(x + 5, y + 18, x + 12, y + 14, dark);
        engine.drawLine(x + 12, y + 14, x + 20, y + 14, dark);
        engine.fillRect({x + 2, y + 2, 2, 2}, bright);
        engine.fillRect({x + 6, y + 7, 2, 2}, bright);
    }

    fs::path folder_;
    std::vector<AssetEntry> assets_;
    std::vector<int> visible_;
    Filter filter_{Filter::All};
    int selected_{-1};
    float scroll_{0.0f};
    rahub::ModelData currentModel_;
    std::vector<rahub::MaterialInfo> currentMaterials_;
    std::string loadError_;
    std::string status_;
    std::unordered_map<std::string, CachedTexture> textureCache_;
    std::uint64_t frame_{};
    SDL_Renderer* renderer_{};
    float yaw_{0.65f};
    float pitch_{-0.28f};
    float zoom_{1.0f};
    bool dragging_{};
    float previousMouseX_{};
    float previousMouseY_{};
    float pulse_{};
};

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    SDL_SetMainReady();
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int result = 1;
    try {
        ra::EngineConfig config{};
        config.title = "RA Asset Hub";
        config.width = 1500;
        config.height = 900;
        config.resizable = true;
        config.vsync = true;
        ra::Engine engine(config);
        AssetHub app;
        result = engine.run(app);
    } catch (const std::exception& e) {
        MessageBoxA(nullptr, e.what(), "RA Asset Hub", MB_ICONERROR | MB_OK);
    }
    if (SUCCEEDED(com)) CoUninitialize();
    return result;
}
