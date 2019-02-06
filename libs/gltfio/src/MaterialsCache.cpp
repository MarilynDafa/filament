/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "MaterialsCache.h"

#include <filamat/MaterialBuilder.h>

#include <string>

using namespace filamat;
using namespace filament;

namespace gltfio {
namespace details {

bool MaterialsCache::EqualFn::operator()(const MaterialKey& k1, const MaterialKey& k2) const {
    return
        (k1.doubleSided == k2.doubleSided) &&
        (k1.unlit == k2.unlit) &&
        (k1.hasVertexColors == k2.hasVertexColors) &&
        (k1.alphaMode == k2.alphaMode) &&
        (k1.alphaMaskThreshold == k2.alphaMaskThreshold) &&
        (k1.baseColorUV == k2.baseColorUV) &&
        (k1.metallicRoughnessUV == k2.metallicRoughnessUV) &&
        (k1.emissiveUV == k2.emissiveUV) &&
        (k1.aoUV == k2.aoUV) &&
        (k1.normalUV == k2.normalUV);
}

MaterialsCache::MaterialsCache(Engine* engine) : mEngine(engine) {}

size_t MaterialsCache::getMaterialsCount() const noexcept {
    return mMaterials.size();
}

const Material* const* MaterialsCache::getMaterials() const noexcept {
    return mMaterials.data();
}

void MaterialsCache::destroyMaterials() {
    for (auto& iter : mCache) {
        mEngine->destroy(iter.second);
    }
    mMaterials.clear();
    mCache.clear();
}

static std::string shaderFromKey(MaterialKey config) {
    std::string shader = R"SHADER(
        void material(inout MaterialInputs material) {
    )SHADER";

    shader += "float2 normalUV = getUV" + std::to_string(config.normalUV) + "();\n";
    shader += "float2 baseColorUV = getUV" + std::to_string(config.baseColorUV) + "();\n";
    shader += "float2 metallicRoughnessUV = getUV" +
            std::to_string(config.metallicRoughnessUV) + "();\n";
    shader += "float2 aoUV = getUV" + std::to_string(config.aoUV) + "();\n";
    shader += "float2 emissiveUV = getUV" + std::to_string(config.emissiveUV) + "();\n";

    if (!config.unlit) {
        shader += R"SHADER(
            material.normal = texture(materialParams_normalMap, normalUV).xyz * 2.0 - 1.0;
            material.normal.y = -material.normal.y;
        )SHADER";
    }

    shader += R"SHADER(
        prepareMaterial(material);
        material.baseColor = texture(materialParams_baseColorMap, baseColorUV);
        material.baseColor *= materialParams.baseColorFactor;
    )SHADER";

    if (config.alphaMode == AlphaMode::TRANSPARENT) {
        shader += R"SHADER(
            material.baseColor.rgb *= material.baseColor.a;
        )SHADER";
    }

    if (!config.unlit) {
        shader += R"SHADER(
            vec4 metallicRoughness =
                    texture(materialParams_metallicRoughnessMap, metallicRoughnessUV);
            material.roughness = materialParams.roughnessFactor * metallicRoughness.g;
            material.metallic = materialParams.metallicFactor * metallicRoughness.b;
            material.ambientOcclusion = texture(materialParams_aoMap, aoUV).r;
            material.emissive = texture(materialParams_emissiveMap, emissiveUV);
            material.emissive.rgb *= materialParams.emissiveFactor.rgb;
        )SHADER";
    }

    shader += "}\n";
    return shader;
}

static Material* createMaterial(Engine* engine, const MaterialKey& config) {
    std::string shader = shaderFromKey(config);
    MaterialBuilder builder = MaterialBuilder()
            .name("material")
            .material(shader.c_str())
            .doubleSided(config.doubleSided)
            .require(VertexAttribute::UV0)
            .parameter(MaterialBuilder::SamplerType::SAMPLER_2D, "baseColorMap")
            .parameter(MaterialBuilder::UniformType::FLOAT4, "baseColorFactor")
            .parameter(MaterialBuilder::SamplerType::SAMPLER_2D, "metallicRoughnessMap")
            .parameter(MaterialBuilder::SamplerType::SAMPLER_2D, "aoMap")
            .parameter(MaterialBuilder::SamplerType::SAMPLER_2D, "emissiveMap")
            .parameter(MaterialBuilder::SamplerType::SAMPLER_2D, "normalMap")
            .parameter(MaterialBuilder::UniformType::FLOAT, "metallicFactor")
            .parameter(MaterialBuilder::UniformType::FLOAT, "roughnessFactor")
            .parameter(MaterialBuilder::UniformType::FLOAT, "normalScale")
            .parameter(MaterialBuilder::UniformType::FLOAT, "aoStrength")
            .parameter(MaterialBuilder::UniformType::FLOAT3, "emissiveFactor");

    uint8_t maxUVIndex = std::max({config.baseColorUV, config.metallicRoughnessUV,
            config.emissiveUV, config.aoUV, config.normalUV});
    if (maxUVIndex > 0) {
        builder.require(VertexAttribute::UV1);
    }

    switch(config.alphaMode) {
        case AlphaMode::MASKED:
            builder.blending(MaterialBuilder::BlendingMode::MASKED);
            builder.maskThreshold(config.alphaMaskThreshold);
            break;
        case AlphaMode::TRANSPARENT:
            builder.blending(MaterialBuilder::BlendingMode::TRANSPARENT);
            break;
        default:
            builder.blending(MaterialBuilder::BlendingMode::OPAQUE);
    }

    builder.shading(config.unlit ? Shading::UNLIT : Shading::LIT);

    Package pkg = builder.build();
    return Material::Builder().package(pkg.getData(), pkg.getSize()).build(*engine);
}

Material* MaterialsCache::getOrCreateMaterial(const MaterialKey& config) {
    auto iter = mCache.find(config);
    if (iter == mCache.end()) {
        Material* mat = createMaterial(mEngine, config);
        mCache.emplace(std::make_pair(config, mat));
        mMaterials.push_back(mat);
        return mat;
    }
    return iter->second;
}

} // namespace details
} // namespace gltfio
