#include "scene.h"
#include "resource.h"
#include "type_conversion.h"
#include "external/assimp/Importer.hpp"
#include "external/assimp/scene.h"
#include "external/assimp/postprocess.h"
#include <numeric>

namespace bb {

ShaderBallScene::ShaderBallScene(CommonSceneResources *_common)
    : SceneBase(_common) {
  const Renderer &renderer = *Common->Renderer;
  VkCommandPool transientCmdPool = Common->TransientCmdPool;

  // Setup plane buffers
  {
    std::vector<Vertex> planeVertices;
    std::vector<uint32_t> planeIndices;
    generatePlaneMesh(planeVertices, planeIndices);

    Plane.VertexBuffer = createDeviceLocalBufferFromMemory(
        renderer, transientCmdPool, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        sizeBytes32(planeVertices), planeVertices.data());

    InstanceBlock planeInstanceData = {};
    planeInstanceData.ModelMat =
        Mat4::translate({0, -10, 0}) * Mat4::scale({100.f, 100.f, 100.f});
    planeInstanceData.InvModelMat = planeInstanceData.ModelMat.inverse();
    Plane.InstanceBuffer = createBuffer(
        renderer, sizeof(planeInstanceData), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    {
      void *dst;
      vkMapMemory(renderer.Device, Plane.InstanceBuffer.Memory, 0,
                  Plane.InstanceBuffer.Size, 0, &dst);
      memcpy(dst, &planeInstanceData, sizeof(planeInstanceData));
      vkUnmapMemory(renderer.Device, Plane.InstanceBuffer.Memory);
    }

    Plane.IndexBuffer = createDeviceLocalBufferFromMemory(
        renderer, transientCmdPool, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        sizeBytes32(planeIndices), planeIndices.data());
    Plane.NumIndices = planeIndices.size();
  }

  // Setup shaderball buffers
  {
    Assimp::Importer importer;
    const aiScene *shaderBallScene =
        importer.ReadFile(createAbsolutePath("ShaderBall.fbx"),
                          aiProcess_Triangulate | aiProcess_CalcTangentSpace);
    const aiMesh *shaderBallMesh = shaderBallScene->mMeshes[0];
    std::vector<Vertex> shaderBallVertices;
    shaderBallVertices.reserve(shaderBallMesh->mNumFaces * 3);
    for (unsigned int i = 0; i < shaderBallMesh->mNumFaces; ++i) {
      const aiFace &face = shaderBallMesh->mFaces[i];
      BB_ASSERT(face.mNumIndices == 3);

      for (int j = 0; j < 3; ++j) {
        unsigned int vi = face.mIndices[j];

        Vertex v = {};
        v.Pos = aiVector3DToFloat3(shaderBallMesh->mVertices[vi]);
        v.UV = aiVector3DToFloat2(shaderBallMesh->mTextureCoords[0][vi]);
        v.Normal = aiVector3DToFloat3(shaderBallMesh->mNormals[vi]);
        v.Tangent = aiVector3DToFloat3(shaderBallMesh->mTangents[vi]);
        shaderBallVertices.push_back(v);
      }
    }

    std::vector<uint32_t> shaderBallIndices;
    shaderBallIndices.resize(shaderBallVertices.size());
    std::iota(shaderBallIndices.begin(), shaderBallIndices.end(), 0);

    ShaderBall.VertexBuffer = createDeviceLocalBufferFromMemory(
        renderer, transientCmdPool, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        sizeBytes32(shaderBallVertices), shaderBallVertices.data());

    ShaderBall.IndexBuffer = createDeviceLocalBufferFromMemory(
        renderer, transientCmdPool, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        sizeBytes32(shaderBallIndices), shaderBallIndices.data());

    std::vector<InstanceBlock> instanceData(ShaderBall.NumInstances);

    ShaderBall.InstanceBuffer = createBuffer(
        renderer, sizeBytes32(instanceData), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
  }
}

ShaderBallScene::~ShaderBallScene() {
  const Renderer &renderer = *Common->Renderer;

  destroyBuffer(renderer, ShaderBall.IndexBuffer);
  destroyBuffer(renderer, ShaderBall.InstanceBuffer);
  destroyBuffer(renderer, ShaderBall.VertexBuffer);

  destroyBuffer(renderer, Plane.IndexBuffer);
  destroyBuffer(renderer, Plane.InstanceBuffer);
  destroyBuffer(renderer, Plane.VertexBuffer);
}

} // namespace bb