#include <ra/Engine.h>

#include <SDL3/SDL.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace ra {
namespace {

constexpr std::size_t keyIndex(Key key)
{
    return static_cast<std::size_t>(key);
}

SDL_Scancode toScancode(Key key)
{
    switch (key) {
    case Key::Escape: return SDL_SCANCODE_ESCAPE;
    case Key::Space:  return SDL_SCANCODE_SPACE;
    case Key::Enter:  return SDL_SCANCODE_RETURN;
    case Key::Up:     return SDL_SCANCODE_UP;
    case Key::Down:   return SDL_SCANCODE_DOWN;
    case Key::Left:   return SDL_SCANCODE_LEFT;
    case Key::Right:  return SDL_SCANCODE_RIGHT;
    case Key::W:      return SDL_SCANCODE_W;
    case Key::A:      return SDL_SCANCODE_A;
    case Key::S:      return SDL_SCANCODE_S;
    case Key::D:      return SDL_SCANCODE_D;
    case Key::Count:  break;
    }
    return SDL_SCANCODE_UNKNOWN;
}

} // namespace

struct Engine::Impl {
    explicit Impl(EngineConfig cfg)
        : config(std::move(cfg))
    {
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
            throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
        }
        sdlInitialized = true;

        SDL_WindowFlags flags = config.resizable ? SDL_WINDOW_RESIZABLE : 0;
        window = SDL_CreateWindow(config.title.c_str(), config.width, config.height, flags);
        if (!window) {
            const std::string error = SDL_GetError();
            SDL_Quit();
            sdlInitialized = false;
            throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + error);
        }

        renderer = SDL_CreateRenderer(window, nullptr);
        if (!renderer) {
            const std::string error = SDL_GetError();
            SDL_DestroyWindow(window);
            window = nullptr;
            SDL_Quit();
            sdlInitialized = false;
            throw std::runtime_error(std::string("SDL_CreateRenderer failed: ") + error);
        }

        if (config.vsync) {
            SDL_SetRenderVSync(renderer, 1);
        }
    }

    ~Impl()
    {
        if (renderer) {
            SDL_DestroyRenderer(renderer);
        }
        if (window) {
            SDL_DestroyWindow(window);
        }
        if (sdlInitialized) {
            SDL_Quit();
        }
    }

    EngineConfig config;
    SDL_Window* window{nullptr};
    SDL_Renderer* renderer{nullptr};
    bool sdlInitialized{false};
    bool running{false};
    float delta{0.0f};
    std::array<bool, keyIndex(Key::Count)> currentKeys{};
    std::array<bool, keyIndex(Key::Count)> previousKeys{};
};

Engine::Engine(EngineConfig config)
    : impl_(std::make_unique<Impl>(std::move(config)))
{
}

Engine::~Engine() = default;
Engine::Engine(Engine&&) noexcept = default;
Engine& Engine::operator=(Engine&&) noexcept = default;

int Engine::run(Game& game)
{
    impl_->running = true;
    game.onStart(*this);

    auto previous = std::chrono::steady_clock::now();

    while (impl_->running) {
        impl_->previousKeys = impl_->currentKeys;

        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                impl_->running = false;
                continue;
            }

            if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
                const bool down = event.type == SDL_EVENT_KEY_DOWN;
                for (std::size_t i = 0; i < keyIndex(Key::Count); ++i) {
                    const auto key = static_cast<Key>(i);
                    if (toScancode(key) == event.key.scancode) {
                        impl_->currentKeys[i] = down;
                    }
                }
            }
        }

        const auto now = std::chrono::steady_clock::now();
        impl_->delta = std::chrono::duration<float>(now - previous).count();
        previous = now;
        if (impl_->delta > 0.25f) {
            impl_->delta = 0.25f;
        }

        game.onUpdate(*this, impl_->delta);
        game.onRender(*this);
        SDL_RenderPresent(impl_->renderer);
    }

    game.onStop(*this);
    return 0;
}

void Engine::quit()
{
    impl_->running = false;
}

bool Engine::keyDown(Key key) const
{
    const auto index = keyIndex(key);
    return index < impl_->currentKeys.size() && impl_->currentKeys[index];
}

bool Engine::keyPressed(Key key) const
{
    const auto index = keyIndex(key);
    return index < impl_->currentKeys.size() && impl_->currentKeys[index] && !impl_->previousKeys[index];
}

bool Engine::keyReleased(Key key) const
{
    const auto index = keyIndex(key);
    return index < impl_->currentKeys.size() && !impl_->currentKeys[index] && impl_->previousKeys[index];
}

int Engine::windowWidth() const
{
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(impl_->window, &width, &height);
    return width;
}

int Engine::windowHeight() const
{
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(impl_->window, &width, &height);
    return height;
}

float Engine::deltaSeconds() const
{
    return impl_->delta;
}

void Engine::clear(Color color)
{
    SDL_SetRenderDrawColor(impl_->renderer, color.r, color.g, color.b, color.a);
    SDL_RenderClear(impl_->renderer);
}

void Engine::fillRect(Rect rect, Color color)
{
    SDL_SetRenderDrawColor(impl_->renderer, color.r, color.g, color.b, color.a);
    const SDL_FRect sdlRect{rect.x, rect.y, rect.w, rect.h};
    SDL_RenderFillRect(impl_->renderer, &sdlRect);
}

void Engine::drawRect(Rect rect, Color color)
{
    SDL_SetRenderDrawColor(impl_->renderer, color.r, color.g, color.b, color.a);
    const SDL_FRect sdlRect{rect.x, rect.y, rect.w, rect.h};
    SDL_RenderRect(impl_->renderer, &sdlRect);
}

const EngineConfig& Engine::config() const noexcept
{
    return impl_->config;
}

} // namespace ra
