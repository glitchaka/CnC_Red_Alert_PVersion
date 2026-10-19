#include <ra/Engine.h>

#include <exception>
#include <iostream>

class SandboxGame final : public ra::Game {
public:
    void onUpdate(ra::Engine& engine, float dt) override
    {
        if (engine.keyDown(ra::Key::Escape)) {
            engine.quit();
        }

        const float speed = 260.0f * dt;
        if (engine.keyDown(ra::Key::A) || engine.keyDown(ra::Key::Left)) {
            x_ -= speed;
        }
        if (engine.keyDown(ra::Key::D) || engine.keyDown(ra::Key::Right)) {
            x_ += speed;
        }
        if (engine.keyDown(ra::Key::W) || engine.keyDown(ra::Key::Up)) {
            y_ -= speed;
        }
        if (engine.keyDown(ra::Key::S) || engine.keyDown(ra::Key::Down)) {
            y_ += speed;
        }
    }

    void onRender(ra::Engine& engine) override
    {
        engine.clear({18, 20, 24, 255});
        engine.drawRect({80.0f, 80.0f, 560.0f, 360.0f}, {92, 120, 92, 255});
        engine.fillRect({x_, y_, 64.0f, 64.0f}, {190, 55, 55, 255});
    }

private:
    float x_{160.0f};
    float y_{160.0f};
};

int main()
{
    try {
        ra::EngineConfig config;
        config.title = "Red Alert Framework Sandbox";
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
