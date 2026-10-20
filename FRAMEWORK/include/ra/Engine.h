#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace ra {

struct Color {
    unsigned char r{0};
    unsigned char g{0};
    unsigned char b{0};
    unsigned char a{255};
};

struct Rect {
    float x{0.0f};
    float y{0.0f};
    float w{0.0f};
    float h{0.0f};
};

struct Texture {
    std::uint32_t id{0};
    int width{0};
    int height{0};

    explicit operator bool() const noexcept { return id != 0; }
};

enum class Key {
    Escape,
    Space,
    Enter,
    Up,
    Down,
    Left,
    Right,
    W,
    A,
    S,
    D,
    Q,
    E,
    LeftShift,
    RightShift,
    LeftControl,
    RightControl,
    Count
};

enum class MouseButton {
    Left,
    Middle,
    Right,
    Count
};

struct EngineConfig {
    std::string title{"Red Alert Framework"};
    int width{1280};
    int height{720};
    bool resizable{true};
    bool vsync{true};
};

class Engine;

class Game {
public:
    virtual ~Game() = default;
    virtual void onStart(Engine&) {}
    virtual void onUpdate(Engine&, float) {}
    virtual void onRender(Engine&) {}
    virtual void onStop(Engine&) {}
};

class Engine {
public:
    explicit Engine(EngineConfig config = {});
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) noexcept;
    Engine& operator=(Engine&&) noexcept;

    int run(Game& game);
    void quit();

    bool keyDown(Key key) const;
    bool keyPressed(Key key) const;
    bool keyReleased(Key key) const;

    float mouseX() const;
    float mouseY() const;
    float mouseWheelY() const;
    bool mouseDown(MouseButton button) const;
    bool mousePressed(MouseButton button) const;
    bool mouseReleased(MouseButton button) const;

    int windowWidth() const;
    int windowHeight() const;
    float deltaSeconds() const;
    void setWindowTitle(const std::string& title);

    void clear(Color color);
    void fillRect(Rect rect, Color color);
    void drawRect(Rect rect, Color color);
    void drawLine(float x1, float y1, float x2, float y2, Color color);
    void drawText(float x, float y, const std::string& text, Color color);

    Texture createTextureRGBA(const unsigned char* rgba, int width, int height);
    Texture loadTexture(const std::string& path);
    void destroyTexture(Texture& texture);
    void drawTexture(const Texture& texture, Rect destination);
    void drawTextureRegion(const Texture& texture, Rect source, Rect destination);

    const EngineConfig& config() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ra
