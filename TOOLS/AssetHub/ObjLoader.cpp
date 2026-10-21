#include "AssetModel.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_map>

namespace rahub {
namespace {

std::string trim(std::string value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

int parseObjIndex(const std::string& token, int vertexCount)
{
    const auto slash = token.find('/');
    const std::string value = token.substr(0, slash);
    if (value.empty()) return -1;
    try {
        const int raw = std::stoi(value);
        if (raw > 0) return raw - 1;
        if (raw < 0) return vertexCount + raw;
    } catch (...) {
    }
    return -1;
}

std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

} // namespace

void computeBounds(ModelData& model)
{
    if (model.vertices.empty()) {
        model.minBounds = {};
        model.maxBounds = {};
        return;
    }

    const float hi = std::numeric_limits<float>::max();
    model.minBounds = {hi, hi, hi};
    model.maxBounds = {-hi, -hi, -hi};
    for (const Vec3& v : model.vertices) {
        model.minBounds.x = std::min(model.minBounds.x, v.x);
        model.minBounds.y = std::min(model.minBounds.y, v.y);
        model.minBounds.z = std::min(model.minBounds.z, v.z);
        model.maxBounds.x = std::max(model.maxBounds.x, v.x);
        model.maxBounds.y = std::max(model.maxBounds.y, v.y);
        model.maxBounds.z = std::max(model.maxBounds.z, v.z);
    }
}

std::vector<MaterialInfo> loadMtl(const std::filesystem::path& path, std::string* error)
{
    std::ifstream input(path);
    if (!input) {
        if (error) *error = "No se pudo abrir el archivo MTL.";
        return {};
    }

    std::vector<MaterialInfo> materials;
    MaterialInfo* current = nullptr;
    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        std::istringstream stream(line);
        std::string command;
        stream >> command;
        command = lower(command);

        if (command == "newmtl") {
            std::string name;
            std::getline(stream, name);
            materials.push_back({});
            current = &materials.back();
            current->name = trim(name);
        } else if (command == "kd" && current) {
            stream >> current->r >> current->g >> current->b;
            current->r = std::clamp(current->r, 0.0f, 1.0f);
            current->g = std::clamp(current->g, 0.0f, 1.0f);
            current->b = std::clamp(current->b, 0.0f, 1.0f);
        } else if (command == "map_kd" && current) {
            std::string texture;
            std::getline(stream, texture);
            texture = trim(texture);
            if (!texture.empty()) current->diffuseTexture = path.parent_path() / std::filesystem::u8path(texture);
        }
    }

    if (materials.empty() && error) *error = "El MTL no contiene materiales newmtl.";
    return materials;
}

bool loadObj(const std::filesystem::path& path, ModelData& out, std::string& error)
{
    out = {};
    out.name = path.filename().u8string();

    std::ifstream input(path);
    if (!input) {
        error = "No se pudo abrir el archivo OBJ.";
        return false;
    }

    std::unordered_map<std::string, int> materialIndex;
    int currentMaterial = -1;
    std::string line;

    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        std::istringstream stream(line);
        std::string command;
        stream >> command;

        if (command == "v") {
            Vec3 v{};
            if (stream >> v.x >> v.y >> v.z) out.vertices.push_back(v);
        } else if (command == "mtllib") {
            std::string fileName;
            std::getline(stream, fileName);
            fileName = trim(fileName);
            if (!fileName.empty()) {
                std::string mtlError;
                const auto loaded = loadMtl(path.parent_path() / std::filesystem::u8path(fileName), &mtlError);
                for (const auto& material : loaded) {
                    if (materialIndex.find(material.name) != materialIndex.end()) continue;
                    const int index = static_cast<int>(out.materials.size());
                    materialIndex[material.name] = index;
                    out.materials.push_back(material);
                }
            }
        } else if (command == "usemtl") {
            std::string name;
            std::getline(stream, name);
            name = trim(name);
            const auto found = materialIndex.find(name);
            if (found != materialIndex.end()) {
                currentMaterial = found->second;
            } else {
                MaterialInfo material{};
                material.name = name;
                currentMaterial = static_cast<int>(out.materials.size());
                materialIndex[name] = currentMaterial;
                out.materials.push_back(material);
            }
        } else if (command == "f") {
            std::vector<int> polygon;
            std::string token;
            while (stream >> token) {
                const int index = parseObjIndex(token, static_cast<int>(out.vertices.size()));
                if (index >= 0 && index < static_cast<int>(out.vertices.size())) polygon.push_back(index);
            }
            if (polygon.size() >= 3) {
                for (std::size_t i = 1; i + 1 < polygon.size(); ++i) {
                    out.triangles.push_back({
                        static_cast<std::uint32_t>(polygon[0]),
                        static_cast<std::uint32_t>(polygon[i]),
                        static_cast<std::uint32_t>(polygon[i + 1]),
                        currentMaterial
                    });
                }
            }
        }
    }

    if (out.vertices.empty()) {
        error = "El OBJ no contiene vertices.";
        return false;
    }
    if (out.triangles.empty()) {
        error = "El OBJ no contiene caras triangulables.";
        return false;
    }

    if (out.materials.empty()) {
        MaterialInfo fallback{};
        fallback.name = "Default";
        out.materials.push_back(fallback);
    }

    computeBounds(out);
    error.clear();
    return true;
}

} // namespace rahub
