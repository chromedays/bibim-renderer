#include "scene.h"
#include "resource.h"
#include "type_conversion.h"
#include "external/assimp/Importer.hpp"
#include "external/assimp/scene.h"
#include "external/assimp/postprocess.h"
#include "external/imgui/imgui_impl_vulkan.h"
#include "external/cgltf.h"
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
  light->Color = {1, 0, 0};
  light->Intensity = 200;
  ++light;
  light->Pos = {4, 2, 0};
  light->Dir = {0, -1, 0};
  light->Type = LightType::Point;
  light->Color = {0, 1, 0};
  light->Intensity = 200;
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

struct Mesh {
  VkPrimitiveTopology Topology;
  std::vector<Vertex> Vertices;
  std::vector<uint32_t> Indices;
};

GLTFScene::GLTFScene(CommonSceneResources *_common) : SceneBase(_common) {
  cgltf_options options = {};
  cgltf_data *data = nullptr;
  std::string path = createAbsolutePath("Cube/glTF/Cube.gltf");
  cgltf_result result = cgltf_parse_file(&options, path.c_str(), &data);
  BB_ASSERT(result == cgltf_result_success);
  cgltf_load_buffers(&options, data, path.c_str());

  std::unordered_map<std::string, Mesh> meshes;

  for (cgltf_size i = 0; i < data->meshes_count; ++i) {
    cgltf_mesh &meshData = data->meshes[i];
    for (cgltf_size j = 0; j < meshData.primitives_count; ++j) {
      cgltf_primitive &primitiveData = meshData.primitives[j];
      Mesh mesh = {};

      switch (primitiveData.type) {
      case cgltf_primitive_type_triangles:
        mesh.Topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        break;
      default:
        BB_ASSERT(false);
      }

      cgltf_accessor &indices = *primitiveData.indices;
      uint8_t *indicesData = (uint8_t *)indices.buffer_view->buffer->data;
      indicesData += indices.buffer_view->offset;
      indicesData += indices.offset;
      uint32_t minIndex = indices.has_min ? (uint32_t)indices.min[0] : 0;
      uint32_t maxIndex =
          indices.has_max ? (uint32_t)indices.max[0] : UINT32_MAX;
      for (cgltf_size k = 0; k < indices.count; ++k) {
        uint32_t index;
        if (indices.component_type == cgltf_component_type_r_16u) {
          index = ((uint16_t *)indicesData)[k];
        } else if (indices.component_type == cgltf_component_type_r_32u) {
          index = ((uint32_t *)indicesData)[k];
        } else {
          BB_ASSERT(false);
        }
        index = std::clamp(index, minIndex, maxIndex);
        mesh.Indices.push_back(index);
      }

      for (cgltf_size k = 0; k < primitiveData.attributes_count; ++k) {
        cgltf_attribute &attributeData = primitiveData.attributes[k];
        if (mesh.Vertices.size() < attributeData.data->count) {
          mesh.Vertices.resize(attributeData.data->count);
        }
        uint8_t *verticesData =
            (uint8_t *)attributeData.data->buffer_view->buffer->data;
        verticesData += attributeData.data->buffer_view->offset;
        verticesData += attributeData.data->offset;
        for (cgltf_size ii = 0; ii < attributeData.data->count; ++ii) {
          switch (attributeData.type) {
          case cgltf_attribute_type_position: {
            float *p = (float *)(verticesData + sizeof(float) * 3 * ii);
            mesh.Vertices[ii].Pos = {p[0], p[1], p[2]};
          } break;
          case cgltf_attribute_type_normal: {
            float *n = (float *)(verticesData + sizeof(float) * 3 * ii);
            mesh.Vertices[ii].Normal = {n[0], n[1], n[2]};
          } break;
          case cgltf_attribute_type_texcoord: {
            float *t = (float *)(verticesData + sizeof(float) * 2 * ii);
            mesh.Vertices[ii].UV = {t[0], t[1]};
          }
          }
        }
      }

      meshes.insert({std::string(meshData.name), mesh});
    }
  }

  VertexBuffer = createVertexBuffer(meshes["Cube"].Vertices);
  IndexBuffer = createIndexBuffer(meshes["Cube"].Indices);
  NumIndices = meshes["Cube"].Indices.size();
  InstanceBuffer = createInstanceBuffer(1);
  InstanceBlock instanceData[1] = {};
  instanceData[0].ModelMat = Mat4::translate({0, 0, 5});
  instanceData[0].InvModelMat = instanceData[0].ModelMat.inverse();
  updateInstanceBufferMemory(InstanceBuffer, instanceData);

  cgltf_free(data);

  Lights.resize(3);
  Light *light = &Lights[0];
  light->Dir = {-1, -1, 0};
  light->Type = LightType::Directional;
  light->Color = {0.2347f, 0.2131f, 0.2079f};
  light->Intensity = 1.f;
  ++light;
  light->Pos = {0, 2, 0};
  light->Type = LightType::Point;
  light->Color = {1, 0, 0};
  light->Intensity = 20;
  ++light;
  light->Pos = {4, 2, 0};
  light->Dir = {0, -1, 0};
  light->Type = LightType::Point;
  light->Color = {0, 1, 0};
  light->Intensity = 20;
  light->InnerCutOff = degToRad(30);
  light->OuterCutOff = degToRad(25);
}

GLTFScene::~GLTFScene() {
  const Renderer &renderer = *Common->Renderer;

  destroyBuffer(renderer, InstanceBuffer);
  destroyBuffer(renderer, IndexBuffer);
  destroyBuffer(renderer, VertexBuffer);
}

void GLTFScene::updateGUI(float _dt) {}

void GLTFScene::updateScene(float _dt) {}

void GLTFScene::drawScene(const Frame &_frame) {
  VkCommandBuffer cmd = _frame.CmdBuffer;
  const StandardPipelineLayout &standardPipelineLayout =
      *Common->StandardPipelineLayout;

  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          standardPipelineLayout.Handle, 2, 1,
                          &_frame.MaterialDescriptorSets[0], 0, nullptr);

  VkDeviceSize offset = 0;
  vkCmdBindVertexBuffers(cmd, 0, 1, &VertexBuffer.Handle, &offset);
  vkCmdBindVertexBuffers(cmd, 1, 1, &InstanceBuffer.Handle, &offset);
  vkCmdBindIndexBuffer(cmd, IndexBuffer.Handle, 0, VK_INDEX_TYPE_UINT32);
  vkCmdDrawIndexed(cmd, NumIndices, 1, 0, 0, 0);
}

} // namespace bb