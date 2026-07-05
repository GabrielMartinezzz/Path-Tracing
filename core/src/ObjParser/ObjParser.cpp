/**
 * @file ObjParser.cpp
 * @brief Mesh loading, triangle extraction and BVH construction.
 * @details
 * This file turns model files into the data that the GPU ray tracer can read.
 * First Assimp imports the mesh. Then the code converts faces into Triangle
 * records. Finally it builds a BVH, which is a tree of bounding boxes used by
 * the shader to skip most triangles during intersection tests.
 */

#include "core/ObjParser/ObjParser.h"

#include <assimp/cimport.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <limits>
#include <unordered_map>


namespace {

// CPU-side mesh loading and BVH construction.
//
// References used:
// - Assimp usage, C import API and post-processing flags:
//   https://documentation.help/assimp/usage.html
// - Assimp post-processing flags list:
//   https://github.com/assimp/assimp/blob/master/include/assimp/postprocess.h
// - PBRT, Bounding Volume Hierarchies and Surface Area Heuristic background:
//   https://pbr-book.org/4ed/Primitives_and_Intersection_Acceleration
// - Scratchapixel, acceleration structures and bounding volumes:
//   https://www.scratchapixel.com/lessons/3d-basic-rendering/introduction-acceleration-structure/bounding-volume.html
// - Jacco Bikker, practical BVH construction series:
//   https://jacco.ompf2.com/2022/04/13/how-to-build-a-bvh-part-1-basics/
// - NVIDIA developer blog, tree construction context:
//   https://developer.nvidia.com/blog/thinking-parallel-part-iii-tree-construction-gpu/
//
// Overview:
// 1. Assimp reads model files and gives us meshes/faces/vertices.
// 2. loadMesh() converts that data into a flat list of Triangle structs.
// 3. construct() builds a BVH for one mesh, so the shader can skip many triangles.
// 4. constructScene() joins all meshes into global GPU buffers and stores offsets
//    in ModelInfo so each model knows where its triangles/BVH begin.


struct BuildTriangle
{
    // Temporary helper used only while building the BVH. It stores the triangle
    // center and bounding box so split decisions are easier to compute. This is
    // not uploaded to the GPU; the shader only receives the final Triangle list
    // and BVH nodes.
    float centreX = 0.0f;
    float centreY = 0.0f;
    float centreZ = 0.0f;
    float minX = 0.0f;
    float minY = 0.0f;
    float minZ = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
    float maxZ = 0.0f;
};

struct NodeList
{
    std::vector<BVH::Node> nodes;

    int add(const BVH::Node& node)
    {
        nodes.push_back(node);
        return static_cast<int>(nodes.size() - 1);
    }
};

void mergeMetrics(BVH::Metrics& target, const BVH::Metrics& source)
{
    // constructScene() may build several BVHs, one per unique mesh file. The GUI
    // wants one summary for the whole scene, so the per-mesh statistics are
    // merged here.
    target.nodeCount += source.nodeCount;
    if (source.leafCount == 0) {
        return;
    }

    if (target.leafCount == 0) {
        target.leafDepthMin = source.leafDepthMin;
        target.leafDepthMax = source.leafDepthMax;
        target.leafTrianglesMin = source.leafTrianglesMin;
        target.leafTrianglesMax = source.leafTrianglesMax;
        target.leafDepthMean = source.leafDepthMean;
        target.leafTrianglesMean = source.leafTrianglesMean;
        target.leafCount = source.leafCount;
        return;
    }

    float totalLeafCount = static_cast<float>(target.leafCount + source.leafCount);
    target.leafDepthMean = (target.leafDepthMean * static_cast<float>(target.leafCount) + source.leafDepthMean * static_cast<float>(source.leafCount)) / totalLeafCount;
    target.leafTrianglesMean = (target.leafTrianglesMean * static_cast<float>(target.leafCount) + source.leafTrianglesMean * static_cast<float>(source.leafCount)) / totalLeafCount;
    target.leafDepthMin = std::min(target.leafDepthMin, source.leafDepthMin);
    target.leafDepthMax = std::max(target.leafDepthMax, source.leafDepthMax);
    target.leafTrianglesMin = std::min(target.leafTrianglesMin, source.leafTrianglesMin);
    target.leafTrianglesMax = std::max(target.leafTrianglesMax, source.leafTrianglesMax);
    target.leafCount += source.leafCount;
}

BuildTriangle makeBuildTriangle(const Triangle& tri)
{
    // The builder repeatedly asks two questions: where is this triangle's center,
    // and what is the smallest box that contains it? Precomputing those answers
    // avoids repeating the same min/max work during split tests.
    BuildTriangle result;
    result.centreX = tri.centroid.x;
    result.centreY = tri.centroid.y;
    result.centreZ = tri.centroid.z;

    result.minX = std::min({tri.v1.x, tri.v2.x, tri.v3.x});
    result.minY = std::min({tri.v1.y, tri.v2.y, tri.v3.y});
    result.minZ = std::min({tri.v1.z, tri.v2.z, tri.v3.z});
    result.maxX = std::max({tri.v1.x, tri.v2.x, tri.v3.x});
    result.maxY = std::max({tri.v1.y, tri.v2.y, tri.v3.y});
    result.maxZ = std::max({tri.v1.z, tri.v2.z, tri.v3.z});
    return result;
}

float nodeCost(float x, float y, float z, int numTriangles)
{
    if (numTriangles <= 0) {
        return 0.0f;
    }
    // SAH-inspired cost proxy: a large box with many triangles is expensive
    // because rays are more likely to enter it and then test many triangles.
    //
    // Box surface area:
    // A = 2(xy + xz + yz)
    //
    // Estimated cost:
    // cost = A * numTriangles
    //
    // The factor 2 is equal for all candidates, so it can be ignored when only
    // comparing split costs.
    return (x * y + x * z + y * z) * static_cast<float>(numTriangles);
}

glm::vec3 toGlm(const aiVector3D& value)
{
    return glm::vec3(value.x, value.y, value.z);
}

glm::vec2 toGlm2(const aiVector3D& value)
{
    return glm::vec2(value.x, value.y);
}

std::string textureSetKey(const PBRTextureSet& textures)
{
    // Used as a cache key in constructScene(). If two models reference the exact
    // same texture files, they can share one texture-array layer on the GPU.
    return textures.albedoPath + "|" + textures.normalPath + "|" + textures.roughnessPath + "|" +
        textures.metallicPath + "|" + textures.aoPath + "|" + textures.heightPath;
}

struct SplitResult
{
    int axis = 0;
    float position = 0.0f;
    float cost = std::numeric_limits<float>::infinity();
};

SplitResult chooseSplit(
    BVH::Heuristic heuristic,
    const BVH::Node& node,
    const std::vector<BuildTriangle>& buildTriangles,
    int start,
    int count)
{
    if (count <= 1) {
        return {};
    }

    // Three split strategies are available in the BVH GUI:
    // - object median: sort triangle centers and split the list roughly in half;
    // - spatial middle: cut the current bounding box through its middle;
    // - SAH-like: try several possible cuts and keep the cheapest estimated one.
    float sizeX = node.maxVec.x - node.minVec.x;
    float sizeY = node.maxVec.y - node.minVec.y;
    float sizeZ = node.maxVec.z - node.minVec.z;

    if (heuristic == BVH::Heuristic::OBJECT_MEDIAN_SPLIT || heuristic == BVH::Heuristic::SPATIAL_MIDDLE_SPLIT) {
        // The two simpler strategies choose only one cut. This makes them easy
        // to compare with SAH in the GUI: changing the cut changes the tree shape,
        // which later changes how much work BVH_traverse() does in the shader.
        int axis = sizeX > sizeY && sizeX > sizeZ ? 0 : (sizeY > sizeZ ? 1 : 2);
        float position = 0.0f;
        if (heuristic == BVH::Heuristic::SPATIAL_MIDDLE_SPLIT) {
            position = axis == 0 ? node.minVec.x + sizeX * 0.5f :
                (axis == 1 ? node.minVec.y + sizeY * 0.5f : node.minVec.z + sizeZ * 0.5f);
        }
        else {
            std::vector<float> values;
            values.reserve(count);
            for (int i = start; i < start + count; ++i) {
                values.push_back(axis == 0 ? buildTriangles[i].centreX :
                    (axis == 1 ? buildTriangles[i].centreY : buildTriangles[i].centreZ));
            }
            std::sort(values.begin(), values.end());
            position = values[values.size() / 2];
        }
        return { axis, position, 0.0f };
    }

    SplitResult best;
    float maxAxis = std::max({ sizeX, sizeY, sizeZ });
    int maxSplitTests = count < 10 ? 3 : 5;

    auto evaluateSplit = [&](int axis, float splitPos) {
        // Try a possible cut without moving triangles yet. We count how many
        // triangles would go left/right, build the two resulting boxes, and
        // estimate the cost. splitNode() only rearranges triangles after the best
        // cut has been chosen.
        int numLeft = 0;
        int numRight = 0;

        float xMinLeft = std::numeric_limits<float>::max();
        float yMinLeft = std::numeric_limits<float>::max();
        float zMinLeft = std::numeric_limits<float>::max();
        float xMaxLeft = -std::numeric_limits<float>::max();
        float yMaxLeft = -std::numeric_limits<float>::max();
        float zMaxLeft = -std::numeric_limits<float>::max();

        float xMinRight = std::numeric_limits<float>::max();
        float yMinRight = std::numeric_limits<float>::max();
        float zMinRight = std::numeric_limits<float>::max();
        float xMaxRight = -std::numeric_limits<float>::max();
        float yMaxRight = -std::numeric_limits<float>::max();
        float zMaxRight = -std::numeric_limits<float>::max();

        for (int i = start; i < start + count; ++i) {
            const BuildTriangle& tri = buildTriangles[i];
            float c = axis == 0 ? tri.centreX : (axis == 1 ? tri.centreY : tri.centreZ);
            if (c < splitPos) {
                xMinLeft = std::min(xMinLeft, tri.minX);
                yMinLeft = std::min(yMinLeft, tri.minY);
                zMinLeft = std::min(zMinLeft, tri.minZ);
                xMaxLeft = std::max(xMaxLeft, tri.maxX);
                yMaxLeft = std::max(yMaxLeft, tri.maxY);
                zMaxLeft = std::max(zMaxLeft, tri.maxZ);
                numLeft++;
            }
            else {
                xMinRight = std::min(xMinRight, tri.minX);
                yMinRight = std::min(yMinRight, tri.minY);
                zMinRight = std::min(zMinRight, tri.minZ);
                xMaxRight = std::max(xMaxRight, tri.maxX);
                yMaxRight = std::max(yMaxRight, tri.maxY);
                zMaxRight = std::max(zMaxRight, tri.maxZ);
                numRight++;
            }
        }

        float costA = nodeCost(xMaxLeft - xMinLeft, yMaxLeft - yMinLeft, zMaxLeft - zMinLeft, numLeft);
        float costB = nodeCost(xMaxRight - xMinRight, yMaxRight - yMinRight, zMaxRight - zMinRight, numRight);
        return costA + costB;
    };

    for (int axis = 0; axis < 3; ++axis) {
        float axisSize = axis == 0 ? sizeX : (axis == 1 ? sizeY : sizeZ);
        float axisMin = axis == 0 ? node.minVec.x : (axis == 1 ? node.minVec.y : node.minVec.z);
        // Longer axes get more candidate cuts. A long direction usually has more
        // room to separate triangles into useful child boxes.
        int numSplitTests = glm::clamp(static_cast<int>(std::ceil(axisSize / std::max(maxAxis, 1.0e-5f) * maxSplitTests)), 1, maxSplitTests);

        for (int i = 0; i < numSplitTests; ++i) {
            float splitT = static_cast<float>(i + 1) / static_cast<float>(numSplitTests + 1);
            float splitPos = axisMin + axisSize * splitT;
            float cost = evaluateSplit(axis, splitPos);
            if (cost < best.cost) {
                best = { axis, splitPos, cost };
            }
        }
    }

    return best;
}

void splitNode(
    int parentIndex,
    int triStart,
    int triCount,
    int depth,
    BVH::Heuristic heuristic,
    std::vector<Triangle>& triangles,
    std::vector<BuildTriangle>& buildTriangles,
    NodeList& nodeList)
{
    constexpr int maxDepth = 32;

    const BVH::Node parent = nodeList.nodes[parentIndex];
    float sizeX = parent.maxVec.x - parent.minVec.x;
    float sizeY = parent.maxVec.y - parent.minVec.y;
    float sizeZ = parent.maxVec.z - parent.minVec.z;
    float parentCost = nodeCost(sizeX, sizeY, sizeZ, triCount);

    SplitResult split = chooseSplit(heuristic, parent, buildTriangles, triStart, triCount);
    if (depth >= maxDepth || triCount <= 1 || split.cost >= parentCost) {
        // Stop splitting and make a leaf. A leaf is where the shader finally
        // tests real triangles. startIndex points to the first triangle in the
        // flat TRIANGLES buffer; triangleCount says how many triangles follow it.
        nodeList.nodes[parentIndex].startIndex = triStart;
        nodeList.nodes[parentIndex].triangleCount = triCount;
        return;
    }

    int numOnLeft = 0;
    float xMinLeft = std::numeric_limits<float>::max();
    float yMinLeft = std::numeric_limits<float>::max();
    float zMinLeft = std::numeric_limits<float>::max();
    float xMaxLeft = -std::numeric_limits<float>::max();
    float yMaxLeft = -std::numeric_limits<float>::max();
    float zMaxLeft = -std::numeric_limits<float>::max();

    float xMinRight = std::numeric_limits<float>::max();
    float yMinRight = std::numeric_limits<float>::max();
    float zMinRight = std::numeric_limits<float>::max();
    float xMaxRight = -std::numeric_limits<float>::max();
    float yMaxRight = -std::numeric_limits<float>::max();
    float zMaxRight = -std::numeric_limits<float>::max();

    for (int i = triStart; i < triStart + triCount; ++i) {
        BuildTriangle tri = buildTriangles[i];
        float c = split.axis == 0 ? tri.centreX : (split.axis == 1 ? tri.centreY : tri.centreZ);
        if (c < split.position) {
            xMinLeft = std::min(xMinLeft, tri.minX);
            yMinLeft = std::min(yMinLeft, tri.minY);
            zMinLeft = std::min(zMinLeft, tri.minZ);
            xMaxLeft = std::max(xMaxLeft, tri.maxX);
            yMaxLeft = std::max(yMaxLeft, tri.maxY);
            zMaxLeft = std::max(zMaxLeft, tri.maxZ);

            std::swap(buildTriangles[triStart + numOnLeft], buildTriangles[i]);
            std::swap(triangles[triStart + numOnLeft], triangles[i]);
            // Move left-side triangles into a continuous range. That keeps GPU
            // traversal simple: a leaf only needs startIndex + triangleCount, not
            // a separate list of triangle indices.
            numOnLeft++;
        }
        else {
            xMinRight = std::min(xMinRight, tri.minX);
            yMinRight = std::min(yMinRight, tri.minY);
            zMinRight = std::min(zMinRight, tri.minZ);
            xMaxRight = std::max(xMaxRight, tri.maxX);
            yMaxRight = std::max(yMaxRight, tri.maxY);
            zMaxRight = std::max(zMaxRight, tri.maxZ);
        }
    }

    int numOnRight = triCount - numOnLeft;
    if (numOnLeft == 0 || numOnRight == 0) {
        // Bad split: every triangle landed on the same side. If we tried to split
        // again, we could loop forever or create empty children. Treat this node
        // as a leaf instead.
        nodeList.nodes[parentIndex].startIndex = triStart;
        nodeList.nodes[parentIndex].triangleCount = triCount;
        return;
    }

    int leftChildIndex = nodeList.add(BVH::Node(
        glm::vec3(xMinLeft, yMinLeft, zMinLeft),
        glm::vec3(xMaxLeft, yMaxLeft, zMaxLeft)));
    int rightChildIndex = nodeList.add(BVH::Node(
        glm::vec3(xMinRight, yMinRight, zMinRight),
        glm::vec3(xMaxRight, yMaxRight, zMaxRight)));

    nodeList.nodes[parentIndex].startIndex = leftChildIndex;
    nodeList.nodes[parentIndex].triangleCount = 0;
    // Internal node convention used by the shader:
    // - triangleCount == 0 means "this is not a leaf"
    // - startIndex points to the left child
    // - the right child is stored immediately after it at startIndex + 1

    splitNode(leftChildIndex, triStart, numOnLeft, depth + 1, heuristic, triangles, buildTriangles, nodeList);
    splitNode(rightChildIndex, triStart + numOnLeft, numOnRight, depth + 1, heuristic, triangles, buildTriangles, nodeList);
}

}

std::ostream& operator<<(std::ostream& os, const Triangle& triangle)
{
    os << "("
        << "[(" << triangle.v1.x << ", " << triangle.v1.y << ", " << triangle.v1.z << "), "
        << "(" << triangle.v2.x << ", " << triangle.v2.y << ", " << triangle.v2.z << "), "
        << "(" << triangle.v3.x << ", " << triangle.v3.y << ", " << triangle.v3.z << ")] "
        << ", ("
        << triangle.centroid.x << ", " << triangle.centroid.y << ", " << triangle.centroid.z <<
        ")),";
    return os;
}

void loadMesh(std::string filePath, std::vector<Triangle>& mesh, unsigned int& numTriangles)
{
    numTriangles = 0;

    // Assimp opens many model formats and converts them into one common scene
    // structure. This ray tracer only needs triangles, normals and optional UV
    // channel 0, so the imported data is reduced to that small format.
    //
    // aiProcessPreset_TargetRealtime_MaxQuality asks Assimp to run a set of
    // post-processing steps suitable for real-time rendering, including triangle
    // conversion and mesh cleanup/optimization.
    const aiScene* scene = aiImportFile(filePath.c_str(), aiProcessPreset_TargetRealtime_MaxQuality);
    if (!scene) {
        std::cerr << "Error loading the model: " << filePath << std::endl;
        return;
    }

    for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
        const aiMesh* currentMesh = scene->mMeshes[meshIndex];
        for (unsigned int faceIndex = 0; faceIndex < currentMesh->mNumFaces; ++faceIndex) {
            const aiFace& face = currentMesh->mFaces[faceIndex];
            if (face.mNumIndices != 3) {
                continue;
            }

            Triangle triangle;
            // Copy positions and normals into the same Triangle layout that the
            // shader reads later. If this C++ struct changes, the Triangle struct
            // in ComputeRayTracing.comp must change too.
            triangle.v1 = toGlm(currentMesh->mVertices[face.mIndices[0]]);
            triangle.v2 = toGlm(currentMesh->mVertices[face.mIndices[1]]);
            triangle.v3 = toGlm(currentMesh->mVertices[face.mIndices[2]]);

            triangle.NA = toGlm(currentMesh->mNormals[face.mIndices[0]]);
            triangle.NB = toGlm(currentMesh->mNormals[face.mIndices[1]]);
            triangle.NC = toGlm(currentMesh->mNormals[face.mIndices[2]]);
            triangle.centroid = (triangle.v1 + triangle.v2 + triangle.v3) / 3.0f;

            if (currentMesh->HasTextureCoords(0)) {
                // Only UV channel 0 is used. At render time the shader blends
                // these UVs at the hit point and uses them to sample PBR textures
                // such as albedo, normal, roughness, metallic and height maps.
                glm::vec2 uvA = toGlm2(currentMesh->mTextureCoords[0][face.mIndices[0]]);
                glm::vec2 uvB = toGlm2(currentMesh->mTextureCoords[0][face.mIndices[1]]);
                glm::vec2 uvC = toGlm2(currentMesh->mTextureCoords[0][face.mIndices[2]]);
                triangle.uvA_uvB = glm::vec4(uvA, uvB);
                triangle.uvC_padding = glm::vec4(uvC, 0.0f, 0.0f);
            }

            mesh.push_back(triangle);
            numTriangles++;
        }
    }

    std::cout << numTriangles << " triangles loaded" << std::endl;
}

BVH::Node::Node(glm::vec3 minVec, glm::vec3 maxVec, int startIndex, int triangleCount)
    : minVec(minVec), startIndex(startIndex), maxVec(maxVec), triangleCount(triangleCount)
{
}

namespace BVH {
std::ostream& operator<<(std::ostream& os, const Node& node)
{
    os << "{ \"minVec\": (" << node.minVec.x << ", " << node.minVec.y << ", " << node.minVec.z << "),"
       << " \"maxVec\": (" << node.maxVec.x << ", " << node.maxVec.y << ", " << node.maxVec.z << "),"
       << " \"startIndex\": " << node.startIndex << ","
       << " \"triangleCount\": " << node.triangleCount << " }";
    return os;
}

BVH_data construct(std::string path, const Heuristic heuristic)
{
    std::vector<Triangle> triangles;
    unsigned int numTriangles = 0;
    loadMesh(path, triangles, numTriangles);

    BVH_data bvhData;
    bvhData.TRIANGLES = triangles;
    bvhData.TRIANGLES_size = numTriangles;

    if (triangles.empty()) {
        return bvhData;
    }

    std::vector<BuildTriangle> buildTriangles;
    buildTriangles.reserve(triangles.size());

    // BuildTriangle exists only while building the tree. The GPU does not need
    // these duplicated bounds/centers after the BVH is ready.
    float xMin = std::numeric_limits<float>::max();
    float yMin = std::numeric_limits<float>::max();
    float zMin = std::numeric_limits<float>::max();
    float xMax = -std::numeric_limits<float>::max();
    float yMax = -std::numeric_limits<float>::max();
    float zMax = -std::numeric_limits<float>::max();

    for (const Triangle& triangle : triangles) {
        BuildTriangle buildTriangle = makeBuildTriangle(triangle);
        buildTriangles.push_back(buildTriangle);

        xMin = std::min(xMin, buildTriangle.minX);
        yMin = std::min(yMin, buildTriangle.minY);
        zMin = std::min(zMin, buildTriangle.minZ);
        xMax = std::max(xMax, buildTriangle.maxX);
        yMax = std::max(yMax, buildTriangle.maxY);
        zMax = std::max(zMax, buildTriangle.maxZ);
    }

    NodeList nodeList;
    nodeList.add(Node(glm::vec3(xMin, yMin, zMin), glm::vec3(xMax, yMax, zMax)));

    // Node 0 is the root for this mesh. Later, constructScene() merges many mesh
    // BVHs into one global buffer, and ModelInfo.nodeOffset tells the shader
    // where this model's root ended up.
    splitNode(0, 0, static_cast<int>(triangles.size()), 0, heuristic, triangles, buildTriangles, nodeList);

    bvhData.BVH = nodeList.nodes;
    bvhData.BVH_size = static_cast<unsigned int>(bvhData.BVH.size());
    bvhData.TRIANGLES = triangles;
    bvhData.TRIANGLES_size = static_cast<unsigned int>(triangles.size());
    bvhData.BVH_tree_depth = getBVHTreeDepth(bvhData.BVH, 0, 0);
    bvhData.metrics = calculateMetrics(bvhData.BVH, 0, 0);
    return bvhData;
}

SceneGeometryData constructScene(const std::vector<ModelInstance>& models, const Heuristic heuristic)
{
    SceneGeometryData sceneData;
    // meshLookup avoids duplicating triangles/BVH nodes when several model
    // instances use the same file. The stored pair is:
    // (BVH node offset, triangle offset)
    // and those offsets are copied into ModelInfo for shader traversal.
    std::unordered_map<std::string, std::pair<int, int>> meshLookup;
    // textureLookup avoids uploading the same PBR texture set multiple times.
    // The stored integer becomes ModelInfo.textureLayer, which selects the layer
    // sampled by ComputeRayTracing.comp.
    std::unordered_map<std::string, int> textureLookup;

    for (const ModelInstance& model : models) {
        // Reuse BVH/triangle buffers when several instances reference the same
        // mesh path. Only ModelInfo changes between instances, because transform
        // and material can differ while the actual geometry is shared.
        auto lookup = meshLookup.find(model.meshPath);
        if (lookup == meshLookup.end()) {
            BVH_data meshBVH = construct(model.meshPath, heuristic);
            lookup = meshLookup.emplace(
                model.meshPath,
                std::make_pair(
                    static_cast<int>(sceneData.BVH.size()),
                    static_cast<int>(sceneData.TRIANGLES.size()))).first;

            sceneData.BVH.insert(sceneData.BVH.end(), meshBVH.BVH.begin(), meshBVH.BVH.end());
            sceneData.TRIANGLES.insert(sceneData.TRIANGLES.end(), meshBVH.TRIANGLES.begin(), meshBVH.TRIANGLES.end());
            sceneData.BVH_tree_depth = std::max(sceneData.BVH_tree_depth, meshBVH.BVH_tree_depth);
            mergeMetrics(sceneData.metrics, meshBVH.metrics);
        }

        ModelInfo modelInfo;
        // These offsets connect this model instance to the global GPU buffers.
        // BVH_traverse() receives them so it can start at the correct BVH root
        // and read the correct triangle range.
        modelInfo.nodeOffset = lookup->second.first;
        modelInfo.triangleOffset = lookup->second.second;
        modelInfo.localToWorldMatrix = model.localToWorld;
        modelInfo.worldToLocalMatrix = glm::inverse(model.localToWorld);
        // The BVH is built in model-local space. The shader transforms the ray
        // into that same local space before triangle tests, which lets multiple
        // transformed instances share one BVH/triangle buffer.
        modelInfo.material = model.material;
        // These vec4 values match ModelInfo in GLSL. Grouping related PBR
        // controls keeps the CPU/GPU buffer layout predictable.
        modelInfo.pbrParameters = glm::vec4(
            model.pbrTextures.uvScale,
            model.pbrTextures.normalStrength,
            model.pbrTextures.roughnessMultiplier,
            model.pbrTextures.metallicMultiplier);
        modelInfo.pbrExtraParameters = glm::vec4(
            model.pbrTextures.parallaxStrength,
            model.pbrTextures.roughnessBias,
            model.pbrTextures.metallicBase,
            0.0f);

        if (model.pbrTextures.enabled()) {
            // PBR textures are packed into texture arrays. textureLayer selects
            // which layer the shader samples for this model.
            const std::string key = textureSetKey(model.pbrTextures);
            auto texture = textureLookup.find(key);
            if (texture == textureLookup.end()) {
                // Upload a texture set once and give it a layer index. Models
                // with the same textures reuse that layer, saving GPU memory.
                int layer = static_cast<int>(sceneData.PBR_TEXTURES.size());
                sceneData.PBR_TEXTURES.push_back(model.pbrTextures);
                texture = textureLookup.emplace(key, layer).first;
            }
            modelInfo.textureLayer = texture->second;
            modelInfo.usePBRTextures = 1;
        }
        sceneData.MODELS.push_back(modelInfo);
    }

    sceneData.BVH_size = static_cast<unsigned int>(sceneData.BVH.size());
    sceneData.TRIANGLES_size = static_cast<unsigned int>(sceneData.TRIANGLES.size());
    sceneData.MODEL_count = static_cast<unsigned int>(sceneData.MODELS.size());
    return sceneData;
}

unsigned int getBVHTreeDepth(const std::vector<Node>& BVH, unsigned int nodeIndex, unsigned int height)
{
    // Walk down the tree and return the largest depth found. The app shows this
    // in the BVH GUI and also uses it as a useful debug value for traversal.
    if (BVH.empty() || nodeIndex >= BVH.size()) {
        return height;
    }

    const Node& node = BVH[nodeIndex];
    if (node.triangleCount > 0) {
        return height;
    }

    unsigned int leftHeight = getBVHTreeDepth(BVH, static_cast<unsigned int>(node.startIndex), height + 1);
    unsigned int rightHeight = getBVHTreeDepth(BVH, static_cast<unsigned int>(node.startIndex + 1), height + 1);
    return std::max(leftHeight, rightHeight);
}

Metrics calculateMetrics(const std::vector<Node>& BVH, unsigned int nodeIndex, unsigned int depth)
{
    // Recursively summarize the tree for the BVH Settings panel:
    // - how many nodes/leaves exist;
    // - how deep the leaves are;
    // - how many triangles each leaf contains.
    //
    // A "good" tree usually avoids very deep branches and avoids leaves with too
    // many triangles, because both cases make the shader do more work.
    Metrics metrics;
    if (BVH.empty() || nodeIndex >= BVH.size()) {
        return metrics;
    }

    metrics.nodeCount = 1;

    const Node& node = BVH[nodeIndex];
    if (node.triangleCount > 0) {
        // Leaf node: this is where traversal stops and triangle tests begin.
        metrics.leafCount = 1;
        metrics.leafDepthMin = depth;
        metrics.leafDepthMax = depth;
        metrics.leafDepthMean = static_cast<float>(depth);
        metrics.leafTrianglesMin = static_cast<unsigned int>(node.triangleCount);
        metrics.leafTrianglesMax = static_cast<unsigned int>(node.triangleCount);
        metrics.leafTrianglesMean = static_cast<float>(node.triangleCount);
        return metrics;
    }

    Metrics leftMetrics = calculateMetrics(BVH, static_cast<unsigned int>(node.startIndex), depth + 1);
    Metrics rightMetrics = calculateMetrics(BVH, static_cast<unsigned int>(node.startIndex + 1), depth + 1);

    // Internal node: combine the summaries from both children.
    metrics.nodeCount += leftMetrics.nodeCount + rightMetrics.nodeCount;
    metrics.leafCount = leftMetrics.leafCount + rightMetrics.leafCount;

    if (metrics.leafCount == 0) {
        return metrics;
    }

    if (leftMetrics.leafCount == 0) {
        metrics.leafDepthMin = rightMetrics.leafDepthMin;
        metrics.leafDepthMax = rightMetrics.leafDepthMax;
        metrics.leafTrianglesMin = rightMetrics.leafTrianglesMin;
        metrics.leafTrianglesMax = rightMetrics.leafTrianglesMax;
    }
    else if (rightMetrics.leafCount == 0) {
        metrics.leafDepthMin = leftMetrics.leafDepthMin;
        metrics.leafDepthMax = leftMetrics.leafDepthMax;
        metrics.leafTrianglesMin = leftMetrics.leafTrianglesMin;
        metrics.leafTrianglesMax = leftMetrics.leafTrianglesMax;
    }
    else {
        metrics.leafDepthMin = std::min(leftMetrics.leafDepthMin, rightMetrics.leafDepthMin);
        metrics.leafDepthMax = std::max(leftMetrics.leafDepthMax, rightMetrics.leafDepthMax);
        metrics.leafTrianglesMin = std::min(leftMetrics.leafTrianglesMin, rightMetrics.leafTrianglesMin);
        metrics.leafTrianglesMax = std::max(leftMetrics.leafTrianglesMax, rightMetrics.leafTrianglesMax);
    }

    float leftWeight = static_cast<float>(leftMetrics.leafCount);
    float rightWeight = static_cast<float>(rightMetrics.leafCount);
    float totalWeight = static_cast<float>(metrics.leafCount);

    // Means are weighted by leaf count so a subtree with many leaves contributes
    // proportionally more than a tiny subtree.
    metrics.leafDepthMean = (leftMetrics.leafDepthMean * leftWeight + rightMetrics.leafDepthMean * rightWeight) / totalWeight;
    metrics.leafTrianglesMean = (leftMetrics.leafTrianglesMean * leftWeight + rightMetrics.leafTrianglesMean * rightWeight) / totalWeight;
    return metrics;
}
}
