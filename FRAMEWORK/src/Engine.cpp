#include <ra/Engine.h>

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <array>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ra {
namespace {

constexpr std::size_t keyCount = static_cast<std::size_t>(Key::Count);
constexpr std::size_t mouseButtonCount = static_cast<std::size_t>(MouseButton::Count);

std::size_t keyIndex(Key key)
{
    return static_cast<std::size_t>(key);
}

std::size_t mouseButtonIndex(MouseButton button)
{
    return static_cast<std::size_t>(button);
}

SDL_Scancode toScancode(Key key)
{
    switch (key) {
    case Key::Escape: return SDL_SCANCODE_ESCAPE;
    case Key::Space: return SDL_SCANCODE_SPACE;
    case Key::Enter: return SDL_SCANCODE_RETURN;
    case Key::Up: return SDL_SCANCODE_UP;
    case Key::Down: return SDL_SCANCODE_DOWN;
    case Key::Left: return SDL_SCANCODE_LEFT;
    case Key::Right: return SDL_SCANCODE_RIGHT;
    case Key::W: return SDL_SCANCODE_W;
    case Key::A: return SDL_SCANCODE_A;
    case Key::S: return SDL_SCANCODE_S;
    case Key::D: return SDL_SCANCODE_D;
    case Key::Q: return SDL_SCANCODE_Q;
    case Key::E: return SDL_SCANCODE_E;
    case Key::LeftShift: return SDL_SCANCODE_LSHIFT;
    case Key::RightShift: return SDL_SCANCODE_RSHIFT;
    case Key::LeftControl: return SDL_SCANCODE_LCTRL;
    case Key::RightControl: return SDL_SCANCODE_RCTRL;
    case Key::Count: break;
    }
    return SDL_SCANCODE_UNKNOWN;
}

std::uint32_t toSdlMouseButton(MouseButton button)
{
    switch (button) {
    case MouseButton::Left: return SDL_BUTTON_LMASK;
    case MouseButton::Middle: return SDL_BUTTON_MMASK;
    case MouseButton::Right: return SDL_BUTTON_RMASK;
    case MouseButton::Count: break;
    }
    return 0;
}

} // namespace

struct Engine::Impl {
    EngineConfig config{};
    SDL_Window* window{nullptr};
    SDL_Renderer* renderer{nullptr};
    bool running{false};
    float deltaSeconds{0.0f};
    std::uint64_t lastTicks{0};
    float mouseX{0.0f};
    float mouseY{0.0f};
    float mouseWheelY{0.0f};
    std::array<bool, keyCount> keys{};
    std::array<bool, keyCount> previousKeys{};
    std::array<bool, mouseButtonCount> mouseButtons{};
    std::array<bool, mouseButtonCount> previousMouseButtons{};
    std::vector<SDL_Texture*> textures{nullptr};
};

Engine::Engine(EngineConfig config)
    : impl_(std::make_unique<Impl>())
{
    impl_->config = std::move(config);

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
        throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
    }

    SDL_WindowFlags flags = 0;
    if (impl_->config.resizable) flags |= SDL_WINDOW_RESIZABLE;

    impl_->window = SDL_CreateWindow(
        impl_->config.title.c_str(),
        impl_->config.width,
        impl_->config.height,
        flags);
    if (!impl_->window) {
        SDL_Quit();
        throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
    }

    impl_->renderer = SDL_CreateRenderer(impl_->window, nullptr);
    if (!impl_->renderer) {
        SDL_DestroyWindow(impl_->window);
        impl_->window = nullptr;
        SDL_Quit();
        throw std::runtime_error(std::string("SDL_CreateRenderer failed: ") + SDL_GetError());
    }

    SDL_SetRenderVSync(impl_->renderer, impl_->config.vsync ? 1 : 0);
    SDL_SetRenderDrawBlendMode(impl_->renderer, SDL_BLENDMODE_BLEND);
}

Engine::~Engine()
{
    if (!impl_) return;
    for (SDL_Texture* texture : impl_->textures) {
        if (texture) SDL_DestroyTexture(texture);
    }
    impl_->textures.clear();
    if (impl_->renderer) SDL_DestroyRenderer(impl_->renderer);
    if (impl_->window) SDL_DestroyWindow(impl_->window);
    SDL_Quit();
}

Engine::Engine(Engine&&) noexcept = default;
Engine& Engine::operator=(Engine&&) noexcept = default;

int Engine::run(Game& game)
{
    impl_->running = true;
    impl_->lastTicks = SDL_GetTicks();
    game.onStart(*this);

    while (impl_->running) {
        impl_->previousKeys = impl_->keys;
        impl_->previousMouseButtons = impl_->mouseButtons;
        impl_->mouseWheelY = 0.0f;

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) impl_->running = false;
            if (event.type == SDL_EVENT_MOUSE_WHEEL) impl_->mouseWheelY += event.wheel.y;
        }

        const bool* keyboardState = SDL_GetKeyboardState(nullptr);
        for (std::size_t i = 0; i < keyCount; ++i) {
            const auto key = static_cast<Key>(i);
            const SDL_Scancode scancode = toScancode(key);
            impl_->keys[i] = scancode != SDL_SCANCODE_UNKNOWN && keyboardState[scancode];
        }

        const SDL_MouseButtonFlags mouseState = SDL_GetMouseState(&impl_->mouseX, &impl_->mouseY);
        for (std::size_t i = 0; i < mouseButtonCount; ++i) {
            impl_->mouseButtons[i] = (mouseState & toSdlMouseButton(static_cast<MouseButton>(i))) != 0;
        }

        const std::uint64_t now = SDL_GetTicks();
        impl_->deltaSeconds = static_cast<float>(now - impl_->lastTicks) / 1000.0f;
        impl_->lastTicks = now;
        if (impl_->deltaSeconds > 0.1f) impl_->deltaSeconds = 0.1f;

        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(impl_->window, &width, &height);
        impl_->config.width = width;
        impl_->config.height = height;

        game.onUpdate(*this, impl_->deltaSeconds);
        game.onRender(*this);
        SDL_RenderPresent(impl_->renderer);
    }

    game.onStop(*this);
    return 0;
}

void Engine::quit() { impl_->running = false; }

bool Engine::keyDown(Key key) const { return impl_->keys[keyIndex(key)]; }
bool Engine::keyPressed(Key key) const { return impl_->keys[keyIndex(key)] && !impl_->previousKeys[keyIndex(key)]; }
bool Engine::keyReleased(Key key) const { return !impl_->keys[keyIndex(key)] && impl_->previousKeys[keyIndex(key)]; }
float Engine::mouseX() const { return impl_->mouseX; }
float Engine::mouseY() const { return impl_->mouseY; }
float Engine::mouseWheelY() const { return impl_->mouseWheelY; }
bool Engine::mouseDown(MouseButton button) const { return impl_->mouseButtons[mouseButtonIndex(button)]; }
bool Engine::mousePressed(MouseButton button) const { return impl_->mouseButtons[mouseButtonIndex(button)] && !impl_->previousMouseButtons[mouseButtonIndex(button)]; }
bool Engine::mouseReleased(MouseButton button) const { return !impl_->mouseButtons[mouseButtonIndex(button)] && impl_->previousMouseButtons[mouseButtonIndex(button)]; }
int Engine::windowWidth() const { return impl_->config.width; }
int Engine::windowHeight() const { return impl_->config.height; }
float Engine::deltaSeconds() const { return impl_->deltaSeconds; }
void Engine::setWindowTitle(const std::string& title) { SDL_SetWindowTitle(impl_->window, title.c_str()); }

void Engine::clear(Color color)
{
    SDL_SetRenderDrawColor(impl_->renderer, color.r, color.g, color.b, color.a);
    SDL_RenderClear(impl_->renderer);
}

void Engine::fillRect(Rect rect, Color color)
{
    SDL_SetRenderDrawColor(impl_->renderer, color.r, color.g, color.b, color.a);
    const SDL_FRect r{rect.x, rect.y, rect.w, rect.h};
    SDL_RenderFillRect(impl_->renderer, &r);
}

void Engine::drawRect(Rect rect, Color color)
{
    SDL_SetRenderDrawColor(impl_->renderer, color.r, color.g, color.b, color.a);
    const SDL_FRect r{rect.x, rect.y, rect.w, rect.h};
    SDL_RenderRect(impl_->renderer, &r);
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
    if (!rgba || width <= 0 || height <= 0) throw std::runtime_error("createTextureRGBA received invalid image data");
    SDL_Texture* texture = SDL_CreateTexture(impl_->renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, width, height);
    if (!texture) throw std::runtime_error(std::string("SDL_CreateTexture failed: ") + SDL_GetError());
    if (!SDL_UpdateTexture(texture, nullptr, rgba, width * 4)) {
        const std::string error = SDL_GetError(); SDL_DestroyTexture(texture);
        throw std::runtime_error(std::string("SDL_UpdateTexture failed: ") + error);
    }
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    impl_->textures.push_back(texture);
    return Texture{static_cast<std::uint32_t>(impl_->textures.size()-1),width,height};
}

Texture Engine::loadTexture(const std::string& path)
{
    SDL_Texture* texture = IMG_LoadTexture(impl_->renderer, path.c_str());
    if (!texture) throw std::runtime_error(std::string("IMG_LoadTexture failed for '")+path+"': "+SDL_GetError());
    float width=0,height=0;
    if (!SDL_GetTextureSize(texture,&width,&height)) {
        const std::string error=SDL_GetError(); SDL_DestroyTexture(texture);
        throw std::runtime_error(std::string("SDL_GetTextureSize failed: ")+error);
    }
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    impl_->textures.push_back(texture);
    return Texture{static_cast<std::uint32_t>(impl_->textures.size()-1),static_cast<int>(width),static_cast<int>(height)};
}

void Engine::destroyTexture(Texture& texture)
{
    if (!texture || texture.id >= impl_->textures.size()) { texture={}; return; }
    SDL_Texture*& native=impl_->textures[texture.id];
    if(native){ SDL_DestroyTexture(native); native=nullptr; }
    texture={};
}

void Engine::drawTexture(const Texture& texture, Rect destination)
{
    if (!texture || texture.id >= impl_->textures.size()) return;
    SDL_Texture* native=impl_->textures[texture.id]; if(!native) return;
    const SDL_FRect dst{destination.x,destination.y,destination.w,destination.h};
    SDL_RenderTexture(impl_->renderer,native,nullptr,&dst);
}

const EngineConfig& Engine::config() const noexcept { return impl_->config; }

} // namespace ra
