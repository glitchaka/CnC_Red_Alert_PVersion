#include "AssetModel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace rahub {
namespace {

class Blob {
public:
    bool load(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input) return false;
        const auto end = input.tellg();
        if (end <= 0) return false;
        bytes_.resize(static_cast<std::size_t>(end));
        input.seekg(0);
        input.read(reinterpret_cast<char*>(bytes_.data()), static_cast<std::streamsize>(bytes_.size()));
        return static_cast<bool>(input);
    }

    bool range(std::uint64_t offset, std::uint64_t count = 1) const
    {
        return offset <= bytes_.size() && count <= bytes_.size() - static_cast<std::size_t>(offset);
    }

    std::uint8_t u8(std::uint64_t o) const { return range(o) ? bytes_[static_cast<std::size_t>(o)] : 0; }

    std::uint16_t u16(std::uint64_t o) const
    {
        if (!range(o, 2)) return 0;
        return static_cast<std::uint16_t>(bytes_[o]) |
               static_cast<std::uint16_t>(bytes_[o + 1] << 8);
    }

    std::int16_t i16(std::uint64_t o) const { return static_cast<std::int16_t>(u16(o)); }

    std::uint32_t u32(std::uint64_t o) const
    {
        if (!range(o, 4)) return 0;
        return static_cast<std::uint32_t>(bytes_[o]) |
               (static_cast<std::uint32_t>(bytes_[o + 1]) << 8) |
               (static_cast<std::uint32_t>(bytes_[o + 2]) << 16) |
               (static_cast<std::uint32_t>(bytes_[o + 3]) << 24);
    }

    float f32(std::uint64_t o) const
    {
        const std::uint32_t raw = u32(o);
        float value = 0.0f;
        std::memcpy(&value, &raw, sizeof(value));
        return value;
    }

    void writeU32(std::uint64_t o, std::uint32_t value)
    {
        if (!range(o, 4)) return;
        bytes_[o] = static_cast<std::uint8_t>(value);
        bytes_[o + 1] = static_cast<std::uint8_t>(value >> 8);
        bytes_[o + 2] = static_cast<std::uint8_t>(value >> 16);
        bytes_[o + 3] = static_cast<std::uint8_t>(value >> 24);
    }

    std::string stringAt(std::uint32_t offset) const
    {
        if (offset == 0 || !range(offset)) return {};
        std::string result;
        for (std::size_t p = offset; p < bytes_.size() && result.size() < 1024; ++p) {
            const char c = static_cast<char>(bytes_[p]);
            if (c == '\0') break;
            result.push_back(c);
        }
        return result;
    }

    std::size_t size() const { return bytes_.size(); }

private:
    std::vector<std::uint8_t> bytes_;
};

struct BchHeader {
    std::uint8_t backward{};
    std::uint8_t forward{};
    std::uint16_t revision{};
    std::uint32_t mainOffset{};
    std::uint32_t stringOffset{};
    std::uint32_t commandOffset{};
    std::uint32_t dataOffset{};
    std::uint32_t dataExtOffset{};
    std::uint32_t relocationOffset{};
    std::uint32_t mainLength{};
    std::uint32_t stringLength{};
    std::uint32_t commandLength{};
    std::uint32_t dataLength{};
    std::uint32_t dataExtLength{};
    std::uint32_t relocationLength{};
};

bool parseHeader(const Blob& data, BchHeader& h)
{
    if (!data.range(0, 0x30)) return false;
    if (data.u8(0) != 'B' || data.u8(1) != 'C' || data.u8(2) != 'H') return false;

    h.backward = data.u8(4);
    h.forward = data.u8(5);
    h.revision = data.u16(6);
    std::uint64_t p = 8;
    h.mainOffset = data.u32(p); p += 4;
    h.stringOffset = data.u32(p); p += 4;
    h.commandOffset = data.u32(p); p += 4;
    h.dataOffset = data.u32(p); p += 4;
    if (h.backward > 0x20) { h.dataExtOffset = data.u32(p); p += 4; }
    h.relocationOffset = data.u32(p); p += 4;

    h.mainLength = data.u32(p); p += 4;
    h.stringLength = data.u32(p); p += 4;
    h.commandLength = data.u32(p); p += 4;
    h.dataLength = data.u32(p); p += 4;
    if (h.backward > 0x20) { h.dataExtLength = data.u32(p); p += 4; }
    h.relocationLength = data.u32(p);

    return data.range(h.mainOffset, h.mainLength) &&
           data.range(h.commandOffset, h.commandLength) &&
           data.range(h.relocationOffset, h.relocationLength);
}

void relocate(Blob& data, const BchHeader& h)
{
    const std::uint64_t end = std::min<std::uint64_t>(data.size(),
        static_cast<std::uint64_t>(h.relocationOffset) + h.relocationLength);

    for (std::uint64_t at = h.relocationOffset; at + 4 <= end; at += 4) {
        const std::uint32_t value = data.u32(at);
        const std::uint32_t offset = value & 0x01ffffffu;
        const std::uint8_t flags = static_cast<std::uint8_t>(value >> 25);

        auto addAt = [&](std::uint64_t target, std::uint32_t base, bool shortIndex, bool clearHigh) {
            if (!data.range(target, 4)) return;
            std::uint32_t patched = data.u32(target) + base;
            if (shortIndex) patched = (patched & 0x7fffffffu) | 0x80000000u;
            if (clearHigh) patched &= 0x7fffffffu;
            data.writeU32(target, patched);
        };

        if (flags == 0) addAt(static_cast<std::uint64_t>(h.mainOffset) + offset * 4ull, h.mainOffset, false, false);
        else if (flags == 1) addAt(static_cast<std::uint64_t>(h.mainOffset) + offset, h.stringOffset, false, false);
        else if (flags == 2) addAt(static_cast<std::uint64_t>(h.mainOffset) + offset * 4ull, h.commandOffset, false, false);
        else if (flags == 7 || flags == 0x0c) addAt(static_cast<std::uint64_t>(h.mainOffset) + offset * 4ull, h.dataOffset, false, false);

        const std::uint64_t commandWord = static_cast<std::uint64_t>(h.commandOffset) + offset * 4ull;
        if (h.backward < 6) {
            if (flags == 0x23 || flags == 0x25) addAt(commandWord, h.dataOffset, false, false);
            else if (flags == 0x26) addAt(commandWord, h.dataOffset, true, false);
            else if (flags == 0x27) addAt(commandWord, h.dataOffset, false, true);
        } else if (h.backward < 8) {
            if (flags == 0x24 || flags == 0x26) addAt(commandWord, h.dataOffset, false, false);
            else if (flags == 0x27) addAt(commandWord, h.dataOffset, true, false);
            else if (flags == 0x28) addAt(commandWord, h.dataOffset, false, true);
        } else if (h.backward < 0x21) {
            if (flags == 0x25 || flags == 0x27) addAt(commandWord, h.dataOffset, false, false);
            else if (flags == 0x28) addAt(commandWord, h.dataOffset, true, false);
            else if (flags == 0x29) addAt(commandWord, h.dataOffset, false, true);
        } else {
            if (flags == 0x25 || flags == 0x26) addAt(commandWord, h.dataOffset, false, false);
            else if (flags == 0x27) addAt(commandWord, h.dataOffset, true, false);
            else if (flags == 0x28) addAt(commandWord, h.dataOffset, false, true);
            else if (flags == 0x2b) addAt(commandWord, h.dataExtOffset, false, false);
            else if (flags == 0x2c) addAt(commandWord, h.dataExtOffset, true, false);
            else if (flags == 0x2d) addAt(commandWord, h.dataExtOffset, false, true);
        }
    }
}

class PicaReader {
public:
    PicaReader(const Blob& data, std::uint32_t offset, std::uint32_t wordCount)
    {
        std::uint64_t p = offset;
        std::uint32_t words = 0;
        std::uint32_t currentUniform = 0;
        std::vector<float> pending;

        while (words + 1 < wordCount && data.range(p, 8)) {
            const std::uint32_t parameter = data.u32(p);
            const std::uint32_t header = data.u32(p + 4);
            p += 8;
            words += 2;

            std::uint16_t id = static_cast<std::uint16_t>(header & 0xffffu);
            const std::uint32_t mask = (header >> 16) & 0x0fu;
            const std::uint32_t extra = (header >> 20) & 0x7ffu;
            const bool consecutive = (header & 0x80000000u) != 0;

            write(id, parameter, mask);
            if (id == 0x23d) break;
            if (id == 0x2c0) currentUniform = parameter & 0x7fffffffu;
            else if (id >= 0x2c1 && id <= 0x2c8) pending.push_back(bitsToFloat(commands_[id]));

            for (std::uint32_t i = 0; i < extra && words < wordCount && data.range(p, 4); ++i) {
                if (consecutive) ++id;
                const std::uint32_t extraParameter = data.u32(p);
                p += 4;
                ++words;
                write(id, extraParameter, mask);
                if (id > 0x2c0 && id <= 0x2c8) pending.push_back(bitsToFloat(commands_[id]));
            }

            if (!pending.empty() && currentUniform < uniforms_.size()) {
                auto& target = uniforms_[currentUniform];
                target.insert(target.end(), pending.begin(), pending.end());
                pending.clear();
            }

            while ((p & 7ull) != 0 && words < wordCount && data.range(p, 4)) {
                p += 4;
                ++words;
            }
        }
    }

    std::uint32_t command(std::uint16_t id) const {
        return id < commands_.size() ? commands_[id] : 0;
    }

    const std::vector<float>& uniform(std::size_t index) const {
        static const std::vector<float> empty;
        return index < uniforms_.size() ? uniforms_[index] : empty;
    }

private:
    static float bitsToFloat(std::uint32_t raw) {
        float value = 0.0f;
        std::memcpy(&value, &raw, sizeof(value));
        return value;
    }

    void write(std::uint16_t id, std::uint32_t parameter, std::uint32_t mask) {
        if (id >= commands_.size()) return;
        const std::uint32_t previous = commands_[id];
        commands_[id] = (previous & ((~mask) & 0x0fu)) | (parameter & (0xfffffff0u | mask));
    }

    std::array<std::uint32_t, 0x400> commands_{};
    std::array<std::vector<float>, 96> uniforms_{};
};

struct AttributeFormat {
    int type{};
    int components{1};
};

std::array<float, 4> readAttribute(const Blob& data, std::uint64_t& p, AttributeFormat format, bool forceUnsignedByte)
{
    std::array<float, 4> out{};
    const int components = std::clamp(format.components, 1, 4);
    const int type = forceUnsignedByte ? 1 : format.type;
    for (int i = 0; i < components; ++i) {
        if (type == 0) {
            if (!data.range(p, 1)) break;
            out[i] = static_cast<float>(static_cast<std::int8_t>(data.u8(p)));
            p += 1;
        } else if (type == 1) {
            if (!data.range(p, 1)) break;
            out[i] = static_cast<float>(data.u8(p));
            p += 1;
        } else if (type == 2) {
            if (!data.range(p, 2)) break;
            out[i] = static_cast<float>(data.i16(p));
            p += 2;
        } else {
            if (!data.range(p, 4)) break;
            out[i] = data.f32(p);
            p += 4;
        }
    }
    return out;
}

struct Bone {
    int parent{-1};
    Vec3 scale{1.0f, 1.0f, 1.0f};
    Vec3 rotation{};
    Vec3 translation{};
};

Vec3 rotateX(Vec3 p, float a) {
    const float c = std::cos(a), s = std::sin(a);
    return {p.x, p.y * c - p.z * s, p.y * s + p.z * c};
}
Vec3 rotateY(Vec3 p, float a) {
    const float c = std::cos(a), s = std::sin(a);
    return {p.x * c + p.z * s, p.y, -p.x * s + p.z * c};
}
Vec3 rotateZ(Vec3 p, float a) {
    const float c = std::cos(a), s = std::sin(a);
    return {p.x * c - p.y * s, p.x * s + p.y * c, p.z};
}

Vec3 applyBone(const std::vector<Bone>& bones, int index, Vec3 p, int depth = 0)
{
    if (index < 0 || index >= static_cast<int>(bones.size()) || depth > 64) return p;
    const Bone& bone = bones[static_cast<std::size_t>(index)];
    p.x *= bone.scale.x; p.y *= bone.scale.y; p.z *= bone.scale.z;
    p = rotateX(p, bone.rotation.x);
    p = rotateY(p, bone.rotation.y);
    p = rotateZ(p, bone.rotation.z);
    p.x += bone.translation.x; p.y += bone.translation.y; p.z += bone.translation.z;
    if (bone.parent >= 0) p = applyBone(bones, bone.parent, p, depth + 1);
    return p;
}

Vec3 applyWorld(const std::array<float, 12>& m, Vec3 p)
{
    return {
        p.x * m[0] + p.y * m[1] + p.z * m[2] + m[3],
        p.x * m[4] + p.y * m[5] + p.z * m[6] + m[7],
        p.x * m[8] + p.y * m[9] + p.z * m[10] + m[11]
    };
}

struct ModelHeader {
    std::array<float, 12> world{};
    std::uint32_t verticesOffset{};
    std::uint32_t verticesCount{};
    std::uint32_t skeletonOffset{};
    std::uint32_t skeletonCount{};
    std::uint32_t visibilityOffset{};
    std::string name;
};

bool parseModelHeader(const Blob& data, std::uint32_t offset, std::uint8_t backward, ModelHeader& out)
{
    if (!data.range(offset, 0x70)) return false;
    std::uint64_t p = offset + 4;
    for (float& value : out.world) { value = data.f32(p); p += 4; }
    p += 12; // materials table/count/name
    out.verticesOffset = data.u32(p); p += 4;
    out.verticesCount = data.u32(p); p += 4;
    p += backward > 6 ? 0x28 : 0x20;
    out.skeletonOffset = data.u32(p); p += 4;
    out.skeletonCount = data.u32(p); p += 4;
    p += 4; // skeleton name tree
    out.visibilityOffset = data.u32(p); p += 4;
    p += 4; // node count
    out.name = data.stringAt(data.u32(p));
    return out.verticesCount < 100000 && out.skeletonCount < 100000;
}

std::uint32_t resolveAddress(const Blob& data, std::uint32_t value, const BchHeader& h)
{
    if (data.range(value)) return value;
    if (data.range(static_cast<std::uint64_t>(h.dataOffset) + value)) return h.dataOffset + value;
    if (h.dataExtOffset && data.range(static_cast<std::uint64_t>(h.dataExtOffset) + value)) return h.dataExtOffset + value;
    return value;
}

struct ObjectEntry {
    std::uint16_t material{};
    bool silhouette{};
    std::uint16_t node{};
    std::uint32_t vertexCommandOffset{};
    std::uint32_t vertexCommandWords{};
    std::uint32_t facesOffset{};
    std::uint32_t facesCount{};
};

} // namespace

bool loadBch(const std::filesystem::path& path, ModelData& out, std::string& error)
{
    out = {};
    out.name = path.filename().u8string();

    Blob data;
    if (!data.load(path)) {
        error = "No se pudo abrir el archivo BCH.";
        return false;
    }

    BchHeader header{};
    if (!parseHeader(data, header)) {
        error = "Cabecera BCH invalida o version no reconocida.";
        return false;
    }

    relocate(data, header);

    if (!data.range(header.mainOffset, 12)) {
        error = "La tabla principal BCH esta fuera del archivo.";
        return false;
    }

    const std::uint32_t modelsOffset = data.u32(header.mainOffset);
    const std::uint32_t modelCount = data.u32(header.mainOffset + 4);
    if (modelCount == 0 || modelCount > 4096 || !data.range(modelsOffset, static_cast<std::uint64_t>(modelCount) * 4ull)) {
        error = "El BCH no contiene una tabla de modelos utilizable.";
        return false;
    }

    int skippedObjects = 0;
    int parsedObjects = 0;
    int highestMaterial = -1;

    for (std::uint32_t modelIndex = 0; modelIndex < modelCount; ++modelIndex) {
        const std::uint32_t modelOffset = data.u32(static_cast<std::uint64_t>(modelsOffset) + modelIndex * 4ull);
        ModelHeader model{};
        if (!parseModelHeader(data, modelOffset, header.backward, model)) continue;
        if (!model.name.empty() && modelIndex == 0) out.name = model.name;

        std::vector<Bone> bones;
        if (model.skeletonOffset && model.skeletonCount && model.skeletonCount < 4096) {
            bones.reserve(model.skeletonCount);
            for (std::uint32_t i = 0; i < model.skeletonCount; ++i) {
                const std::uint64_t base = static_cast<std::uint64_t>(model.skeletonOffset) + i * 0x64ull;
                if (!data.range(base, 0x64)) break;
                Bone bone{};
                bone.parent = data.i16(base + 4);
                bone.scale = {data.f32(base + 8), data.f32(base + 12), data.f32(base + 16)};
                bone.rotation = {data.f32(base + 20), data.f32(base + 24), data.f32(base + 28)};
                bone.translation = {data.f32(base + 32), data.f32(base + 36), data.f32(base + 40)};
                bones.push_back(bone);
            }
        }

        if (!model.verticesOffset || model.verticesCount == 0 || model.verticesCount > 100000 ||
            !data.range(model.verticesOffset, static_cast<std::uint64_t>(model.verticesCount) * 0x38ull)) {
            continue;
        }

        std::uint32_t visibility = 0xffffffffu;
        if (model.visibilityOffset && data.range(model.visibilityOffset, 4)) visibility = data.u32(model.visibilityOffset);

        std::vector<ObjectEntry> objects;
        objects.reserve(model.verticesCount);
        for (std::uint32_t i = 0; i < model.verticesCount; ++i) {
            const std::uint64_t base = static_cast<std::uint64_t>(model.verticesOffset) + i * 0x38ull;
            ObjectEntry object{};
            object.material = data.u16(base);
            const std::uint16_t flags = data.u16(base + 2);
            object.silhouette = header.backward != 8 && (flags & 1u) != 0;
            object.node = data.u16(base + 4);
            object.vertexCommandOffset = data.u32(base + 8);
            object.vertexCommandWords = data.u32(base + 12);
            object.facesOffset = data.u32(base + 16);
            object.facesCount = data.u32(base + 20);
            objects.push_back(object);
        }

        for (std::uint32_t objectIndex = 0; objectIndex < objects.size(); ++objectIndex) {
            const ObjectEntry& object = objects[objectIndex];
            if (object.silhouette) continue;
            if (object.node < 32 && (visibility & (1u << object.node)) == 0) continue;
            if (!object.vertexCommandOffset || !object.vertexCommandWords || object.vertexCommandWords > 0x10000) {
                ++skippedObjects;
                continue;
            }

            PicaReader vertexCommands(data, object.vertexCommandOffset, object.vertexCommandWords);

            std::array<float, 4> positionOffset{};
            float positionScale = 1.0f;
            float boneWeightScale = 1.0f;
            {
                const auto& u6 = vertexCommands.uniform(6);
                if (u6.size() >= 4) {
                    positionOffset[0] = u6[u6.size() - 1];
                    positionOffset[1] = u6[u6.size() - 2];
                    positionOffset[2] = u6[u6.size() - 3];
                    positionOffset[3] = u6[u6.size() - 4];
                }
                const auto& u7 = vertexCommands.uniform(7);
                if (u7.size() >= 8) {
                    std::size_t p = u7.size();
                    (void)u7[--p]; // tex0
                    (void)u7[--p]; // tex1
                    (void)u7[--p]; // tex2
                    boneWeightScale = u7[--p];
                    positionScale = u7[--p];
                }
            }

            std::uint32_t vertexBuffer = resolveAddress(data, vertexCommands.command(0x203), header);
            const std::uint32_t strideCommand = vertexCommands.command(0x205);
            std::uint32_t stride = (strideCommand >> 16) & 0xffu;
            std::uint32_t totalAttributes = strideCommand >> 28;
            if (totalAttributes == 0) totalAttributes = (vertexCommands.command(0x242) & 0x0fu) + 1;
            totalAttributes = std::min<std::uint32_t>(totalAttributes, 12);

            const std::uint64_t mainPermutation = static_cast<std::uint64_t>(vertexCommands.command(0x2bb)) |
                                                  (static_cast<std::uint64_t>(vertexCommands.command(0x2bc)) << 32);
            const std::uint64_t bufferPermutation = static_cast<std::uint64_t>(vertexCommands.command(0x204)) |
                                                    (static_cast<std::uint64_t>(strideCommand & 0xffffu) << 32);
            const std::uint64_t formatBits = static_cast<std::uint64_t>(vertexCommands.command(0x201)) |
                                             (static_cast<std::uint64_t>(vertexCommands.command(0x202)) << 32);

            if (!vertexBuffer || totalAttributes == 0) {
                ++skippedObjects;
                continue;
            }

            if (stride == 0) {
                for (std::uint32_t attribute = 0; attribute < totalAttributes; ++attribute) {
                    const std::uint32_t formatIndex = static_cast<std::uint32_t>((bufferPermutation >> (attribute * 4)) & 0x0fu);
                    const std::uint32_t nibble = static_cast<std::uint32_t>((formatBits >> (formatIndex * 4)) & 0x0fu);
                    const int components = static_cast<int>((nibble >> 2) + 1);
                    const int type = static_cast<int>(nibble & 3u);
                    const int bytesPerComponent = type <= 1 ? 1 : (type == 2 ? 2 : 4);
                    stride += static_cast<std::uint32_t>(components * bytesPerComponent);
                }
            }
            if (stride == 0 || stride > 1024) {
                ++skippedObjects;
                continue;
            }

            std::uint32_t faceGroups = object.facesCount;
            std::uint32_t oldFaceTable = 0;
            bool modernFaces = faceGroups > 0;
            if (!modernFaces) {
                const std::uint64_t old = static_cast<std::uint64_t>(model.verticesOffset) +
                                          model.verticesCount * 0x38ull + objectIndex * 0x1cull + 0x10ull;
                if (data.range(old, 8)) {
                    oldFaceTable = data.u32(old);
                    faceGroups = data.u32(old + 4);
                }
            }
            faceGroups = std::min<std::uint32_t>(faceGroups, 4096);

            bool objectProducedGeometry = false;
            for (std::uint32_t faceGroup = 0; faceGroup < faceGroups; ++faceGroup) {
                std::uint16_t skinMode = 0;
                std::vector<std::uint16_t> nodeList;
                std::uint32_t indexAddress = 0;
                bool shortIndices = true;
                std::uint32_t indexCount = 0;

                if (modernFaces) {
                    const std::uint64_t base = static_cast<std::uint64_t>(object.facesOffset) + faceGroup * 0x34ull;
                    if (!data.range(base, 0x34)) continue;
                    skinMode = data.u16(base);
                    const std::uint16_t nodeCount = std::min<std::uint16_t>(data.u16(base + 2), 20);
                    for (std::uint16_t n = 0; n < nodeCount; ++n) nodeList.push_back(data.u16(base + 4 + n * 2ull));
                    const std::uint32_t commandsOffset = data.u32(base + 0x2c);
                    const std::uint32_t commandsWords = data.u32(base + 0x30);
                    if (!commandsOffset || !commandsWords || commandsWords > 0x10000) continue;
                    PicaReader indexCommands(data, commandsOffset, commandsWords);
                    const std::uint32_t config = indexCommands.command(0x227);
                    indexAddress = resolveAddress(data, config & 0x7fffffffu, header);
                    shortIndices = (config >> 31) != 0;
                    indexCount = indexCommands.command(0x228);
                } else {
                    const std::uint64_t entry = static_cast<std::uint64_t>(oldFaceTable) + faceGroup * 8ull;
                    if (!data.range(entry, 8)) continue;
                    indexAddress = resolveAddress(data, data.u32(entry), header);
                    indexCount = data.u32(entry + 4);
                    shortIndices = true;
                }

                if (indexCount < 3 || indexCount > 10000000) continue;
                const std::uint32_t indexBytes = shortIndices ? 2u : 1u;
                if (!data.range(indexAddress, static_cast<std::uint64_t>(indexCount) * indexBytes)) continue;

                std::uint32_t inTriangle = 0;
                std::array<std::uint32_t, 3> tri{};
                for (std::uint32_t faceIndex = 0; faceIndex < indexCount; ++faceIndex) {
                    const std::uint64_t indexPos = static_cast<std::uint64_t>(indexAddress) + faceIndex * indexBytes;
                    const std::uint32_t index = shortIndices ? data.u16(indexPos) : data.u8(indexPos);
                    const std::uint64_t vertexPos = static_cast<std::uint64_t>(vertexBuffer) + index * stride;
                    if (!data.range(vertexPos, stride)) continue;

                    std::uint64_t cursor = vertexPos;
                    Vec3 position{};
                    bool hasPosition = false;
                    std::array<int, 4> boneIds{{-1, -1, -1, -1}};
                    std::array<float, 4> weights{{0, 0, 0, 0}};
                    int boneIdCount = 0;
                    int weightCount = 0;

                    for (std::uint32_t attribute = 0; attribute < totalAttributes; ++attribute) {
                        const std::uint32_t formatIndex = static_cast<std::uint32_t>((bufferPermutation >> (attribute * 4)) & 0x0fu);
                        if (formatIndex >= 16) continue;
                        const int semantic = static_cast<int>((mainPermutation >> (formatIndex * 4)) & 0x0fu);
                        const std::uint32_t nibble = static_cast<std::uint32_t>((formatBits >> (formatIndex * 4)) & 0x0fu);
                        AttributeFormat format{static_cast<int>(nibble & 3u), static_cast<int>((nibble >> 2) + 1)};
                        const auto value = readAttribute(data, cursor, format, semantic == 8);

                        if (semantic == 0) {
                            position = {
                                value[0] * positionScale + positionOffset[0],
                                value[1] * positionScale + positionOffset[1],
                                value[2] * positionScale + positionOffset[2]
                            };
                            hasPosition = true;
                        } else if (semantic == 7) {
                            boneIdCount = std::min(format.components, 4);
                            for (int c = 0; c < boneIdCount; ++c) {
                                const int listIndex = static_cast<int>(value[c]);
                                if (listIndex >= 0 && listIndex < static_cast<int>(nodeList.size())) boneIds[c] = nodeList[static_cast<std::size_t>(listIndex)];
                            }
                        } else if (semantic == 8) {
                            weightCount = std::min(format.components, 4);
                            for (int c = 0; c < weightCount; ++c) weights[c] = value[c] * boneWeightScale;
                        }
                    }

                    if (!hasPosition) continue;

                    if (!bones.empty() && !nodeList.empty()) {
                        if (skinMode == 1) {
                            if (boneIdCount == 0) {
                                boneIdCount = std::min<int>(static_cast<int>(nodeList.size()), 4);
                                for (int c = 0; c < boneIdCount; ++c) boneIds[c] = nodeList[static_cast<std::size_t>(c)];
                            }
                            if (weightCount == 0) {
                                weightCount = 1;
                                weights[0] = 1.0f;
                            }
                            Vec3 blended{};
                            float totalWeight = 0.0f;
                            const int count = std::min(boneIdCount, weightCount);
                            for (int c = 0; c < count; ++c) {
                                if (boneIds[c] < 0 || weights[c] == 0.0f) continue;
                                const Vec3 transformed = applyBone(bones, boneIds[c], position);
                                blended.x += transformed.x * weights[c];
                                blended.y += transformed.y * weights[c];
                                blended.z += transformed.z * weights[c];
                                totalWeight += weights[c];
                            }
                            if (totalWeight > 0.0001f) {
                                position = {blended.x / totalWeight, blended.y / totalWeight, blended.z / totalWeight};
                            }
                        } else if (skinMode != 0) {
                            int bone = boneIdCount > 0 ? boneIds[0] : static_cast<int>(nodeList[0]);
                            position = applyBone(bones, bone, position);
                        }
                    }

                    position = applyWorld(model.world, position);
                    tri[inTriangle++] = static_cast<std::uint32_t>(out.vertices.size());
                    out.vertices.push_back(position);
                    if (inTriangle == 3) {
                        out.triangles.push_back({tri[0], tri[1], tri[2], static_cast<int>(object.material)});
                        highestMaterial = std::max(highestMaterial, static_cast<int>(object.material));
                        inTriangle = 0;
                        objectProducedGeometry = true;
                    }
                }
            }

            if (objectProducedGeometry) ++parsedObjects;
            else ++skippedObjects;
        }
    }

    if (out.vertices.empty() || out.triangles.empty()) {
        error = "El BCH fue reconocido, pero no se pudo extraer geometria PICA200 compatible.";
        return false;
    }

    static constexpr std::array<std::array<float, 3>, 8> palette{{
        {{0.38f, 0.62f, 0.82f}}, {{0.72f, 0.49f, 0.31f}}, {{0.42f, 0.72f, 0.55f}}, {{0.67f, 0.48f, 0.77f}},
        {{0.78f, 0.68f, 0.36f}}, {{0.37f, 0.70f, 0.73f}}, {{0.74f, 0.43f, 0.49f}}, {{0.58f, 0.62f, 0.68f}}
    }};
    for (int i = 0; i <= std::max(0, highestMaterial); ++i) {
        MaterialInfo material{};
        material.name = "Material " + std::to_string(i);
        const auto& color = palette[static_cast<std::size_t>(i) % palette.size()];
        material.r = color[0]; material.g = color[1]; material.b = color[2];
        out.materials.push_back(material);
    }

    computeBounds(out);
    if (skippedObjects > 0) {
        out.warning = "BCH: " + std::to_string(parsedObjects) + " mallas visibles, " +
                      std::to_string(skippedObjects) + " omitidas por formato/flags no compatibles.";
    }
    error.clear();
    return true;
}

} // namespace rahub
