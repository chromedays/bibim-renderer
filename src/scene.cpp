#include "scene.h"
#include "resource.h"
#include "type_conversion.h"
#include "external/assimp/Importer.hpp"
#include "external/assimp/scene.h"
#include "external/assimp/postprocess.h"
#include "external/imgui/imgui_impl_vulkan.h"
#include <numeric>

namespace bb {

ShaderBallScene::ShaderBallScene(CommonSceneResources *_common)
    : SceneBase(_common) {
  const Renderer &renderer = *Common->Renderer;
  VkCommandPool transientCmdPool = Common->TransientCmdPool;
  const PBRMaterialSet &materialSet = *Common->MaterialSet;

  Lights.resize(3);
  Light *light = &Lights[0];
  light->Dir = {-1, -1, 0};
  light->Type = LightType::Directional;
  light->Color = {0.2347f, 0.2131f, 0.2079f};
  light->Intensity = 10.f;
  ++light;
  light->Pos = {0, 2, 0};
  light->Type = LightType::Point;
  light->Color = {1, 0.8f, 0.8f};
  light->Intensity = 50;
  ++light;
  light->Pos = {4, 2, 0};
  light->Dir = {0, -1, 0};
  light->Type = LightType::Point;
  light->Color = {0.8f, 1, 0.8f};
  light->Intensity = 50;
  light->InnerCutOff = degToRad(30);
  light->OuterCutOff = degToRad(25);

  // Setup plane buffers
  {
    std::vector<Vertex> planeVertices;
    std::vector<uint32_t> planeIndices;
    generatePlaneMesh(planeVertices, planeIndices);
    Plane.VertexBuffer = createVertexBuffer(planeVertices);
    Plane.IndexBuffer = createIndexBuffer(planeIndices);
    Plane.NumIndices = planeIndices.size();

    Plane.InstanceData.resize(Plane.NumInstances);
    InstanceBlock &planeInstanceData = Plane.InstanceData[0];
    planeInstanceData.ModelMat =
        Mat4::translate({0, -10, 0}) * Mat4::scale({100.f, 100.f, 100.f});
    planeInstanceData.InvModelMat = planeInstanceData.ModelMat.inverse();
    Plane.InstanceBuffer = createInstanceBuffer(Plane.NumInstances);
    updateInstanceBufferMemory(Plane.InstanceBuffer, Plane.InstanceData);
  }

  // Setup shaderball buffers
  {
    Assimp::Importer importer;
    const aiScene *shaderBallScene =
        importer.ReadFile(createCommonResourcePath("ShaderBall.fbx"),
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

    ShaderBall.VertexBuffer = createVertexBuffer(shaderBallVertices);
    ShaderBall.NumVertices = shaderBallVertices.size();

    ShaderBall.InstanceData.resize(ShaderBall.NumInstances);
    ShaderBall.InstanceBuffer = createInstanceBuffer(ShaderBall.NumInstances);
  }

  VkSampler materialImageSampler =
      Common->StandardPipelineLayout->ImmutableSamplers[SamplerType::Nearest];

  for (auto mapType : AllEnums<PBRMapType>) {
    const Image &image = materialSet.DefaultMaterial.Maps[mapType];
    if (image.Handle != VK_NULL_HANDLE) {
      GUI.DefaultMaterialTextureId[mapType] =
          ImGui_ImplVulkan_AddTexture(materialImageSampler, image.View,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
  }

  for (const PBRMaterial &material : materialSet.Materials) {
    EnumArray<PBRMapType, ImTextureID> textureIds;
    for (PBRMapType mapType : AllEnums<PBRMapType>) {
      const Image &image = material.Maps[mapType];
      if (image.Handle != VK_NULL_HANDLE) {
        textureIds[mapType] = ImGui_ImplVulkan_AddTexture(
            materialImageSampler, image.View,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      } else {
        textureIds[mapType] = GUI.DefaultMaterialTextureId[mapType];
      }
    }

    GUI.MaterialTextureIds.push_back(textureIds);
  }
}

ShaderBallScene::~ShaderBallScene() {
  const Renderer &renderer = *Common->Renderer;

  destroyBuffer(renderer, ShaderBall.InstanceBuffer);
  destroyBuffer(renderer, ShaderBall.VertexBuffer);

  destroyBuffer(renderer, Plane.IndexBuffer);
  destroyBuffer(renderer, Plane.InstanceBuffer);
  destroyBuffer(renderer, Plane.VertexBuffer);
}

void ShaderBallScene::updateGUI(float _dt) {
  const PBRMaterialSet &materialSet = *Common->MaterialSet;

  if (ImGui::Begin("Shader Balls")) {
    for (size_t i = 0; i < ShaderBall.InstanceData.size(); ++i) {
      std::string label = fmt::format("Shader Ball {}", i);
      if (ImGui::Selectable(label.c_str(),
                            i == GUI.SelectedShaderBallInstance)) {
        GUI.SelectedShaderBallInstance = i;
      }
    }
  }
  ImGui::End();

  if (ImGui::Begin("Material Selector")) {
    for (int i = 0; i < GUI.MaterialTextureIds.size(); ++i) {

      if (ImGui::Selectable(materialSet.Materials[i].Name.c_str(),
                            GUI.SelectedMaterial == i)) {
        GUI.SelectedMaterial = i;
      }
    }
  }
  ImGui::End();

  int numCols = 3;
  int col = 0;

  if (ImGui::Begin("Current Material")) {
    const auto &textureIds = GUI.MaterialTextureIds[GUI.SelectedMaterial];

    for (auto textureId : textureIds) {
      ImGui::Image(textureId, {50, 50});
      ++col;
      if (col < numCols) {
        ImGui::SameLine();
      } else {
        col = 0;
      }
    }
  }
  ImGui::End();
}

void ShaderBallScene::updateScene(float _dt) {
  const Renderer &renderer = *Common->Renderer;

  // ShaderBall.Angle += 30.f * dt;
  if (ShaderBall.Angle > 360) {
    ShaderBall.Angle -= 360;
  }

  for (int i = 0; i < ShaderBall.InstanceData.size(); i++) {
    ShaderBall.InstanceData[i].ModelMat =
        Mat4::translate({(float)(i * 2), -1, 2}) *
        Mat4::rotateY(ShaderBall.Angle) * Mat4::rotateX(-90) *
        Mat4::scale({0.01f, 0.01f, 0.01f});
    ShaderBall.InstanceData[i].InvModelMat =
        ShaderBall.InstanceData[i].ModelMat.inverse();
  }

  updateInstanceBufferMemory(ShaderBall.InstanceBuffer,
                             ShaderBall.InstanceData);
}

void ShaderBallScene::drawScene(const Frame &_frame) {
  VkCommandBuffer cmd = _frame.CmdBuffer;
  const StandardPipelineLayout &standardPipelineLayout =
      *Common->StandardPipelineLayout;

  vkCmdBindDescriptorSets(
      cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, standardPipelineLayout.Handle, 2, 1,
      &_frame.MaterialDescriptorSets[GUI.SelectedMaterial], 0, nullptr);

  VkDeviceSize offset = 0;
  vkCmdBindVertexBuffers(cmd, 0, 1, &ShaderBall.VertexBuffer.Handle, &offset);
  vkCmdBindVertexBuffers(cmd, 1, 1, &ShaderBall.InstanceBuffer.Handle, &offset);
  vkCmdDraw(cmd, ShaderBall.NumVertices, ShaderBall.NumInstances, 0, 0);

  vkCmdBindVertexBuffers(cmd, 0, 1, &Plane.VertexBuffer.Handle, &offset);
  vkCmdBindVertexBuffers(cmd, 1, 1, &Plane.InstanceBuffer.Handle, &offset);
  vkCmdBindIndexBuffer(cmd, Plane.IndexBuffer.Handle, 0, VK_INDEX_TYPE_UINT32);
  vkCmdDrawIndexed(cmd, Plane.NumIndices, Plane.NumInstances, 0, 0, 0);
}


SponzaScene::SponzaScene(CommonSceneResources *_common)
    : SceneBase(_common) {
  const Renderer &renderer = *Common->Renderer;
  VkCommandPool transientCmdPool = Common->TransientCmdPool;
  const PBRMaterialSet &materialSet = *Common->MaterialSet;


  Lights.resize(1);
  Light *light = &Lights[0];
  light->Dir = {-1, -1, 0};
  light->Type = LightType::Directional;
  light->Color = {1.0f, 1.0f, 1.0f};
  light->Intensity = 10.f;

  // Setup buffers
  {
    Assimp::Importer importer;
    const aiScene *sponzaScene =
        importer.ReadFile(createCommonResourcePath("sponza_crytek//sponza.obj"),
                          aiProcess_Triangulate | aiProcess_CalcTangentSpace);


    MeshGroups.resize(sponzaScene->mNumMeshes);

    uint32_t offset = 0;

    for(unsigned i = 0; i < sponzaScene->mNumMeshes; i++)
    {
      aiMesh* currentMesh = sponzaScene->mMeshes[i];

      MeshGroups[sponzaScene->mMeshes[i]->mMaterialIndex].NumIndies = currentMesh->mNumFaces * 3;
      MeshGroups[sponzaScene->mMeshes[i]->mMaterialIndex].Offset = offset;

      offset += MeshGroups[sponzaScene->mMeshes[i]->mMaterialIndex].NumIndies;
    }

    std::vector<Vertex>* vertices = new std::vector<Vertex>;
    std::vector<uint32_t>* indices = new std::vector<uint32_t>;

    // Build vertex chunk
    for(unsigned i = 0; i < sponzaScene->mNumMeshes; i++)
    {
      aiMesh* currentMesh = sponzaScene->mMeshes[i];
      uint32_t currntVerticesIndex = vertices->size();

      for(unsigned j = 0; j < currentMesh->mNumVertices; j++)
      {
        Vertex v = {};
        v.Pos = aiVector3DToFloat3(currentMesh->mVertices[j]);
        v.UV = aiVector3DToFloat2(currentMesh->mTextureCoords[0][j]);
        v.Normal = aiVector3DToFloat3(currentMesh->mNormals[j]);
        v.Tangent = aiVector3DToFloat3(currentMesh->mTangents[j]);

        vertices->push_back(v);
      }

      for(unsigned j = 0; j < currentMesh->mNumFaces; j++)
      {
          // Assume mesh is already triangulated.
          indices->emplace_back(currntVerticesIndex + currentMesh->mFaces[j].mIndices[0]);
          indices->emplace_back(currntVerticesIndex + currentMesh->mFaces[j].mIndices[1]);
          indices->emplace_back(currntVerticesIndex + currentMesh->mFaces[j].mIndices[2]);
      }
    }

  {
    Sponza.InstanceData.resize(Sponza.NumInstances);
    InstanceBlock &instanceData = Sponza.InstanceData[0];
    instanceData.ModelMat =
        Mat4::translate({0, 0, 0}) * Mat4::scale({0.1f, 0.1f, 0.1f});
    instanceData.InvModelMat = instanceData.ModelMat.inverse();

    Sponza.InstanceBuffer = createInstanceBuffer(Sponza.NumInstances);
    updateInstanceBufferMemory(Sponza.InstanceBuffer, Sponza.InstanceData);
  }
    
    Sponza.VertexBuffer = createVertexBuffer(*vertices);
    Sponza.NumVertices = vertices->size();
    Sponza.IndexBuffer = createIndexBuffer(*indices);
    Sponza.NumIndices = indices->size();

    delete vertices;
    delete indices;

    // const aiMesh *shaderBallMesh = shaderBallScene->mMeshes[0];
    // std::vector<Vertex> shaderBallVertices;
    // shaderBallVertices.reserve(shaderBallMesh->mNumFaces * 3);
    // for (unsigned int i = 0; i < shaderBallMesh->mNumFaces; ++i) {
    //   const aiFace &face = shaderBallMesh->mFaces[i];
    //   BB_ASSERT(face.mNumIndices == 3);

    //   for (int j = 0; j < 3; ++j) {
    //     unsigned int vi = face.mIndices[j];

    //     Vertex v = {};
    //     v.Pos = aiVector3DToFloat3(shaderBallMesh->mVertices[vi]);
    //     v.UV = aiVector3DToFloat2(shaderBallMesh->mTextureCoords[0][vi]);
    //     v.Normal = aiVector3DToFloat3(shaderBallMesh->mNormals[vi]);
    //     v.Tangent = aiVector3DToFloat3(shaderBallMesh->mTangents[vi]);
    //     shaderBallVertices.push_back(v);
    //   }
    }

    //Sponza.VertexBuffer = createVertexBuffer(shaderBallVertices);
    //Sponza.NumVertices = shaderBallVertices.size();
}

void SponzaScene::updateGUI(float /*_dt*/)
{

}

void SponzaScene::updateScene(float /*_dt*/)
{

}

void SponzaScene::drawScene(const Frame & _frame)
{
  VkCommandBuffer cmd = _frame.CmdBuffer;
  const StandardPipelineLayout &standardPipelineLayout =
      *Common->StandardPipelineLayout;

  vkCmdBindDescriptorSets(
      cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, standardPipelineLayout.Handle, 2, 1,
      &_frame.MaterialDescriptorSets[2], 0, nullptr);

  VkDeviceSize offset = 0;
  vkCmdBindVertexBuffers(cmd, 0, 1, &Sponza.VertexBuffer.Handle, &offset);
  vkCmdBindVertexBuffers(cmd, 1, 1, &Sponza.InstanceBuffer.Handle, &offset);
  vkCmdBindIndexBuffer(cmd, Sponza.IndexBuffer.Handle, 0, VK_INDEX_TYPE_UINT32);



  vkCmdDrawIndexed(cmd, Sponza.NumIndices, 1, 0, 0, 0);
}

SponzaScene::~SponzaScene() {
  const Renderer &renderer = *Common->Renderer;

  destroyBuffer(renderer, Sponza.VertexBuffer);
  destroyBuffer(renderer, Sponza.IndexBuffer);
  destroyBuffer(renderer, Sponza.InstanceBuffer);
}

} // namespace bb