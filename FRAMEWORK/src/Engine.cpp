#include <ra/Engine.h>

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ra {
namespace {

constexpr std::size_t keyIndex(Key key)
{
    return static_cast<std::size_t>(key);
}

constexpr std::size_t mouseIndex(MouseButton button)
{
    return static_cast<std::size_t>(button);
}

SDL_Scancode toScancode(Key key)
{
    switch (key) {
    case Key::Escape:       return SDL_SCANCODE_ESCAPE;
    case Key::Space:        return SDL_SCANCODE_SPACE;
    case Key::Enter:        return SDL_SCANCODE_RETURN;
    case Key::Up:           return SDL_SCANCODE_UP;
    case Key::Down:         return SDL_SCANCODE_DOWN;
    case Key::Left:         return SDL_SCANCODE_LEFT;
    case Key::Right:        return SDL_SCANCODE_RIGHT;
    case Key::W:            return SDL_SCANCODE_W;
    case Key::A:            return SDL_SCANCODE_A;
    case Key::S:            return SDL_SCANCODE_S;
    case Key::D:            return SDL_SCANCODE_D;
    case Key::Q:            return SDL_SCANCODE_Q;
    case Key::E:            return SDL_SCANCODE_E;
    case Key::LeftShift:    return SDL_SCANCODE_LSHIFT;
    case Key::RightShift:   return SDL_SCANCODE_RSHIFT;
    case Key::LeftControl:  return SDL_SCANCODE_LCTRL;
    case Key::RightControl: return SDL_SCANCODE_RCTRL;
    case Key::Count:        break;
    }
    return SDL_SCANCODE_UNKNOWN;
}

MouseButton fromSDLMouseButton(Uint8 button)
{
    switch (button) {
    case SDL_BUTTON_LEFT:   return MouseButton::Left;
    case SDL_BUTTON_MIDDLE: return MouseButton::Middle;
    case SDL_BUTTON_RIGHT:  return MouseButton::Right;
    default:                return MouseButton::Count;
    }
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

        textures.push_back(nullptr);
    }

    ~Impl()
    {
        for (SDL_Texture* texture : textures) {
            if (texture) SDL_DestroyTexture(texture);
        }
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
        if (sdlInitialized) SDL_Quit();
    }

    EngineConfig config;
    SDL_Window* window{nullptr};
    SDL_Renderer* renderer{nullptr};
    bool sdlInitialized{false};
    bool running{false};
    float delta{0.0f};
    float mouseX{0.0f};
    float mouseY{0.0f};
    float wheelY{0.0f};
    std::array<bool, keyIndex(Key::Count)> currentKeys{};
    std::array<bool, keyIndex(Key::Count)> previousKeys{};
    std::array<bool, mouseIndex(MouseButton::Count)> currentMouse{};
    std::array<bool, mouseIndex(MouseButton::Count)> previousMouse{};
    std::vector<SDL_Texture*> textures;
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
        impl_->previousMouse = impl_->currentMouse;
        impl_->wheelY = 0.0f;

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

            if (event.type == SDL_EVENT_MOUSE_MOTION) {
                impl_->mouseX = event.motion.x;
                impl_->mouseY = event.motion.y;
            }

            if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                impl_->wheelY += event.wheel.y;
            }

            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                impl_->mouseX = event.button.x;
                impl_->mouseY = event.button.y;
                const MouseButton button = fromSDLMouseButton(event.button.button);
                if (button != MouseButton::Count) {
                    impl_->currentMouse[mouseIndex(button)] = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
                }
            }
        }

        const auto now = std::chrono::steady_clock::now();
        impl_->delta = std::chrono::duration<float>(now - previous).count();
        previous = now;
        if (impl_->delta > 0.25f) impl_->delta = 0.25f;

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

float Engine::mouseX() const
{
    return impl_->mouseX;
}

float Engine::mouseY() const
{
    return impl_->mouseY;
}

float Engine::mouseWheelY() const
{
    return impl_->wheelY;
}

bool Engine::mouseDown(MouseButton button) const
{
    const auto index = mouseIndex(button);
    return index < impl_->currentMouse.size() && impl_->currentMouse[index];
}

bool Engine::mousePressed(MouseButton button) const
{
    const auto index = mouseIndex(button);
    return index < impl_->currentMouse.size() && impl_->currentMouse[index] && !impl_->previousMouse[index];
}

bool Engine::mouseReleased(MouseButton button) const
{
    const auto index = mouseIndex(button);
    return index < impl_->currentMouse.size() && !impl_->currentMouse[index] && impl_->previousMouse[index];
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

void Engine::setWindowTitle(const std::string& title)
{
    impl_->config.title = title;
    SDL_SetWindowTitle(impl_->window, title.c_str());
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

void Engine::drawLine(float x1, float y1, float x2, float y2, Color color)
{
    SDL_SetRenderDrawColor(impl_->renderer, color.r, color.g, color.b, color.a);
    SDL_RenderLine(impl_->renderer, x1, y1, x2, y2);
}

void Engine::drawText(float x, float y, const std::string& text, Color color)
{
    SDL_SetRenderDrawColor(impl_->renderer, color.r, color.g, color.b, color.a);
    SDL_RenderDebugText(impl_->renderer, x, y, text.c_str());
}

Texture Engine::createTextureRGBA(const unsigned char* rgba, int width, int height)
{
    if (!rgba || width <= 0 || height <= 0) {
        throw std::runtime_error("createTextureRGBA received invalid image data");
    }

    SDL_Texture* texture = SDL_CreateTexture(
        impl_->renderer,
        SDL_PIXELFORMAT_RGBA32,
        SDL_TEXTUREACCESS_STATIC,
        width,
        height);
    if (!texture) {
        throw std::runtime_error(std::string("SDL_CreateTexture failed: ") + SDL_GetError());
    }

    if (!SDL_UpdateTexture(texture, nullptr, rgba, width * 4)) {
        const std::string error = SDL_GetError();
        SDL_DestroyTexture(texture);
        throw std::runtime_error(std::string("SDL_UpdateTexture failed: ") + error);
    }

    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);

    impl_->textures.push_back(texture);
    return Texture{static_cast<std::uint32_t>(impl_->textures.size() - 1), width, height};
}

Texture Engine::loadTexture(const std::string& path)
{
    SDL_Texture* texture = IMG_LoadTexture(impl_->renderer, path.c_str());
    if (!texture) {
        throw std::runtime_error(std::string("IMG_LoadTexture failed for '") + path + "': " + SDL_GetError());
    }

    float width = 0.0f;
    float height = 0.0f;
    if (!SDL_GetTextureSize(texture, &width, &height)) {
        const std::string error = SDL_GetError();
        SDL_DestroyTexture(texture);
        throw std::runtime_error(std::string("SDL_GetTextureSize failed: ") + error);
    }

    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
    impl_->textures.push_back(texture);
    return Texture{
        static_cast<std::uint32_t>(impl_->textures.size() - 1),
        static_cast<int>(width),
        static_cast<int>(height)};
}

void Engine::destroyTexture(Texture& texture)
{
    if (!texture || texture.id >= impl_->textures.size()) {
        texture = {};
        return;
    }

    SDL_Texture*& native = impl_->textures[texture.id];
    if (native) {
        SDL_DestroyTexture(native);
        native = nullptr;
    }
    texture = {};
}

void Engine::drawTexture(const Texture& texture, Rect destination)
{
    if (!texture || texture.id >= impl_->textures.size()) return;

    SDL_Texture* native = impl_->textures[texture.id];
    if (!native) return;

    const SDL_FRect dst{destination.x, destination.y, destination.w, destination.h};
    SDL_RenderTexture(impl_->renderer, native, nullptr, &dst);
}

const EngineConfig& Engine::config() const noexcept
{
    return impl_->config;
}

} // namespace ra
