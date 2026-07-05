/**
 * @file ObjParser.h
 * @brief Shared geometry, material, PBR and BVH declarations.
 * @details
 * This header is the bridge between the scene built in C++ and the ray tracing
 * shader. The app fills these structures on the CPU, Renderer uploads them to
 * OpenGL buffers, and ComputeRayTracing.comp reads the same data on the GPU.
 *
 * Some values are grouped into vec4 fields. That is mostly for OpenGL buffer
 * alignment: it keeps the C++ layout close to the GLSL layout and avoids the
 * shader reading the wrong bytes.
 */

// standard
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <queue>
#include <sstream>
#include <string>
#include <vector>

// third-party
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/string_cast.hpp>

#ifndef RTX_MATERIAL
#define RTX_MATERIAL
/**
 * @struct RaytracingMaterial
 * @brief Material parameters shared by the CPU and GLSL path tracer.
 *
 * when a ray hits an object: 
 * base color, emission, shininess, surface response, glass/refraction,
 * procedural patterns and absorption.
 * Scene1.hpp creates and edits these values; Renderer uploads them; TraceRay() 
 * in ComputeRayTracing.comp decides how the ray bounces using this data.
 *
 * The fields are grouped as vec4 values because GPU buffers align vec3 values
 * to 16 bytes. Keeping the CPU and GLSL structs equivalent avoids padding bugs.
 */
struct RaytracingMaterial
{
    enum class Type : int
    {
        Default = 0,
        CheckerPattern = 1,
        Glass = 2
    };

    glm::vec4 color_type = glm::vec4(1.0f, 1.0f, 1.0f, static_cast<float>(Type::Default)); ///< RGB base color and material type in w.
    glm::vec4 emission_strength = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f); ///< RGB emission color and strength in w.
    glm::vec4 specular_smoothness = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f); ///< RGB specular color and smoothness in w.
    glm::vec4 surface_params = glm::vec4(0.0f); ///< General surface controls: specular probability in x.
    glm::vec4 absorption_strength = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f); ///< Beer-Lambert absorption color and density in w.
    glm::vec4 glass_params = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f); ///< Glass-only controls: IOR in x.
    glm::vec4 pattern_params = glm::vec4(4.0f, 0.0f, 0.0f, 0.0f); ///< Procedural pattern controls: checker scale in x.

    inline void setColor(const glm::vec3& value) { color_type = glm::vec4(value, color_type.w); }
    inline glm::vec3 getColor() const { return glm::vec3(color_type); }

    inline void setType(Type value) { color_type.w = static_cast<float>(value); }
    inline Type getType() const { return static_cast<Type>(static_cast<int>(std::round(color_type.w))); }

    inline void setEmissionColor(const glm::vec3& value) { emission_strength = glm::vec4(value, emission_strength.w); }
    inline glm::vec3 getEmissionColor() const { return glm::vec3(emission_strength); }

    inline void setEmissionStrength(float value) { emission_strength.w = value; }
    inline float getEmissionStrength() const { return emission_strength.w; }

    inline void setSpecularColor(const glm::vec3& value) { specular_smoothness = glm::vec4(value, specular_smoothness.w); }
    inline glm::vec3 getSpecularColor() const { return glm::vec3(specular_smoothness); }

    inline void setSmoothness(float value) { specular_smoothness.w = value; }
    inline float getSmoothness() const { return specular_smoothness.w; }

    inline void setAbsorptionColor(const glm::vec3& value) { absorption_strength = glm::vec4(value, absorption_strength.w); }
    inline glm::vec3 getAbsorptionColor() const { return glm::vec3(absorption_strength); }

    inline void setAbsorptionStrength(float value) { absorption_strength.w = value; }
    inline float getAbsorptionStrength() const { return absorption_strength.w; }

    inline void setSpecularProbability(float value) { surface_params.x = value; }
    inline float getSpecularProbability() const { return surface_params.x; }

    inline void setIOR(float value) { glass_params.x = value; }
    inline float getIOR() const { return glass_params.x; }

    inline void setCheckerScale(float value) { pattern_params.x = value; }
    inline float getCheckerScale() const { return pattern_params.x; }
};
#endif

#ifndef TRIANGLE
#define TRIANGLE
/**
 * @struct Triangle
 * @brief One mesh triangle in the format sent to the GPU.
 *
 * Assimp loads models as vertices/faces, loadMesh() converts every face to this
 * flat representation, and the Renderer uploads the vector to the TRIANGLES
 * SSBO. The shader uses positions for intersection tests, normals for lighting,
 * and UVs for PBR texture lookup.
 */
struct Triangle
{
    glm::vec3 v1;
    float padding1 = 0.0f;
    glm::vec3 v2;
    float padding2 = 0.0f;
    glm::vec3 v3;
    float padding3 = 0.0f;

    glm::vec3 NA;
    float padding4 = 0.0f;
    glm::vec3 NB;
    float padding5 = 0.0f;
    glm::vec3 NC;
    float padding6 = 0.0f;

    glm::vec3 centroid;
    float padding7 = 0.0f;

    glm::vec4 uvA_uvB = glm::vec4(0.0f);
    glm::vec4 uvC_padding = glm::vec4(0.0f);

    friend std::ostream& operator<<(std::ostream& os, const Triangle& triangle);
};
#endif

#ifndef OBJ_PARSER
#define OBJ_PARSER
/**
 * @struct PBRTextureSet
 * @brief File paths and simple controls for one PBR texture material.
 *
 * A model can use this instead of only a plain color. The paths point to the
 * texture files, while the multipliers make it possible to tune the material
 * from the scene/GUI without editing the image files.
 */
struct PBRTextureSet
{
    std::string albedoPath; ///< Base color texture.
    std::string normalPath; ///< Tangent-space normal map using OpenGL convention.
    std::string roughnessPath; ///< Roughness map: 0 glossy, 1 matte.
    std::string metallicPath; ///< Metallic map: 0 dielectric, 1 metal.
    std::string aoPath; ///< Ambient occlusion map used to darken creases.
    std::string heightPath; ///< Height map for parallax UV displacement only.
    float uvScale = 1.0f; ///< Texture tiling multiplier.
    float normalStrength = 1.0f; ///< Strength applied to tangent normal xy.
    float roughnessMultiplier = 1.0f; ///< Multiplier applied to the roughness map.
    float metallicMultiplier = 1.0f; ///< Multiplier applied to the metallic map.
    float roughnessBias = 0.0f; ///< Additive roughness offset for artistic control.
    float metallicBase = 0.0f; ///< Minimum metallic value when a map is black.
    float parallaxStrength = 0.04f; ///< Height-to-UV offset scale for parallax mapping.

    bool enabled() const { return !albedoPath.empty(); }
};

/**
 * @struct ModelInstance
 * @brief One model placed in the scene.
 *
 * Scene presets create ModelInstance objects. The GUI can edit their transform
 * and material. constructScene() then converts them into ModelInfo records so
 * the shader knows which mesh, transform and material belong to each model.
 */
struct ModelInstance
{
    std::string meshPath; ///< Path to the mesh asset loaded by Assimp.
    glm::mat4 localToWorld = glm::mat4(1.0f); ///< Object-to-world transform sent to GPU.
    glm::vec3 position = glm::vec3(0.0f); ///< Editable translation.
    glm::vec3 rotationDeg = glm::vec3(0.0f); ///< Editable Euler rotation in degrees.
    glm::vec3 scale = glm::vec3(1.0f); ///< Editable non-uniform scale.
    RaytracingMaterial material; ///< Fallback/non-textured material.
    PBRTextureSet pbrTextures; ///< Optional PBR texture set for this instance.
};

/**
 * @struct ModelInfo
 * @brief Compact per-model record consumed by the compute shader.
 * @details
 * This is the shader's lookup card for a model instance. nodeOffset and
 * triangleOffset point into the big global BVH/TRIANGLES buffers. textureLayer
 * selects the layer inside the OpenGL texture arrays. The transform matrices let
 * the shader test rays in the model's local space.
 *
 * Current limitation: each ModelInstance has one material/PBR set for the whole
 * mesh.
 */
struct ModelInfo
{
    int nodeOffset = 0;
    int triangleOffset = 0;
    int textureLayer = -1;
    int usePBRTextures = 0;
    glm::vec4 pbrParameters = glm::vec4(1.0f);
    glm::vec4 pbrExtraParameters = glm::vec4(0.04f, 0.0f, 0.0f, 0.0f);
    glm::mat4 worldToLocalMatrix = glm::mat4(1.0f);
    glm::mat4 localToWorldMatrix = glm::mat4(1.0f);
    RaytracingMaterial material;
};

/**
 * @brief Loads a mesh file into the flat triangle representation used by the GPU.
 * @param filePath Path to the mesh file, for example an OBJ/FBX asset.
 * @param mesh Destination vector that receives the triangles.
 * @param numTriangles Output triangle count.
 */
void loadMesh(std::string filePath, std::vector<Triangle>& mesh, unsigned int& numTriangles);
#endif

#ifndef BVH_IMPLEMENTATION
#define BVH_IMPLEMENTATION
namespace BVH {

#ifndef heuristicEnum
#define heuristicEnum
    enum class Heuristic {
        OBJECT_MEDIAN_SPLIT,
        SPATIAL_MIDDLE_SPLIT,
        SURFACE_AREA_HEURISTIC
    };
#endif

    /**
     * @class Node
     * @brief Node of the binary BVH tree stored in a flat vector.
     *
     * A node is a bounding box around a group of triangles. If triangleCount is
     * greater than zero, it is a leaf and startIndex points to triangles. If
     * triangleCount is zero, it is an internal node and startIndex points to its
     * left child; the right child is stored immediately after it.
     */
    class Node {
    public:
        Node() = default;
        Node(glm::vec3 minVec, glm::vec3 maxVec, int startIndex = -1, int triangleCount = 0);

        friend std::ostream& operator<<(std::ostream& os, const Node& node);

        glm::vec3 minVec = glm::vec3(0.0f);
        int startIndex = -1;
        glm::vec3 maxVec = glm::vec3(0.0f);
        int triangleCount = 0;
    };

    /**
     * @struct Metrics
     * @brief Runtime statistics used by the BVH debug GUI.
     *
     * These values are not required for rendering. They exist to help compare
     * split heuristics and understand whether the tree became shallow/deep or
     * whether leaves contain many/few triangles.
     */
    struct Metrics {
        unsigned int nodeCount = 0;
        unsigned int leafCount = 0;
        unsigned int leafDepthMin = 0;
        unsigned int leafDepthMax = 0;
        float leafDepthMean = 0.0f;
        unsigned int leafTrianglesMin = 0;
        unsigned int leafTrianglesMax = 0;
        float leafTrianglesMean = 0.0f;
    };

    /**
     * @struct BVH_data
     * @brief BVH and triangle buffers for a single mesh file.
     *
     * construct() returns this before the data is merged with the rest of the
     * scene. Renderer later uploads the triangles and nodes to GPU buffers.
     */
    struct BVH_data {
        std::vector<BVH::Node> BVH;
        std::vector<Triangle> TRIANGLES;

        unsigned int BVH_tree_depth = 0;
        std::vector<glm::vec3> heatmapLayers;
        Metrics metrics;

        unsigned int BVH_size = 0;
        unsigned int TRIANGLES_size = 0;
    };

    /**
     * @struct SceneGeometryData
     * @brief Combined GPU upload data for all mesh instances in a scene.
     *
     * This is what application.cpp passes to Renderer. It contains one big set
     * of triangles, one big set of BVH nodes, and one ModelInfo per model so the
     * shader can find the correct offsets for each instance.
     */
    struct SceneGeometryData {
        std::vector<BVH::Node> BVH;
        std::vector<Triangle> TRIANGLES;
        std::vector<ModelInfo> MODELS;
        std::vector<PBRTextureSet> PBR_TEXTURES;

        unsigned int BVH_tree_depth = 0;
        Metrics metrics;
        unsigned int BVH_size = 0;
        unsigned int TRIANGLES_size = 0;
        unsigned int MODEL_count = 0;
    };

    /**
     * @brief Builds a BVH for one mesh file.
     *
     * This is used when a mesh path appears for the first time in constructScene().
     * If several scene objects use the same mesh, constructScene() can reuse the
     * result instead of loading and building it again.
     * @param path Mesh asset path.
     * @param heuristic Split heuristic used during construction.
     * @return Triangle and BVH buffers for the mesh.
     */
    BVH::BVH_data construct(std::string path, const Heuristic heuristic);

    /**
     * @brief Builds shared scene geometry buffers for all model instances.
     *
     * This function prepares the geometry package that Renderer uploads to the
     * GPU. It also deduplicates repeated mesh files and repeated PBR texture sets.
     * @param models Scene model instances.
     * @param heuristic Split heuristic used by every mesh BVH.
     * @return Combined triangles, BVH nodes, model info and PBR texture sets.
     */
    BVH::SceneGeometryData constructScene(const std::vector<ModelInstance>& models, const Heuristic heuristic);

    /**
     * @brief Recursively computes maximum BVH tree depth.
     *
     * The app uses this for BVH debug settings and to keep the shader traversal
     * stack large enough for the generated tree.
     */
    unsigned int getBVHTreeDepth(const std::vector<Node>& BVH, unsigned int nodeIndex, unsigned int height);

    /**
     * @brief Computes BVH statistics for GUI inspection.
     *
     * These numbers are shown in the BVH settings GUI. They are useful when
     * comparing OBJECT_MEDIAN_SPLIT, SPATIAL_MIDDLE_SPLIT and SURFACE_AREA_HEURISTIC.
     */
    Metrics calculateMetrics(const std::vector<Node>& BVH, unsigned int nodeIndex, unsigned int depth = 0);
}
#endif
