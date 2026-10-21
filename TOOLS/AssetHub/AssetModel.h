#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace rahub {

struct Vec3 {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
};

struct MaterialInfo {
    std::string name;
    float r{0.62f};
    float g{0.68f};
    float b{0.75f};
    std::filesystem::path diffuseTexture;
};

struct Triangle {
    std::uint32_t a{0};
    std::uint32_t b{0};
    std::uint32_t c{0};
    int material{-1};
};

struct ModelData {
    std::string name;
    std::vector<Vec3> vertices;
    std::vector<Triangle> triangles;
    std::vector<MaterialInfo> materials;
    Vec3 minBounds{};
    Vec3 maxBounds{};
    std::string warning;

    explicit operator bool() const noexcept {
        return !vertices.empty() && !triangles.empty();
    }
};

void computeBounds(ModelData& model);
std::vector<MaterialInfo> loadMtl(const std::filesystem::path& path, std::string* error = nullptr);
bool loadObj(const std::filesystem::path& path, ModelData& out, std::string& error);
bool loadBch(const std::filesystem::path& path, ModelData& out, std::string& error);

} // namespace rahub
