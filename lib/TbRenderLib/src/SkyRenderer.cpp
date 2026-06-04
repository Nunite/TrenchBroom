/*
 Copyright (C) 2026 Kristian Duske

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#include "render/SkyRenderer.h"

#include "PreferenceManager.h"
#include "Preferences.h"
#include "fs/PathInfo.h"
#include "gl/ActiveShader.h"
#include "gl/Camera.h"
#include "gl/GlInterface.h"
#include "gl/Material.h"
#include "gl/MaterialManager.h"
#include "gl/PrimType.h"
#include "gl/ResourceManager.h"
#include "gl/Shaders.h"
#include "gl/Texture.h"
#include "gl/TextureResource.h"
#include "gl/VertexArray.h"
#include "gl/VertexType.h"
#include "mdl/Brush.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/EditorContext.h"
#include "mdl/Entity.h"
#include "mdl/EntityProperties.h"
#include "mdl/EntityNode.h"
#include "mdl/GameFileSystem.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/LoadTexture.h"
#include "mdl/Map.h"
#include "mdl/Node.h"
#include "mdl/PatchNode.h"
#include "mdl/WorldNode.h"
#include "render/RenderBatch.h"
#include "render/RenderContext.h"
#include "render/Renderable.h"

#include "kd/overload.h"
#include "kd/result.h"
#include "kd/string_compare.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace tb::render
{
namespace
{
using Vertex = gl::VertexTypes::P3UV2::Vertex;

std::string worldSkyname(const mdl::Map& map)
{
  if (const auto* skyname =
        map.worldNode().entity().property(mdl::EntityPropertyKeys::Skyname))
  {
    return *skyname;
  }
  return "";
}

std::shared_ptr<gl::TextureResource> textureResourceForMaterial(const gl::Material* material)
{
  return material ? std::shared_ptr<gl::TextureResource>{
                      const_cast<gl::TextureResource*>(&material->textureResource()),
                      [](gl::TextureResource*) {}}
                  : nullptr;
}

bool isSkyFace(const mdl::BrushFace& face)
{
  return kdl::ci::str_is_equal(face.attributes().materialName(), "sky");
}

std::shared_ptr<gl::TextureResource> loadLooseSkyTexture(
  mdl::GameFileSystem& gameFileSystem,
  gl::ResourceManager& resourceManager,
  const std::string& name)
{
  for (const auto& path : looseSkyMaterialPaths(name))
  {
    if (gameFileSystem.pathInfo(path) != fs::PathInfo::File)
    {
      continue;
    }

    auto texture = mdl::loadTexture(path, name, gameFileSystem);
    if (texture.is_error())
    {
      continue;
    }

    auto textureResource = gl::createTextureResource(std::move(texture) | kdl::value());
    resourceManager.addResource(textureResource);
    return textureResource;
  }

  return nullptr;
}

std::array<std::shared_ptr<gl::TextureResource>, 6> findSkyTextures(
  const gl::MaterialManager& materialManager,
  mdl::GameFileSystem& gameFileSystem,
  gl::ResourceManager& resourceManager,
  const std::string& skyname)
{
  auto textures = std::array<std::shared_ptr<gl::TextureResource>, 6>{};
  const auto names = skyMaterialNames(skyname);

  std::ranges::transform(names, textures.begin(), [&](const auto& name) {
    if (const auto* material = materialManager.material(name))
    {
      return textureResourceForMaterial(material);
    }
    return loadLooseSkyTexture(gameFileSystem, resourceManager, name);
  });

  return textures;
}

void addQuad(
  std::vector<Vertex>& vertices,
  const vm::vec3f& a,
  const vm::vec3f& b,
  const vm::vec3f& c,
  const vm::vec3f& d)
{
  vertices.emplace_back(a, vm::vec2f{0.0f, 0.0f});
  vertices.emplace_back(b, vm::vec2f{1.0f, 0.0f});
  vertices.emplace_back(c, vm::vec2f{1.0f, 1.0f});
  vertices.emplace_back(d, vm::vec2f{0.0f, 1.0f});
}

std::vector<Vertex> makeSkyBrushFaceVertices(const mdl::Map& map)
{
  auto vertices = std::vector<Vertex>{};

  const auto addBrush = [&](const mdl::BrushNode& brushNode) {
    if (!map.editorContext().visible(brushNode) || !map.editorContext().editable(brushNode))
    {
      return;
    }

    for (const auto& face : brushNode.brush().faces())
    {
      if (!isSkyFace(face) || !map.editorContext().visible(brushNode, face))
      {
        continue;
      }

      const auto faceVertices = face.vertexPositions();
      if (faceVertices.size() < 3)
      {
        continue;
      }

      const auto firstVertex = vm::vec3f{faceVertices.front()};
      for (size_t i = 1; i + 1 < faceVertices.size(); ++i)
      {
        vertices.emplace_back(firstVertex, vm::vec2f{0.0f, 0.0f});
        vertices.emplace_back(vm::vec3f{faceVertices[i + 1]}, vm::vec2f{0.0f, 0.0f});
        vertices.emplace_back(vm::vec3f{faceVertices[i]}, vm::vec2f{0.0f, 0.0f});
      }
    }
  };

  const auto visitNode = kdl::overload(
    [](auto&& thisLambda, const mdl::WorldNode& worldNode) {
      worldNode.visitChildren(thisLambda);
    },
    [](auto&& thisLambda, const mdl::LayerNode& layerNode) {
      layerNode.visitChildren(thisLambda);
    },
    [](auto&& thisLambda, const mdl::GroupNode& groupNode) {
      groupNode.visitChildren(thisLambda);
    },
    [](auto&& thisLambda, const mdl::EntityNode& entityNode) {
      entityNode.visitChildren(thisLambda);
    },
    [&](const mdl::BrushNode& brushNode) { addBrush(brushNode); },
    [](const mdl::PatchNode&) {});

  map.worldNode().accept(visitNode);
  return vertices;
}

class SkyRenderable : public DirectRenderable
{
private:
  std::array<std::shared_ptr<gl::TextureResource>, 6> m_textures;
  gl::VertexArray m_vertexArray;

public:
  SkyRenderable(
    std::array<std::shared_ptr<gl::TextureResource>, 6> textures,
    gl::VertexArray vertexArray)
    : m_textures{std::move(textures)}
    , m_vertexArray{std::move(vertexArray)}
  {
  }

  void prepare(gl::Gl& gl, gl::VboManager& vboManager) override
  {
    m_vertexArray.prepare(gl, vboManager);
  }

  void render(RenderContext& renderContext) override
  {
    auto& gl = renderContext.gl();
    auto shader =
      gl::ActiveShader{gl, renderContext.shaderManager(), gl::Shaders::SkyShader};

    gl.disable(GL_CULL_FACE);
    gl.enable(GL_TEXTURE_2D);
    gl.depthFunc(GL_LEQUAL);
    gl.activeTexture(GL_TEXTURE0);
    shader.set("Material", 0);
    shader.set("CameraPosition", renderContext.camera().position());

    if (m_vertexArray.setup(gl, shader.program()))
    {
      for (size_t i = 0; i < m_textures.size(); ++i)
      {
        const auto* texture = m_textures[i]->get();

        shader.set("SkyFaceIndex", static_cast<int>(i));
        texture->activate(
          gl, renderContext.minFilterMode(), renderContext.magFilterMode());
        m_vertexArray.render(gl, gl::PrimType::Triangles);
        texture->deactivate(gl);
      }
      m_vertexArray.cleanup(gl, shader.program());
    }

    gl.depthFunc(GL_LEQUAL);
    gl.enable(GL_CULL_FACE);
  }
};

} // namespace

SkyMaterialNames skyMaterialNames(const std::string& skyname)
{
  return {
    skyname + "rt",
    skyname + "bk",
    skyname + "lf",
    skyname + "ft",
    skyname + "up",
    skyname + "dn"};
}

std::array<std::filesystem::path, 6> looseSkyMaterialPaths(const std::string& name)
{
  return {
    std::filesystem::path{"gfx"} / "env" / (name + ".tga"),
    std::filesystem::path{"gfx"} / "env" / (name + ".bmp"),
    std::filesystem::path{"gfx"} / "env" / (name + ".png"),
    std::filesystem::path{"gfx"} / "env" / (name + ".jpg"),
    std::filesystem::path{"gfx"} / "env" / (name + ".jpeg"),
    std::filesystem::path{"gfx"} / "env" / (name + ".dds")};
}

bool shouldRenderSky(const bool render3D, const bool showSky, const std::string& skyname)
{
  return render3D && showSky && !skyname.empty();
}

bool skyMaterialsReady(const std::array<const gl::Material*, 6>& materials)
{
  return std::ranges::all_of(materials, [](const auto* material) {
    return material != nullptr;
  });
}

bool skyTexturesReady(const std::array<std::shared_ptr<gl::TextureResource>, 6>& textures)
{
  return std::ranges::all_of(textures, [](const auto& textureResource) {
    const auto* texture = textureResource ? textureResource->get() : nullptr;
    return texture && texture->isReady();
  });
}

SkyRenderer::SkyRenderer(mdl::Map& map)
  : m_map{map}
{
}

SkyRenderer::~SkyRenderer() = default;

void SkyRenderer::invalidate()
{
  m_cachedSkyname = std::nullopt;
  m_textures = {};
}

void SkyRenderer::render(RenderContext& renderContext, RenderBatch& renderBatch)
{
  const auto skyname = worldSkyname(m_map);
  if (!shouldRenderSky(
        renderContext.render3D(), pref(Preferences::ShowSky), skyname))
  {
    return;
  }

  if (!validate())
  {
    return;
  }

  auto skyBrushFaceVertices = makeSkyBrushFaceVertices(m_map);
  if (skyBrushFaceVertices.empty())
  {
    return;
  }

  renderBatch.addOneShot(new SkyRenderable{
    m_textures,
    gl::VertexArray::move(std::move(skyBrushFaceVertices))});
}

bool SkyRenderer::validate()
{
  const auto skyname = worldSkyname(m_map);
  if (m_cachedSkyname != skyname)
  {
    m_cachedSkyname = skyname;
    m_textures = findSkyTextures(
      m_map.materialManager(),
      m_map.gameFileSystem(),
      m_map.resourceManager(),
      skyname);
  }
  return skyTexturesReady(m_textures);
}

} // namespace tb::render
