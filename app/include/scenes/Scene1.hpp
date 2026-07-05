#pragma once
/**
 * @file Scene1.hpp
 * @brief Scene presets, material helpers and active scene upload data.
 * @details
 * The presets in this file define camera positions, analytic spheres, lights,
 * mesh instances and PBR texture sets. Keeping them together makes the final
 * project easier to inspect and document.
 *
 * A ScenePreset is high-level authoring data. application.cpp turns the selected
 * preset into ActiveScene, BVH::constructScene() converts mesh models into
 * triangle/BVH buffers, and Renderer uploads everything for the shaders.
 */

#include <array>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "core/Renderer.h"
#include "core/ObjParser/ObjParser.h"

/** @brief Maximum number of analytic sphere slots uploaded to the shader. */
constexpr int MAX_SCENE_SPHERES = 10;

/**
 * @struct Sphere
 * @brief Analytic sphere primitive used by the compute shader.
 */
struct Sphere {
    RaytracingMaterial material; ///< Material used when no PBR textures are enabled.
    glm::vec3 position = glm::vec3(0.0f); ///< World-space sphere center.
    float radius = 0.0f; ///< Radius; values <= 0 mark unused slots.
    glm::ivec4 pbrTextureInfo = glm::ivec4(-1, 0, 0, 0); ///< x=texture layer, y=enabled.
    glm::vec4 pbrParameters = glm::vec4(1.0f); ///< uv scale, normal, roughness and metallic multipliers.
    glm::vec4 pbrExtraParameters = glm::vec4(0.04f, 0.0f, 0.0f, 0.0f); ///< parallax, roughness bias, metallic base and padding.
};

/** @brief Default camera transform for a scene preset. */
struct CameraPreset
{
    glm::vec3 position = glm::vec3(0.0f, 0.0f, -6.0f);
    glm::vec3 rotationDeg = glm::vec3(0.0f);
};

/** @brief Sky gradient colors and enabled state for a scene preset. */
struct SkyPreset
{
    bool enabled = true;
    glm::vec3 groundColor = glm::vec3(0.6392156862f, 0.5803921f, 0.6392156862f);
    glm::vec3 horizonColor = glm::vec3(1.0f);
    glm::vec3 zenithColor = glm::vec3(0.486274509f, 0.71372549f, 234.0f / 255.0f);
};

/** @brief Complete high-level description of one selectable scene. */
struct ScenePreset
{
    std::string name;
    std::vector<ModelInstance> models;
    std::array<Sphere, MAX_SCENE_SPHERES> spheres {};
    std::array<PBRTextureSet, MAX_SCENE_SPHERES> spherePBRTextures {};
    CameraPreset camera;
    SkyPreset sky;
};

/**
 * @brief Creates a diffuse/default material with optional glossy bounce chance.
 */
inline RaytracingMaterial makeDiffuseMaterial(
    const glm::vec3& color,
    float smoothness = 0.0f,
    float specularProbability = 0.0f)
{
    RaytracingMaterial material;
    material.setType(RaytracingMaterial::Type::Default);
    material.setColor(color);
    material.setEmissionColor(glm::vec3(0.0f));
    material.setEmissionStrength(0.0f);
    material.setSpecularColor(glm::vec3(1.0f));
    material.setSmoothness(smoothness);
    material.setAbsorptionColor(glm::vec3(0.0f));
    material.setAbsorptionStrength(0.0f);
    material.setSpecularProbability(specularProbability);
    material.setIOR(1.0f);
    material.setCheckerScale(4.0f);
    return material;
}

/** @brief Creates a warm brass-like material preset. */
inline RaytracingMaterial makeBrassMaterial()
{
    RaytracingMaterial material = makeDiffuseMaterial(
        glm::vec3(0.780f, 0.568f, 0.114f),
        0.82f,
        0.22f);
    material.setSpecularColor(glm::vec3(1.0f, 0.92f, 0.70f));
    return material;
}

/** @brief Creates an emissive material used by light spheres. */
inline RaytracingMaterial makeLightMaterial(const glm::vec3& emissionColor, float emissionStrength)
{
    RaytracingMaterial material = makeDiffuseMaterial(glm::vec3(0.0f));
    material.setEmissionColor(emissionColor);
    material.setEmissionStrength(emissionStrength);
    return material;
}

/**
 * @brief Creates a glass/refractive material.
 * @param tint Base transmission color.
 * @param ior Index of refraction.
 * @param smoothness Reflection/refraction direction sharpness.
 * @param specularProbability Probability of specular reflection.
 * @param absorption Beer-Lambert absorption color.
 * @param absorptionStrength Absorption density.
 */
inline RaytracingMaterial makeGlassMaterial(
    const glm::vec3& tint,
    float ior = 1.5f,
    float smoothness = 1.0f,
    float specularProbability = 1.0f,
    const glm::vec3& absorption = glm::vec3(0.0f),
    float absorptionStrength = 0.0f)
{
    RaytracingMaterial material = makeDiffuseMaterial(tint, smoothness, specularProbability);
    material.setType(RaytracingMaterial::Type::Glass);
    material.setSpecularColor(glm::vec3(1.0f));
    material.setIOR(ior);
    material.setAbsorptionColor(absorption);
    material.setAbsorptionStrength(absorptionStrength);
    return material;
}

/** @brief Creates one analytic sphere slot. */
inline Sphere makeSphere(const glm::vec3& position, float radius, const RaytracingMaterial& material)
{
    Sphere sphere;
    sphere.position = position;
    sphere.radius = radius;
    sphere.material = material;
    return sphere;
}

/** @brief Creates a mesh model from an already composed transform. */
inline ModelInstance makeModel(
    const std::string& meshPath,
    const glm::mat4& localToWorld,
    const RaytracingMaterial& material)
{
    ModelInstance model;
    model.meshPath = meshPath;
    model.localToWorld = localToWorld;
    model.material = material;
    return model;
}

/**
 * @brief Composes a local-to-world model matrix.
 * @details Matrix convention: M = T * Rx * Ry * Rz * S.
 */
inline glm::mat4 composeTransform(const glm::vec3& position, const glm::vec3& rotationDeg, const glm::vec3& scale)
{
    glm::mat4 transform(1.0f);
    transform = glm::translate(transform, position);
    transform = glm::rotate(transform, glm::radians(rotationDeg.x), glm::vec3(1.0f, 0.0f, 0.0f));
    transform = glm::rotate(transform, glm::radians(rotationDeg.y), glm::vec3(0.0f, 1.0f, 0.0f));
    transform = glm::rotate(transform, glm::radians(rotationDeg.z), glm::vec3(0.0f, 0.0f, 1.0f));
    transform = glm::scale(transform, scale);
    return transform;
}

/** @brief Recomputes a model matrix after GUI transform edits. */
inline void updateModelTransform(ModelInstance& model)
{
    model.localToWorld = composeTransform(model.position, model.rotationDeg, model.scale);
}

/** @brief Creates a mesh model from editable transform components. */
inline ModelInstance makeModel(
    const std::string& meshPath,
    const glm::vec3& position,
    const glm::vec3& rotationDeg,
    const glm::vec3& scale,
    const RaytracingMaterial& material)
{
    ModelInstance model;
    model.meshPath = meshPath;
    model.position = position;
    model.rotationDeg = rotationDeg;
    model.scale = scale;
    model.material = material;
    updateModelTransform(model);
    return model;
}

/** @brief Packs common PBR parameters into the vec4 consumed by the shader. */
inline glm::vec4 makePBRParameters(const PBRTextureSet& textures)
{
    return glm::vec4(
        textures.uvScale,
        textures.normalStrength,
        textures.roughnessMultiplier,
        textures.metallicMultiplier);
}

/** @brief Packs extra PBR controls into the vec4 consumed by the shader. */
inline glm::vec4 makePBRExtraParameters(const PBRTextureSet& textures)
{
    return glm::vec4(
        textures.parallaxStrength,
        textures.roughnessBias,
        textures.metallicBase,
        0.0f);
}

/** @brief Builds a unique key used to deduplicate PBR texture array layers. */
inline std::string makePBRTextureSetKey(const PBRTextureSet& textures)
{
    return textures.albedoPath + "|" + textures.normalPath + "|" + textures.roughnessPath + "|" +
        textures.metallicPath + "|" + textures.aoPath + "|" + textures.heightPath;
}

/**
 * @brief Creates a PBR texture set from the project resource naming convention.
 */
inline PBRTextureSet makePBRTextureSet(
    const std::string& folder,
    const std::string& textureName,
    float uvScale = 1.0f,
    float parallaxStrength = 0.04f)
{
    // The downloaded PBR materials use a consistent file naming convention. This
    // helper keeps the scene presets readable and avoids repeating long paths.
    const std::string root = std::string(APP_RESOURCES_PATH) + "textures/" + folder + "/" + textureName;
    PBRTextureSet textures;
    textures.albedoPath = root + "_albedo.png";
    textures.normalPath = root + "_normal-ogl.png";
    textures.roughnessPath = root + "_roughness.png";
    textures.metallicPath = root + "_metallic.png";
    textures.aoPath = root + "_ao.png";
    textures.heightPath = root + "_height.png";
    textures.uvScale = uvScale;
    textures.parallaxStrength = parallaxStrength;
    return textures;
}

/** @brief Adds a PBR-textured analytic sphere to a scene preset. */
inline void setPBRSphere(
    ScenePreset& scene,
    int index,
    const glm::vec3& position,
    float radius,
    const PBRTextureSet& textures)
{
    scene.spheres[index] = makeSphere(position, radius, makeDiffuseMaterial(glm::vec3(1.0f), 0.0f, 0.0f));
    scene.spheres[index].pbrTextureInfo = glm::ivec4(-1, textures.enabled() ? 1 : 0, 0, 0);
    scene.spheres[index].pbrParameters = makePBRParameters(textures);
    scene.spheres[index].pbrExtraParameters = makePBRExtraParameters(textures);
    scene.spherePBRTextures[index] = textures;
}

inline ModelInstance makePBRModel(
    const std::string& meshPath,
    const glm::vec3& position,
    const glm::vec3& rotationDeg,
    const glm::vec3& scale,
    const PBRTextureSet& textures)
{
    // PBR models still receive a regular diffuse material as a fallback. The
    // shader uses the texture set when usePBRTextures is enabled in ModelInfo.
    ModelInstance model = makeModel(
        meshPath,
        position,
        rotationDeg,
        scale,
        makeDiffuseMaterial(glm::vec3(1.0f), 0.0f, 0.0f));
    model.pbrTextures = textures;
    return model;
}

inline std::vector<ScenePreset> buildScenePresets()
{
    // These are demonstration scenes, not runtime-loaded level files. Each block
    // defines one option in the scene selector: mesh instances, analytic spheres,
    // emissive spheres used as lights, initial camera and optional sky colors.
    //
    // When the selected preset changes, application.cpp rebuilds the active
    // scene and recreates Renderer because the mesh/BVH buffers may be different.
    std::vector<ScenePreset> scenes;

    {
        ScenePreset scene;
        scene.name = "Glass Dragon";
        scene.models.push_back(makeModel(
            APP_RESOURCES_PATH "models/Dragon_80K.obj",
            glm::vec3(0.0f, -0.15f, 6.25f),
            glm::vec3(0.0f, -35.0f, 0.0f),
            glm::vec3(6.75f),
            makeGlassMaterial(
                glm::vec3(1.0f),
                1.5f,
                0.85f,
                0.888f,
                glm::vec3(0.9137255f, 0.7906576f, 0.24705878f),
                1.5f)));

        scene.spheres[0] = makeSphere(glm::vec3(0.0f, -1002.0f, 6.0f), 1000.0f, makeDiffuseMaterial(glm::vec3(0.80f, 0.79f, 0.75f), 0.02f, 0.02f));
        scene.spheres[1] = makeSphere(glm::vec3(0.0f, 7.0f, 5.5f), 2.1f, makeLightMaterial(glm::vec3(1.0f, 0.96f, 0.92f), 12.0f));
        scene.camera.position = glm::vec3(0.35f, 1.45f, -2.35f);
        scene.camera.rotationDeg = glm::vec3(-8.0f, 0.0f, 0.0f);
        scenes.push_back(scene);
    }

    {
        ScenePreset scene;
        scene.name = "Brass Dragon";
        scene.models.push_back(makeModel(
            APP_RESOURCES_PATH "models/Dragon_80K.obj",
            glm::vec3(0.0f, -0.15f, 6.25f),
            glm::vec3(0.0f, -35.0f, 0.0f),
            glm::vec3(6.75f),
            makeBrassMaterial()));

        scene.spheres[0] = makeSphere(glm::vec3(0.0f, -1002.0f, 6.0f), 1000.0f, makeDiffuseMaterial(glm::vec3(0.77f, 0.73f, 0.68f), 0.03f, 0.03f));
        scene.spheres[1] = makeSphere(glm::vec3(0.0f, 7.0f, 5.5f), 1.8f, makeLightMaterial(glm::vec3(1.0f, 0.96f, 0.90f), 10.0f));
        scene.camera.position = glm::vec3(0.35f, 1.45f, -2.35f);
        scene.camera.rotationDeg = glm::vec3(-8.0f, 0.0f, 0.0f);
        scene.sky.enabled = false;
        scenes.push_back(scene);
    }

    {
        ScenePreset scene;
        scene.name = "Glass Balls";
        scene.models.push_back(makeModel(
            APP_RESOURCES_PATH "models/cube_rounded2.obj",
            glm::vec3(0.0f, -0.7f, 7.0f),
            glm::vec3(0.0f, 0.0f, 0.0f),
            glm::vec3(7.5f, 0.8f, 7.5f),
            makeDiffuseMaterial(glm::vec3(0.75f, 0.73f, 0.70f), 0.04f, 0.05f)));

        scene.spheres[0] = makeSphere(glm::vec3(-2.4f, 0.4f, 6.8f), 1.0f, makeGlassMaterial(glm::vec3(1.0f), 1.6f, 1.0f, 1.0f));
        scene.spheres[1] = makeSphere(glm::vec3(0.0f, 0.4f, 7.2f), 1.0f, makeGlassMaterial(glm::vec3(0.82f, 0.68f, 0.24f), 1.6f, 1.0f, 1.0f));
        scene.spheres[2] = makeSphere(glm::vec3(2.4f, 0.4f, 6.8f), 1.0f, makeGlassMaterial(glm::vec3(0.75f, 0.95f, 1.0f), 1.6f, 1.0f, 1.0f));
        scene.spheres[3] = makeSphere(glm::vec3(0.0f, 6.0f, 4.5f), 1.8f, makeLightMaterial(glm::vec3(1.0f), 10.0f));
        scene.camera.position = glm::vec3(0.0f, 1.3f, -2.6f);
        scene.camera.rotationDeg = glm::vec3(-10.0f, 0.0f, 0.0f);
        scenes.push_back(scene);
    }

    {
        ScenePreset scene;
        scene.name = "Sphere Refract";
        scene.models.push_back(makeModel(
            APP_RESOURCES_PATH "models/cube_rounded2.obj",
            glm::vec3(0.0f, -0.8f, 8.0f),
            glm::vec3(0.0f, 0.0f, 0.0f),
            glm::vec3(8.5f, 0.8f, 8.5f),
            makeDiffuseMaterial(glm::vec3(0.88f, 0.86f, 0.80f), 0.0f, 0.0f)));

        scene.spheres[0] = makeSphere(glm::vec3(0.0f, 0.55f, 7.2f), 1.1f, makeGlassMaterial(glm::vec3(1.0f), 1.6f, 1.0f, 0.517f));
        scene.spheres[1] = makeSphere(glm::vec3(-2.2f, 0.35f, 6.8f), 0.9f, makeDiffuseMaterial(glm::vec3(0.95f, 0.44f, 0.40f), 0.65f, 0.15f));
        scene.spheres[2] = makeSphere(glm::vec3(2.2f, 0.35f, 6.8f), 0.9f, makeDiffuseMaterial(glm::vec3(0.38f, 0.62f, 0.95f), 0.41f, 0.07f));
        scene.spheres[3] = makeSphere(glm::vec3(0.0f, 6.5f, 4.5f), 2.0f, makeLightMaterial(glm::vec3(1.0f, 0.98f, 0.96f), 12.0f));
        scene.camera.position = glm::vec3(0.0f, 1.15f, -3.1f);
        scene.camera.rotationDeg = glm::vec3(-8.5f, 0.0f, 0.0f);
        scenes.push_back(scene);
    }

    {
        ScenePreset scene;
        scene.name = "PBR Texture Gallery";

        const float sphereRadius = 1.05f;
        setPBRSphere(
            scene,
            0,
            glm::vec3(-3.1f, 1.25f, 6.8f),
            sphereRadius,
            makePBRTextureSet("alley-brick-wall-bl", "alley-brick-wall", 3.0f, 0.055f));
        setPBRSphere(
            scene,
            1,
            glm::vec3(0.0f, 1.25f, 6.8f),
            sphereRadius,
            makePBRTextureSet("limestone-cliffs-bl", "limestone-cliffs", 3.0f, 0.05f));
        setPBRSphere(
            scene,
            2,
            glm::vec3(3.1f, 1.25f, 6.8f),
            sphereRadius,
            makePBRTextureSet("sloppy-mortar-stone-wall-bl", "sloppy-mortar-stone-wall", 4.0f, 0.055f));
        setPBRSphere(
            scene,
            3,
            glm::vec3(-3.1f, 1.25f, 9.4f),
            sphereRadius,
            makePBRTextureSet("chiseled-cobble-bl", "chiseled-cobble", 4.5f, 0.055f));
        setPBRSphere(
            scene,
            4,
            glm::vec3(0.0f, 1.25f, 9.4f),
            sphereRadius,
            makePBRTextureSet("square-block-vegetation-bl", "square-blocks-vegetation", 3.6f, 0.05f));
        setPBRSphere(
            scene,
            5,
            glm::vec3(3.1f, 1.25f, 9.4f),
            sphereRadius,
            makePBRTextureSet("soft-blanket-bl", "soft-blanket", 3.5f, 0.035f));

        scene.spheres[6] = makeSphere(
            glm::vec3(0.0f, -1001.0f, 8.0f),
            1000.0f,
            makeDiffuseMaterial(glm::vec3(0.32f, 0.33f, 0.35f), 0.16f, 0.04f));
        scene.spheres[7] = makeSphere(
            glm::vec3(-4.5f, 6.5f, 3.0f),
            1.5f,
            makeLightMaterial(glm::vec3(1.0f, 0.86f, 0.70f), 24.0f));
        scene.spheres[8] = makeSphere(
            glm::vec3(5.5f, 4.0f, 4.5f),
            1.25f,
            makeLightMaterial(glm::vec3(0.58f, 0.76f, 1.0f), 17.0f));
        scene.spheres[9] = makeSphere(
            glm::vec3(1.8f, 4.7f, 10.2f),
            1.7f,
            makeLightMaterial(glm::vec3(1.0f, 0.96f, 0.88f), 36.0f));
        scene.camera.position = glm::vec3(0.0f, 1.75f, -3.2f);
        scene.camera.rotationDeg = glm::vec3(-2.5f, 0.0f, 0.0f);
        scene.sky.groundColor = glm::vec3(0.18f, 0.20f, 0.24f);
        scene.sky.horizonColor = glm::vec3(0.78f, 0.80f, 0.84f);
        scene.sky.zenithColor = glm::vec3(0.30f, 0.42f, 0.62f);
        scenes.push_back(scene);
    }

    {
        ScenePreset scene;
        scene.name = "Splash";
        scene.models.push_back(makeModel(
            APP_RESOURCES_PATH "models/Water.fbx",
            glm::vec3(0.0f, -0.3f, 7.2f),
            glm::vec3(0.0f, 180.0f, 0.0f),
            glm::vec3(0.27f),
            makeGlassMaterial(glm::vec3(0.80f, 0.95f, 1.0f), 1.5f, 1.0f, 1.0f)));

        scene.spheres[0] = makeSphere(glm::vec3(0.0f, -1002.0f, 6.0f), 1000.0f, makeDiffuseMaterial(glm::vec3(0.84f, 0.52f, 0.34f), 0.01f, 0.0f));
        scene.spheres[1] = makeSphere(glm::vec3(0.0f, 6.0f, 4.0f), 2.0f, makeLightMaterial(glm::vec3(0.38f, 0.82f, 1.0f), 14.0f));
        scene.camera.position = glm::vec3(0.0f, 1.0f, -3.0f);
        scene.camera.rotationDeg = glm::vec3(-7.0f, 0.0f, 0.0f);
        scene.sky.zenithColor = glm::vec3(0.36f, 0.62f, 0.92f);
        scenes.push_back(scene);
    }

    {
        ScenePreset scene;
        scene.name = "Stanford Dragon (GLB)";
        scene.models.push_back(makeModel(
            APP_RESOURCES_PATH "models/stanford_dragon_pbr.glb",
            glm::vec3(-0.005f, 0.0f, 7.0f),
            glm::vec3(0.0f, -20.0f, 0.0f),
            glm::vec3(0.06f),
            makeBrassMaterial()));

        scene.spheres[0] = makeSphere(glm::vec3(0.0f, -1000.0f, 7.0f), 1000.0f, makeDiffuseMaterial(glm::vec3(0.26f, 0.27f, 0.30f), 0.12f, 0.05f));
        scene.spheres[1] = makeSphere(glm::vec3(-4.5f, 7.5f, 1.5f), 1.5f, makeLightMaterial(glm::vec3(1.0f, 0.78f, 0.56f), 20.0f));
        scene.spheres[2] = makeSphere(glm::vec3(5.0f, 4.0f, 8.0f), 1.1f, makeLightMaterial(glm::vec3(0.48f, 0.68f, 1.0f), 14.0f));
        scene.camera.position = glm::vec3(0.0f, 2.5f, -2.5f);
        scene.camera.rotationDeg = glm::vec3(-4.0f, 0.0f, 0.0f);
        scene.sky.groundColor = glm::vec3(0.08f, 0.09f, 0.12f);
        scene.sky.horizonColor = glm::vec3(0.42f, 0.48f, 0.58f);
        scene.sky.zenithColor = glm::vec3(0.08f, 0.13f, 0.24f);
        scenes.push_back(scene);
    }

    {
        ScenePreset scene;
        scene.name = "Erato";
        scene.models.push_back(makeModel(
            APP_RESOURCES_PATH "models/erato/erato.obj",
            glm::vec3(-0.423f, 0.229f, 6.0f),
            glm::vec3(0.0f, 18.0f, 0.0f),
            glm::vec3(0.18f),
            makeDiffuseMaterial(glm::vec3(0.78f, 0.76f, 0.70f), 0.55f, 0.14f)));

        scene.spheres[0] = makeSphere(glm::vec3(0.0f, -1000.0f, 6.0f), 1000.0f, makeDiffuseMaterial(glm::vec3(0.18f, 0.19f, 0.22f), 0.08f, 0.03f));
        scene.spheres[1] = makeSphere(glm::vec3(-4.0f, 7.0f, 1.5f), 1.2f, makeLightMaterial(glm::vec3(1.0f, 0.82f, 0.62f), 22.0f));
        scene.spheres[2] = makeSphere(glm::vec3(4.5f, 4.0f, 7.0f), 0.9f, makeLightMaterial(glm::vec3(0.52f, 0.68f, 1.0f), 13.0f));
        scene.camera.position = glm::vec3(0.0f, 2.7f, -3.0f);
        scene.camera.rotationDeg = glm::vec3(-2.0f, 0.0f, 0.0f);
        scene.sky.groundColor = glm::vec3(0.10f, 0.10f, 0.12f);
        scene.sky.horizonColor = glm::vec3(0.55f, 0.57f, 0.62f);
        scene.sky.zenithColor = glm::vec3(0.18f, 0.23f, 0.34f);
        scenes.push_back(scene);
    }

    return scenes;
}

/**
 * @struct ActiveScene
 * @brief Mutable scene state currently bound to the renderer.
 * @details
 * ScenePreset is immutable template data. ActiveScene stores the editable copy
 * used by the GUI and exposes SceneData, the raw upload block consumed by
 * Renderer::updateSceneBuffers().
 */
struct ActiveScene
{
    ScenePreset preset {}; ///< Current scene preset copy.
    std::array<Sphere, MAX_SCENE_SPHERES> spheres {}; ///< Editable sphere array uploaded to the UBO.
    std::array<PBRTextureSet, MAX_SCENE_SPHERES> spherePBRTextures {}; ///< PBR textures assigned to analytic spheres.
    std::vector<PBRTextureSet> additionalPBRTextureSets {}; ///< Texture sets appended after mesh texture layers.
    SceneData sceneData {}; ///< Raw upload descriptor used by Renderer.

    ActiveScene()
    {
        syncSceneData();
    }

    ActiveScene(const ActiveScene& other)
        : preset(other.preset),
          spheres(other.spheres),
          spherePBRTextures(other.spherePBRTextures),
          additionalPBRTextureSets(other.additionalPBRTextureSets)
    {
        syncSceneData();
    }

    ActiveScene& operator=(const ActiveScene& other)
    {
        if (this != &other) {
            preset = other.preset;
            spheres = other.spheres;
            spherePBRTextures = other.spherePBRTextures;
            additionalPBRTextureSets = other.additionalPBRTextureSets;
            syncSceneData();
        }
        return *this;
    }

    ActiveScene(ActiveScene&& other) noexcept
        : preset(std::move(other.preset)),
          spheres(std::move(other.spheres)),
          spherePBRTextures(std::move(other.spherePBRTextures)),
          additionalPBRTextureSets(std::move(other.additionalPBRTextureSets))
    {
        syncSceneData();
    }

    ActiveScene& operator=(ActiveScene&& other) noexcept
    {
        if (this != &other) {
            preset = std::move(other.preset);
            spheres = std::move(other.spheres);
            spherePBRTextures = std::move(other.spherePBRTextures);
            additionalPBRTextureSets = std::move(other.additionalPBRTextureSets);
            syncSceneData();
        }
        return *this;
    }

    /**
     * @brief Refreshes GPU-facing sphere data after GUI edits.
     */
    void syncSceneData()
    {
        // SceneData stores raw pointers for Renderer uploads, so it must be
        // refreshed after copies/moves and after GUI edits. The actual object
        // storage remains owned by ActiveScene.
        for (int i = 0; i < MAX_SCENE_SPHERES; ++i) {
            if (spheres[i].radius > 0.0f && spherePBRTextures[i].enabled()) {
                spheres[i].pbrTextureInfo.y = 1;
                spheres[i].pbrParameters = makePBRParameters(spherePBRTextures[i]);
                spheres[i].pbrExtraParameters = makePBRExtraParameters(spherePBRTextures[i]);
            }
            else {
                spheres[i].pbrTextureInfo = glm::ivec4(-1, 0, 0, 0);
                spheres[i].pbrParameters = glm::vec4(1.0f);
                spheres[i].pbrExtraParameters = glm::vec4(0.04f, 0.0f, 0.0f, 0.0f);
            }
        }
        sceneData.sceneObjects = spheres.data();
        sceneData.numberOfObjects = MAX_SCENE_SPHERES;
        sceneData.size = MAX_SCENE_SPHERES * sizeof(Sphere);
        sceneData.pbrTextureSets = &additionalPBRTextureSets;
    }

    /**
     * @brief Resolves texture array layers for PBR spheres.
     * @param baseTextureSets Texture sets already used by mesh ModelInstances.
     */
    void syncPBRTextureLayers(const std::vector<PBRTextureSet>& baseTextureSets)
    {
        // Mesh PBR textures are assigned layers first by BVH::constructScene().
        // Sphere textures are then matched against those layers or appended. The
        // shader only needs the final layer index stored in pbrTextureInfo.x.
        additionalPBRTextureSets.clear();

        std::vector<std::string> layerKeys;
        layerKeys.reserve(baseTextureSets.size() + MAX_SCENE_SPHERES);
        for (const PBRTextureSet& textures : baseTextureSets) {
            layerKeys.push_back(makePBRTextureSetKey(textures));
        }

        for (int i = 0; i < MAX_SCENE_SPHERES; ++i) {
            if (spheres[i].radius <= 0.0f || !spherePBRTextures[i].enabled()) {
                continue;
            }

            const std::string key = makePBRTextureSetKey(spherePBRTextures[i]);
            int layer = -1;
            for (int existingLayer = 0; existingLayer < static_cast<int>(layerKeys.size()); ++existingLayer) {
                if (layerKeys[existingLayer] == key) {
                    layer = existingLayer;
                    break;
                }
            }

            if (layer < 0) {
                layer = static_cast<int>(layerKeys.size());
                layerKeys.push_back(key);
                additionalPBRTextureSets.push_back(spherePBRTextures[i]);
            }

            spheres[i].pbrTextureInfo = glm::ivec4(layer, 1, 0, 0);
        }

        syncSceneData();
    }
};

/**
 * @brief Creates a mutable active scene from a preset.
 * @param preset Source preset selected in the scene selector.
 * @return Active scene with upload pointers synchronized.
 */
inline ActiveScene makeActiveScene(const ScenePreset& preset)
{
    ActiveScene scene;
    scene.preset = preset;
    scene.spheres = preset.spheres;
    scene.spherePBRTextures = preset.spherePBRTextures;
    scene.syncSceneData();
    return scene;
}
