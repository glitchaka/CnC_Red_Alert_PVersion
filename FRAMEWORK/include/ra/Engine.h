#pragma once

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

    int windowWidth() const;
    int windowHeight() const;
    float deltaSeconds() const;

    void clear(Color color);
    void fillRect(Rect rect, Color color);
    void drawRect(Rect rect, Color color);

    const EngineConfig& config() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ra
