#pragma once

#include <cstdint>
#include <vector>

namespace ra::rts {

struct Vec2 {
    float x{0.0f};
    float y{0.0f};
};

struct Bounds {
    float left{0.0f};
    float top{0.0f};
    float right{0.0f};
    float bottom{0.0f};
};

using UnitId = std::uint32_t;

enum class UnitOrder {
    Idle,
    Move,
    Patrol
};

struct Unit {
    UnitId id{0};
    Vec2 position{};
    Vec2 destination{};
    Vec2 home{};
    float speed{110.0f};
    float radius{12.0f};
    float health{100.0f};
    float maxHealth{100.0f};
    float patrolRadius{110.0f};
    float thinkCooldown{0.0f};
    std::uint32_t aiStep{0};
    int team{0};
    bool selected{false};
    bool autonomous{false};
    UnitOrder order{UnitOrder::Idle};
};

class Camera2D {
public:
    void setViewport(float width, float height);
    void setWorldBounds(Bounds bounds);
    void setPosition(Vec2 position);
    void pan(Vec2 delta);

    Vec2 position() const noexcept;
    Vec2 worldToScreen(Vec2 world) const noexcept;
    Vec2 screenToWorld(Vec2 screen) const noexcept;
    Bounds visibleWorld() const noexcept;

private:
    void clamp();

    Vec2 position_{};
    float viewportWidth_{1280.0f};
    float viewportHeight_{720.0f};
    Bounds worldBounds_{0.0f, 0.0f, 4096.0f, 4096.0f};
};

class World {
public:
    explicit World(Bounds bounds = {0.0f, 0.0f, 2048.0f, 1536.0f});

    UnitId spawnUnit(Vec2 position, int team = 0, float speed = 110.0f);
    void clear();
    void update(float dt);

    // Replaces the simulation state with an authoritative snapshot.
    // Intended for LAN clients following a host-authoritative simulation.
    void replaceUnits(const std::vector<Unit>& units);

    void setAutonomous(UnitId id, bool enabled, float patrolRadius = 110.0f);
    void setAllAutonomous(bool enabled, float patrolRadius = 110.0f);

    void clearSelection();
    bool selectPoint(Vec2 worldPoint, float pickRadius = 24.0f, bool additive = false);
    int selectRect(Bounds worldRect, bool additive = false);
    int selectedCount() const noexcept;

    void commandMove(Vec2 destination, float spacing = 34.0f);
    void commandMoveUnits(const std::vector<UnitId>& ids, Vec2 destination, float spacing = 34.0f);
    void commandStop();
    void commandStopUnits(const std::vector<UnitId>& ids);

    Unit* find(UnitId id);
    const Unit* find(UnitId id) const;

    const std::vector<Unit>& units() const noexcept;
    std::vector<Unit>& units() noexcept;
    Bounds bounds() const noexcept;

private:
    Vec2 clampToBounds(Vec2 point, float radius) const noexcept;
    void updateAutonomous(Unit& unit, float dt);
    void applySeparation(float dt);

    Bounds bounds_{};
    std::vector<Unit> units_{};
    UnitId nextId_{1};
};

} // namespace ra::rts
