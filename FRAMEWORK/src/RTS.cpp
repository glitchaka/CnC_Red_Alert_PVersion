#include <ra/RTS.h>

#include <algorithm>
#include <cmath>

namespace ra::rts {
namespace {

float lengthSquared(Vec2 value)
{
    return value.x * value.x + value.y * value.y;
}

float distanceSquared(Vec2 a, Vec2 b)
{
    return lengthSquared({a.x - b.x, a.y - b.y});
}

Bounds normalized(Bounds bounds)
{
    if (bounds.left > bounds.right) std::swap(bounds.left, bounds.right);
    if (bounds.top > bounds.bottom) std::swap(bounds.top, bounds.bottom);
    return bounds;
}

} // namespace

void Camera2D::setViewport(float width, float height)
{
    viewportWidth_ = std::max(1.0f, width);
    viewportHeight_ = std::max(1.0f, height);
    clamp();
}

void Camera2D::setWorldBounds(Bounds bounds)
{
    worldBounds_ = normalized(bounds);
    clamp();
}

void Camera2D::setPosition(Vec2 position)
{
    position_ = position;
    clamp();
}

void Camera2D::pan(Vec2 delta)
{
    position_.x += delta.x;
    position_.y += delta.y;
    clamp();
}

Vec2 Camera2D::position() const noexcept
{
    return position_;
}

Vec2 Camera2D::worldToScreen(Vec2 world) const noexcept
{
    return {world.x - position_.x, world.y - position_.y};
}

Vec2 Camera2D::screenToWorld(Vec2 screen) const noexcept
{
    return {screen.x + position_.x, screen.y + position_.y};
}

Bounds Camera2D::visibleWorld() const noexcept
{
    return {position_.x, position_.y, position_.x + viewportWidth_, position_.y + viewportHeight_};
}

void Camera2D::clamp()
{
    const float maxX = std::max(worldBounds_.left, worldBounds_.right - viewportWidth_);
    const float maxY = std::max(worldBounds_.top, worldBounds_.bottom - viewportHeight_);
    position_.x = std::clamp(position_.x, worldBounds_.left, maxX);
    position_.y = std::clamp(position_.y, worldBounds_.top, maxY);
}

World::World(Bounds bounds)
    : bounds_(normalized(bounds))
{
}

UnitId World::spawnUnit(Vec2 position, int team, float speed)
{
    Unit unit;
    unit.id = nextId_++;
    unit.position = clampToBounds(position, unit.radius);
    unit.destination = unit.position;
    unit.speed = std::max(1.0f, speed);
    unit.team = team;
    units_.push_back(unit);
    return unit.id;
}

void World::clear()
{
    units_.clear();
    nextId_ = 1;
}

void World::update(float dt)
{
    dt = std::clamp(dt, 0.0f, 0.1f);

    for (auto& unit : units_) {
        const float dx = unit.destination.x - unit.position.x;
        const float dy = unit.destination.y - unit.position.y;
        const float distance = std::sqrt(dx * dx + dy * dy);
        if (distance <= 0.5f) {
            unit.position = unit.destination;
            continue;
        }

        const float step = std::min(distance, unit.speed * dt);
        unit.position.x += dx / distance * step;
        unit.position.y += dy / distance * step;
        unit.position = clampToBounds(unit.position, unit.radius);
    }

    applySeparation(dt);
}

void World::clearSelection()
{
    for (auto& unit : units_) unit.selected = false;
}

bool World::selectPoint(Vec2 worldPoint, float pickRadius, bool additive)
{
    int bestIndex = -1;
    float bestDistance = pickRadius * pickRadius;

    for (int i = 0; i < static_cast<int>(units_.size()); ++i) {
        const float candidate = distanceSquared(worldPoint, units_[i].position);
        if (candidate <= bestDistance) {
            bestDistance = candidate;
            bestIndex = i;
        }
    }

    if (!additive) clearSelection();
    if (bestIndex < 0) return false;
    units_[bestIndex].selected = true;
    return true;
}

int World::selectRect(Bounds worldRect, bool additive)
{
    worldRect = normalized(worldRect);
    if (!additive) clearSelection();

    int count = 0;
    for (auto& unit : units_) {
        const bool inside = unit.position.x >= worldRect.left && unit.position.x <= worldRect.right &&
                            unit.position.y >= worldRect.top && unit.position.y <= worldRect.bottom;
        if (inside) {
            unit.selected = true;
            ++count;
        }
    }
    return count;
}

int World::selectedCount() const noexcept
{
    int count = 0;
    for (const auto& unit : units_) if (unit.selected) ++count;
    return count;
}

void World::commandMove(Vec2 destination, float spacing)
{
    const int count = selectedCount();
    if (count <= 0) return;

    const int columns = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<float>(count)))));
    const int rows = (count + columns - 1) / columns;
    const float totalWidth = static_cast<float>(columns - 1) * spacing;
    const float totalHeight = static_cast<float>(rows - 1) * spacing;

    int index = 0;
    for (auto& unit : units_) {
        if (!unit.selected) continue;
        const int column = index % columns;
        const int row = index / columns;
        Vec2 slot{
            destination.x + static_cast<float>(column) * spacing - totalWidth * 0.5f,
            destination.y + static_cast<float>(row) * spacing - totalHeight * 0.5f};
        unit.destination = clampToBounds(slot, unit.radius);
        ++index;
    }
}

void World::commandStop()
{
    for (auto& unit : units_) {
        if (unit.selected) unit.destination = unit.position;
    }
}

Unit* World::find(UnitId id)
{
    for (auto& unit : units_) if (unit.id == id) return &unit;
    return nullptr;
}

const Unit* World::find(UnitId id) const
{
    for (const auto& unit : units_) if (unit.id == id) return &unit;
    return nullptr;
}

const std::vector<Unit>& World::units() const noexcept
{
    return units_;
}

std::vector<Unit>& World::units() noexcept
{
    return units_;
}

Bounds World::bounds() const noexcept
{
    return bounds_;
}

Vec2 World::clampToBounds(Vec2 point, float radius) const noexcept
{
    point.x = std::clamp(point.x, bounds_.left + radius, bounds_.right - radius);
    point.y = std::clamp(point.y, bounds_.top + radius, bounds_.bottom - radius);
    return point;
}

void World::applySeparation(float dt)
{
    constexpr float strength = 36.0f;
    for (std::size_t i = 0; i < units_.size(); ++i) {
        for (std::size_t j = i + 1; j < units_.size(); ++j) {
            Vec2 delta{units_[j].position.x - units_[i].position.x,
                       units_[j].position.y - units_[i].position.y};
            float distSq = lengthSquared(delta);
            const float minDistance = units_[i].radius + units_[j].radius + 4.0f;
            if (distSq <= 0.0001f || distSq >= minDistance * minDistance) continue;

            const float distance = std::sqrt(distSq);
            const float overlap = minDistance - distance;
            const float push = std::min(overlap * 0.5f, strength * dt);
            delta.x /= distance;
            delta.y /= distance;

            units_[i].position.x -= delta.x * push;
            units_[i].position.y -= delta.y * push;
            units_[j].position.x += delta.x * push;
            units_[j].position.y += delta.y * push;
            units_[i].position = clampToBounds(units_[i].position, units_[i].radius);
            units_[j].position = clampToBounds(units_[j].position, units_[j].radius);
        }
    }
}

} // namespace ra::rts
