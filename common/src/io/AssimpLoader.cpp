/*
 Copyright (C) 2023 Daniel Walder
 Copyright (C) 2022 Amara M. Kilic
 Copyright (C) 2022 Kristian Duske

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

#include "AssimpLoader.h"

#include "Logger.h"
#include "ReaderException.h"
#include "io/File.h"
#include "io/FileSystem.h"
#include "io/MaterialUtils.h"
#include "io/ParserException.h"
#include "io/PathInfo.h"
#include "io/ReadFreeImageTexture.h"
#include "io/ResourceUtils.h"
#include "mdl/BrushFaceAttributes.h"
#include "mdl/EntityModel.h"
#include "mdl/Material.h"
#include "mdl/Texture.h"
#include "render/IndexRangeMapBuilder.h"
#include "render/PrimType.h"

#include "kdl/path_utils.h"
#include "kdl/ranges/as_rvalue_view.h"
#include "kdl/ranges/to.h"
#include "kdl/result.h"
#include "kdl/result_fold.h"
#include "kdl/string_format.h"
#include "kdl/vector_utils.h"

#include <assimp/IOStream.hpp>
#include <assimp/IOSystem.hpp>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/types.h>
#include <fmt/format.h>
#include <fmt/std.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <optional>
#include <ranges>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>


namespace tb::io
{

namespace
{

class AssimpIOStream : public Assimp::IOStream
{
  friend class AssimpIOSystem;

private:
  std::shared_ptr<File> m_file;
  Reader m_reader;

public:
  explicit AssimpIOStream(std::shared_ptr<File> file)
    : m_file{std::move(file)}
    , m_reader{m_file->reader()}
  {
  }

  size_t Read(void* buffer, const size_t size, const size_t count) override
  {
    if (m_reader.canRead(size * count))
    {
      m_reader.read(reinterpret_cast<char*>(buffer), size * count);
      return count;
    }
    return 0;
  }

  size_t Write(
    const void* /* buffer */, const size_t /* size */, const size_t /* count */) override
  {
    return 0; // unsupported
  }

  aiReturn Seek(const size_t offset, const aiOrigin origin) override
  {
    try
    {
      switch (origin)
      {
      case aiOrigin_SET:
        m_reader.seekFromBegin(offset);
        return aiReturn_SUCCESS;
      case aiOrigin_CUR:
        m_reader.seekForward(offset);
        return aiReturn_SUCCESS;
      case aiOrigin_END:
        m_reader.seekFromEnd(offset);
        return aiReturn_SUCCESS;
      case _AI_ORIGIN_ENFORCE_ENUM_SIZE:
        break;
      }
    }
    catch (const ReaderException& /*e*/)
    {
      return aiReturn_FAILURE;
    }
    return aiReturn_FAILURE;
  }

  size_t Tell() const override { return m_reader.position(); }

  size_t FileSize() const override { return m_reader.size(); }

  // No writing.
  void Flush() override {}
};

class AssimpIOSystem : public Assimp::IOSystem
{
private:
  const FileSystem& m_fs;

public:
  explicit AssimpIOSystem(const FileSystem& fs)
    : m_fs{fs}
  {
  }

  bool Exists(const char* path) const override
  {
    return m_fs.pathInfo(std::filesystem::path{path}) == PathInfo::File;
  }

  char getOsSeparator() const override
  {
    return std::filesystem::path::preferred_separator;
  }

  void Close(Assimp::IOStream* file) override { delete file; }

  Assimp::IOStream* Open(const char* path, const char* mode) override
  {
    if (mode[0] != 'r')
    {
      throw ParserException{"Assimp attempted to open a file not for reading."};
    }

    return (m_fs.openFile(path) | kdl::transform([](auto file) {
              return std::make_unique<AssimpIOStream>(std::move(file));
            })
            | kdl::if_error([](auto e) { throw ParserException{e.msg}; }) | kdl::value())
      .release();
  }
};

std::optional<mdl::Texture> loadFallbackTexture(const FileSystem& fs)
{
  static const auto NoTextureName = mdl::BrushFaceAttributes::NoMaterialName;

  static const auto texturePaths = std::vector<std::filesystem::path>{
    "textures" / kdl::path_add_extension(NoTextureName, ".png"),
    "textures" / kdl::path_add_extension(NoTextureName, ".jpg"),
    kdl::path_add_extension(NoTextureName, ".png"),
    kdl::path_add_extension(NoTextureName, ".jpg"),
  };

  return texturePaths | kdl::first([&](const auto& texturePath) {
           return fs.openFile(texturePath) | kdl::and_then([](auto file) {
                    auto reader = file->reader().buffer();
                    return readFreeImageTexture(reader);
                  });
         });
}

mdl::Texture loadFallbackOrDefaultTexture(const FileSystem& fs, Logger& logger)
{
  if (auto fallbackTexture = loadFallbackTexture(fs))
  {
    return std::move(*fallbackTexture);
  }
  return loadDefaultTexture(fs, logger);
}

mdl::Texture loadTextureFromFileSystem(
  const std::filesystem::path& path, const FileSystem& fs, Logger& logger)
{
  return fs.openFile(path) | kdl::and_then([](auto file) {
           auto reader = file->reader().buffer();
           return readFreeImageTexture(reader);
         })
         | kdl::or_else(makeReadTextureErrorHandler(fs, logger)) | kdl::value();
}

mdl::Texture loadUncompressedEmbeddedTexture(
  const aiTexel& data, const size_t width, const size_t height)
{
  auto buffer = mdl::TextureBuffer{width * height * sizeof(aiTexel)};
  std::memcpy(buffer.data(), &data, width * height * sizeof(aiTexel));

  auto masked = false;
  auto* bytes = buffer.data();
  const auto byteCount = buffer.size();
  for (size_t i = 0; i + 3 < byteCount; i += sizeof(aiTexel))
  {
    if (bytes[i + 3] != static_cast<unsigned char>(0xFF))
    {
      masked = true;
      break;
    }
  }

  if (!masked && width > 0u && height > 0u)
  {
    struct Bgr
    {
      unsigned char b;
      unsigned char g;
      unsigned char r;
    };

    const auto pixelIndex = [&](const size_t x, const size_t y) {
      return (y * width + x) * sizeof(aiTexel);
    };

    const auto readBgr = [&](const size_t x, const size_t y) {
      const auto i = pixelIndex(x, y);
      return Bgr{bytes[i + 0], bytes[i + 1], bytes[i + 2]};
    };

    const auto corners = std::array<Bgr, 4>{
      readBgr(0u, 0u),
      readBgr(width - 1u, 0u),
      readBgr(0u, height - 1u),
      readBgr(width - 1u, height - 1u),
    };

    auto cornerCandidates = std::vector<Bgr>{};
    cornerCandidates.reserve(4);
    for (const auto& c : corners)
    {
      const auto alreadyPresent = std::any_of(
        cornerCandidates.begin(),
        cornerCandidates.end(),
        [&](const Bgr& e) { return e.b == c.b && e.g == c.g && e.r == c.r; });
      if (!alreadyPresent)
      {
        const auto cornerMatches = std::count_if(
          corners.begin(),
          corners.end(),
          [&](const Bgr& e) { return e.b == c.b && e.g == c.g && e.r == c.r; });
        if (cornerMatches >= 2)
        {
          cornerCandidates.push_back(c);
        }
      }
    }

    auto best = std::optional<std::pair<Bgr, size_t>>{};
    for (const auto& candidate : cornerCandidates)
    {
      size_t count = 0u;
      for (size_t i = 0; i + 3 < byteCount; i += sizeof(aiTexel))
      {
        if (
          bytes[i + 0] == candidate.b && bytes[i + 1] == candidate.g
          && bytes[i + 2] == candidate.r)
        {
          ++count;
        }
      }

      if (!best || count > best->second)
      {
        best = std::make_pair(candidate, count);
      }
    }

    if (best)
    {
      const auto totalPixels = width * height;
      const auto minKeyPixels = totalPixels / 16u;
      if (best->second >= minKeyPixels)
      {
        const auto& key = best->first;
        for (size_t i = 0; i + 3 < byteCount; i += sizeof(aiTexel))
        {
          if (bytes[i + 0] == key.b && bytes[i + 1] == key.g && bytes[i + 2] == key.r)
          {
            bytes[i + 3] = 0u;
          }
        }
        masked = true;
      }
    }
  }

  const auto averageColor = getAverageColor(buffer, GL_BGRA);
  return {
    width,
    height,
    averageColor,
    GL_BGRA,
    masked ? mdl::TextureMask::On : mdl::TextureMask::Off,
    mdl::NoEmbeddedDefaults{},
    std::move(buffer)};
}

mdl::Texture loadCompressedEmbeddedTexture(
  const aiTexel& data, const size_t size, const FileSystem& fs, Logger& logger)
{
  return readFreeImageTextureFromMemory(reinterpret_cast<const uint8_t*>(&data), size)
         | kdl::or_else(makeReadTextureErrorHandler(fs, logger)) | kdl::value();
}

mdl::Texture loadTexture(
  const aiTexture* texture,
  const std::filesystem::path& texturePath,
  const std::filesystem::path& modelPath,
  const FileSystem& fs,
  Logger& logger)
{
  if (!texture)
  {
    // The texture is not embedded. Load it using the file system.
    const auto filePath = modelPath.parent_path() / texturePath;
    return loadTextureFromFileSystem(filePath, fs, logger);
  }

  if (texture->mHeight != 0)
  {
    // The texture is uncompressed, load it directly.
    return loadUncompressedEmbeddedTexture(
      *texture->pcData, texture->mWidth, texture->mHeight);
  }

  // The texture is embedded, but compressed. Let FreeImage load it from memory.
  return loadCompressedEmbeddedTexture(*texture->pcData, texture->mWidth, fs, logger);
}

struct GoldSrcTextureLibrary
{
  std::unordered_map<std::string, std::shared_ptr<mdl::TextureResource>> m_textures;
};

static constexpr int32_t GoldSrcStudioId_IDST = 0x54534449;
static constexpr int32_t GoldSrcStudioVersion = 10;
static constexpr int32_t STUDIO_NF_MASKED = 0x0040;

struct GoldSrcStudioHdr
{
  int32_t id{};
  int32_t version{};
  char name[64]{};
  int32_t length{};

  float eyeposition[3]{};
  float min[3]{};
  float max[3]{};
  float bbmin[3]{};
  float bbmax[3]{};

  int32_t flags{};

  int32_t numbones{};
  int32_t boneindex{};

  int32_t numbonecontrollers{};
  int32_t bonecontrollerindex{};

  int32_t numhitboxes{};
  int32_t hitboxindex{};

  int32_t numseq{};
  int32_t seqindex{};

  int32_t numseqgroups{};
  int32_t seqgroupindex{};

  int32_t numtextures{};
  int32_t textureindex{};
  int32_t texturedataindex{};

  int32_t numskinref{};
  int32_t numskinfamilies{};
  int32_t skinindex{};

  int32_t numbodyparts{};
  int32_t bodypartindex{};

  int32_t numattachments{};
  int32_t attachmentindex{};

  int32_t soundtable{};
  int32_t soundindex{};
  int32_t soundgroups{};
  int32_t soundgroupindex{};

  int32_t numtransitions{};
  int32_t transitionindex{};
};

struct GoldSrcMStudioTexture
{
  char name[64]{};
  int32_t flags{};
  int32_t width{};
  int32_t height{};
  int32_t index{};
};

template <typename T>
const T* ptrAt(const uint8_t* base, const size_t size, const int32_t offset)
{
  if (offset < 0)
  {
    return nullptr;
  }

  const auto off = size_t(offset);
  if (off > size || size - off < sizeof(T))
  {
    return nullptr;
  }

  return reinterpret_cast<const T*>(base + off);
}

std::filesystem::path addSuffixToFileName(
  const std::filesystem::path& filePath, const std::string_view suffix)
{
  const auto stem = filePath.stem().string();
  const auto ext = filePath.extension().string();
  return filePath.parent_path() / (stem + std::string{suffix} + ext);
}

std::string_view cStringView(const char (&s)[64])
{
  const auto* begin = &s[0];
  const auto* end = static_cast<const char*>(std::memchr(begin, '\0', 64));
  if (!end)
  {
    end = begin + 64;
  }
  return std::string_view{begin, size_t(end - begin)};
}

void insertTextureKey(
  GoldSrcTextureLibrary& lib,
  const std::string_view key,
  const std::shared_ptr<mdl::TextureResource>& textureResource)
{
  if (key.empty())
  {
    return;
  }

  lib.m_textures.try_emplace(kdl::str_to_lower(key), textureResource);
}

std::optional<std::shared_ptr<mdl::TextureResource>> findGoldSrcTexture(
  const GoldSrcTextureLibrary& lib, const std::string_view raw)
{
  const auto rawLower = kdl::str_to_lower(raw);
  if (const auto it = lib.m_textures.find(rawLower); it != lib.m_textures.end())
  {
    return it->second;
  }

  const auto sep = raw.find_last_of("/\\");
  const auto basename = (sep == std::string_view::npos) ? raw : raw.substr(sep + 1u);

  const auto basenameLower = kdl::str_to_lower(basename);
  if (const auto it = lib.m_textures.find(basenameLower); it != lib.m_textures.end())
  {
    return it->second;
  }

  const auto dot = basename.find_last_of('.');
  if (dot != std::string_view::npos)
  {
    const auto stemLower = kdl::str_to_lower(basename.substr(0u, dot));
    if (const auto it = lib.m_textures.find(stemLower); it != lib.m_textures.end())
    {
      return it->second;
    }
  }

  return std::nullopt;
}

Result<GoldSrcTextureLibrary> loadGoldSrcTextureLibraryFromFile(
  const FileSystem& fs, const std::filesystem::path& mdlPath, Logger& logger)
{
  return fs.openFile(mdlPath) | kdl::and_then([&](auto file) -> Result<GoldSrcTextureLibrary> {
           auto reader = file->reader().buffer();
           const auto buffered = reader.buffer();
           const auto* begin = reinterpret_cast<const uint8_t*>(buffered.begin());
           const auto size = buffered.size();

           const auto* header = ptrAt<GoldSrcStudioHdr>(begin, size, 0);
           if (!header || header->id != GoldSrcStudioId_IDST || header->version != GoldSrcStudioVersion)
           {
             return Error{"Not a GoldSrc Studio MDL"};
           }

           if (header->numtextures <= 0 || header->textureindex <= 0)
           {
             return Error{"GoldSrc Studio MDL has no textures"};
           }

           const auto textureCount = size_t(header->numtextures);
           if (textureCount > (std::numeric_limits<size_t>::max() / sizeof(GoldSrcMStudioTexture)))
           {
             return Error{"GoldSrc Studio MDL has too many textures"};
           }

           const auto* textures =
             ptrAt<GoldSrcMStudioTexture>(begin, size, header->textureindex);
           if (!textures)
           {
             return Error{"GoldSrc Studio MDL texture index is out of bounds"};
           }

           auto out = GoldSrcTextureLibrary{};
           for (size_t ti = 0; ti < textureCount; ++ti)
           {
             const auto& tex = textures[ti];
             if (tex.width <= 0 || tex.height <= 0)
             {
               continue;
             }

             const auto width = size_t(tex.width);
             const auto height = size_t(tex.height);

             if (!checkTextureDimensions(width, height))
             {
               continue;
             }

             if (width > std::numeric_limits<size_t>::max() / height)
             {
               continue;
             }

             const auto pixelCount = width * height;
             const auto bytesNeeded = pixelCount + 256u * 3u;
             if (tex.index < 0)
             {
               continue;
             }

             const auto indexOffset = size_t(tex.index);
             if (indexOffset > size || size - indexOffset < bytesNeeded)
             {
               continue;
             }

             const auto* indices = begin + indexOffset;
             const auto* palette = indices + pixelCount;

             auto rgba = mdl::TextureBuffer{pixelCount * 4u};
             auto* outPixels = rgba.data();

             const auto masked = (tex.flags & STUDIO_NF_MASKED) != 0;
             for (size_t i = 0; i < pixelCount; ++i)
             {
               const auto index = size_t(indices[i]);
               const auto paletteOffset = index * 3u;
               const auto pixelOffset = i * 4u;

               outPixels[pixelOffset + 0] = palette[paletteOffset + 0];
               outPixels[pixelOffset + 1] = palette[paletteOffset + 1];
               outPixels[pixelOffset + 2] = palette[paletteOffset + 2];
               outPixels[pixelOffset + 3] = 0xFF;

               if (masked && index == 255u)
               {
                 outPixels[pixelOffset + 0] = 0u;
                 outPixels[pixelOffset + 1] = 0u;
                 outPixels[pixelOffset + 2] = 0u;
                 outPixels[pixelOffset + 3] = 0u;
               }
             }

             const auto averageColor = getAverageColor(rgba, GL_RGBA);
             auto texture = mdl::Texture{
               width,
               height,
               averageColor,
               GL_RGBA,
               masked ? mdl::TextureMask::On : mdl::TextureMask::Off,
               mdl::NoEmbeddedDefaults{},
               std::move(rgba)};

             auto textureResource = createTextureResource(std::move(texture));
             const auto name = cStringView(tex.name);
             insertTextureKey(out, name, textureResource);

             const auto sep = name.find_last_of("/\\");
             const auto basename =
               (sep == std::string_view::npos) ? name : name.substr(sep + 1u);
             insertTextureKey(out, basename, textureResource);

             const auto dot = basename.find_last_of('.');
             if (dot != std::string_view::npos)
             {
               insertTextureKey(out, basename.substr(0u, dot), textureResource);
             }
           }

           if (out.m_textures.empty())
           {
             logger.error(fmt::format("GoldSrc Studio MDL '{}' contained no usable textures", mdlPath));
           }

           return out;
         });
}

std::optional<GoldSrcTextureLibrary> tryLoadGoldSrcTextureLibrary(
  const FileSystem& fs, const std::filesystem::path& modelPath, Logger& logger)
{
  const auto primary = loadGoldSrcTextureLibraryFromFile(fs, modelPath, logger);
  if (primary && !primary.value().m_textures.empty())
  {
    return primary.value();
  }

  const auto textureMdlPath = addSuffixToFileName(modelPath, "T");
  const auto fallback = loadGoldSrcTextureLibraryFromFile(fs, textureMdlPath, logger);
  if (fallback && !fallback.value().m_textures.empty())
  {
    return fallback.value();
  }

  return std::nullopt;
}

std::optional<std::filesystem::path> tryMakeTexturePath(
  const aiString& path, Logger& logger, const std::filesystem::path& modelPath)
{
  const auto raw = std::string_view{path.C_Str()};

  try
  {
#ifdef _WIN32
    auto u8 = std::u8string{};
    u8.reserve(raw.size());
    for (const auto c : raw)
    {
      u8.push_back(char8_t(static_cast<unsigned char>(c)));
    }
    return kdl::parse_path(std::move(u8));
#else
    return kdl::parse_path(std::string{raw});
#endif
  }
  catch (const std::exception& e)
  {
    logger.error(fmt::format("Failed to parse texture path '{}' for model '{}': {}", raw, modelPath, e.what()));
    return std::nullopt;
  }
}

std::vector<mdl::Material> loadMaterialsForMaterial(
  const aiScene& scene,
  const size_t materialIndex,
  const std::filesystem::path& modelPath,
  const FileSystem& fs,
  Logger& logger,
  const std::optional<GoldSrcTextureLibrary>& goldSrcTextures)
{
  auto materials = std::vector<mdl::Material>{};

  // Is there even a single diffuse texture? If not, fail and load fallback texture.
  const auto textureCount =
    scene.mMaterials[materialIndex]->GetTextureCount(aiTextureType_DIFFUSE);
  if (textureCount > 0)
  {
    // load up every diffuse texture
    for (unsigned int ti = 0; ti < textureCount; ++ti)
    {
      auto path = aiString{};
      scene.mMaterials[materialIndex]->GetTexture(aiTextureType_DIFFUSE, ti, &path);

      if (goldSrcTextures)
      {
        if (const auto texRes = findGoldSrcTexture(*goldSrcTextures, path.C_Str()))
        {
          materials.emplace_back("", *texRes);
          continue;
        }
      }

      const auto texturePath = tryMakeTexturePath(path, logger, modelPath);
      if (!texturePath)
      {
        continue;
      }
      const auto* texture = scene.GetEmbeddedTexture(path.C_Str());
      auto loadedTexture = loadTexture(texture, *texturePath, modelPath, fs, logger);
      auto textureResource = createTextureResource(std::move(loadedTexture));
      materials.emplace_back("", std::move(textureResource));
    }
  }
  else
  {
    logger.error(fmt::format(
      "No diffuse textures found for material {} of model '{}', loading fallback texture",
      materialIndex,
      modelPath));

    auto fallbackTexture = loadFallbackOrDefaultTexture(fs, logger);
    auto textureResource = createTextureResource(std::move(fallbackTexture));
    materials.emplace_back("", std::move(textureResource));
  }

  if (materials.empty())
  {
    auto fallbackTexture = loadFallbackOrDefaultTexture(fs, logger);
    auto textureResource = createTextureResource(std::move(fallbackTexture));
    materials.emplace_back("", std::move(textureResource));
  }

  return materials;
}

struct AssimpComputedMeshData
{
  size_t m_meshIndex;
  std::vector<mdl::EntityModelVertex> m_vertices;
  render::IndexRangeMap m_indices;
};

struct AssimpBoneInformation
{
  size_t m_boneIndex;
  int32_t m_parentIndex;
  aiString m_name;
  aiMatrix4x4 m_localTransform;
  aiMatrix4x4 m_globalTransform;
};

struct AssimpVertexBoneWeight
{
  size_t m_boneIndex;
  float m_weight;
  const aiBone& m_bone;
};

/**
 * Gets the index for a particular assimp mesh.
 * Assimp meshes do not have unique names so we match by reference.
 */
std::optional<size_t> getMeshIndex(const aiScene& scene, const aiMesh& mesh)
{
  for (unsigned int i = 0; i < scene.mNumMeshes; ++i)
  {
    if (&mesh == scene.mMeshes[i])
    {
      return size_t(i);
    }
  }
  return std::nullopt;
}

/**
 * Gets the channel index for a particular assimp node, matched by name.
 */
std::optional<size_t> getChannelIndex(const aiAnimation& animation, const aiNode& node)
{
  const auto nodeName = std::string_view{node.mName.data};
  for (unsigned int i = 0; i < animation.mNumChannels; ++i)
  {
    const auto channelName = std::string_view{animation.mChannels[i]->mNodeName.data};
    if (channelName == nodeName)
    {
      return size_t(i);
    }
  }
  return std::nullopt;
}

/**
 * Gets the parent bone's channel index as well as its transformation,
 * recursively multiplied by all bones further up the hierarchy.
 */
std::tuple<size_t, aiMatrix4x4> getBoneParentChannelAndTransformation(
  const aiAnimation& animation,
  const aiNode& boneNode,
  const std::vector<aiMatrix4x4>& channelTransforms)
{
  const auto* parentNode = boneNode.mParent;
  if (!parentNode)
  {
    // reached the root node
    return {-1, aiMatrix4x4{}};
  }

  if (const auto index = getChannelIndex(animation, *parentNode))
  {
    // we have found the index of this bone in the channel list
    const auto [parentIndex, parentTransform] =
      getBoneParentChannelAndTransformation(animation, *parentNode, channelTransforms);
    unused(parentIndex);

    return {*index, parentTransform * channelTransforms[*index]};
  }

  // this node is not a bone, use the node's default transformation
  return {-1, parentNode->mTransformation};
}

/**
 * Computes the animation information for each channel in an
 * animation sequence. Always uses the first frame of the animation.
 */
std::vector<AssimpBoneInformation> getAnimationInformation(
  const aiNode& root, const aiAnimation& animation)
{
  auto indivTransforms = std::vector<aiMatrix4x4>{};
  indivTransforms.reserve(animation.mNumChannels);

  // calculate the transformations for each animation channel
  for (unsigned int i = 0; i < animation.mNumChannels; ++i)
  {
    const auto& channel = *animation.mChannels[i];

    const auto position = channel.mNumPositionKeys > 0 ? channel.mPositionKeys[0].mValue
                                                       : aiVector3D{0, 0, 0};
    const auto rotation = channel.mNumRotationKeys > 0 ? channel.mRotationKeys[0].mValue
                                                       : aiQuaternion{1, 0, 0, 0};
    const auto scale =
      channel.mNumScalingKeys > 0 ? channel.mScalingKeys[0].mValue : aiVector3D{1, 1, 1};

    // build a transformation matrix from it
    auto mat = aiMatrix4x4{rotation.GetMatrix()};
    mat.a1 *= scale.x;
    mat.b1 *= scale.x;
    mat.c1 *= scale.x;
    mat.a2 *= scale.y;
    mat.b2 *= scale.y;
    mat.c2 *= scale.y;
    mat.a3 *= scale.z;
    mat.b3 *= scale.z;
    mat.c3 *= scale.z;
    mat.a4 = position.x;
    mat.b4 = position.y;
    mat.c4 = position.z;

    indivTransforms.push_back(std::move(mat));
  }

  // assemble the transform information from the bone hierarchy (child
  // bones must be multiplied by their parent transformations, recursively)
  auto transforms = std::vector<AssimpBoneInformation>{};
  transforms.reserve(animation.mNumChannels);

  for (unsigned int i = 0; i < animation.mNumChannels; ++i)
  {
    const auto& channel = *animation.mChannels[i];

    // traverse the bone hierarchy to compute the global transformation
    if (const auto* boneNode = root.FindNode(channel.mNodeName))
    {
      const auto [parentId, parentTransform] =
        getBoneParentChannelAndTransformation(animation, *boneNode, indivTransforms);

      transforms.push_back(
        {i,
         int32_t(parentId),
         channel.mNodeName,
         indivTransforms[i],
         parentTransform * indivTransforms[i]});
    }
    else
    {
      // couldn't find the bone node, something is weird
      transforms.emplace_back();
    }
  }

  return transforms;
}

void processNode(
  std::vector<AssimpMeshWithTransforms>& meshes,
  const aiNode& node,
  const aiScene& scene,
  const aiMatrix4x4& transform,
  const aiMatrix4x4& axisTransform)
{
  for (unsigned int i = 0; i < node.mNumMeshes; ++i)
  {
    const auto* mesh = scene.mMeshes[node.mMeshes[i]];
    meshes.push_back({mesh, transform, axisTransform});
  }

  for (unsigned int i = 0; i < node.mNumChildren; ++i)
  {
    processNode(
      meshes,
      *node.mChildren[i],
      scene,
      transform * node.mChildren[i]->mTransformation,
      axisTransform);
  }
}

const auto AiMdlHl1NodeBodyparts = "<MDL_bodyparts>";

void processRootNode(
  std::vector<AssimpMeshWithTransforms>& meshes,
  const aiNode& node,
  const aiScene& scene,
  const aiMatrix4x4& transform,
  const aiMatrix4x4& axisTransform)
{
  // HL1 models have a slightly different structure than normal, the format consists
  // of multiple body parts, and each body part has one or more submodels. Only one
  // submodel per body part should be rendered at a time.

  // See if we have loaded a HL1 model
  if (const auto hl1BodyParts = node.FindNode(AiMdlHl1NodeBodyparts))
  {
    // HL models are loaded by assimp in a particular way, each bodypart and all
    // its submodels are loaded into different nodes in the scene. To properly
    // display the model, we must choose EXACTLY ONE submodel from each body
    // part and render the meshes for those chosen submodels.

    // loop through each body part
    for (unsigned int i = 0; i < hl1BodyParts->mNumChildren; ++i)
    {
      const auto& bodypart = *hl1BodyParts->mChildren[i];
      if (bodypart.mNumChildren > 0)
      {
        // currently we don't have a way to know which submodel the user
        // might want to see, so just use the first one.
        processNode(meshes, *bodypart.mChildren[0], scene, transform, axisTransform);
      }
    }
  }
  else
  {
    // not a HL1 model, just process like normal
    processNode(meshes, node, scene, transform, axisTransform);
  }
}

std::optional<size_t> getBoneIndexByName(
  const std::vector<AssimpBoneInformation>& boneTransforms, const aiBone& bone)
{
  const auto boneName = std::string_view{bone.mName.data};
  for (size_t i = 0; i < boneTransforms.size(); ++i)
  {
    const auto boneTransformName = std::string_view{boneTransforms[i].m_name.data};
    if (boneTransformName == boneName)
    {
      return i;
    }
  }
  return std::nullopt;
}

Result<std::vector<mdl::EntityModelVertex>> computeMeshVertices(
  const aiMesh& mesh,
  const aiMatrix4x4& transform,
  const aiMatrix4x4& axisTransform,
  const std::vector<AssimpBoneInformation>& boneTransforms)
{
  auto vertices = std::vector<mdl::EntityModelVertex>{};

  // We pass through the aiProcess_Triangulate flag to assimp, so we know for sure we'll
  // ONLY get triangles in a single mesh. This is just a safety net to make sure we don't
  // do anything bad.
  if (!(mesh.mPrimitiveTypes & aiPrimitiveType_TRIANGLE))
  {
    return vertices;
  }

  // the weights for each vertex are stored in the bones, not in
  // the vertices. this loop lets us collect the bone weightings
  // per vertex so we can process them.
  auto weightsPerVertex =
    std::vector<std::vector<AssimpVertexBoneWeight>>{mesh.mNumVertices};
  for (unsigned int i = 0; i < mesh.mNumBones; ++i)
  {
    const auto& bone = *mesh.mBones[i];

    // find the bone with the matching name
    if (const auto boneIndex = getBoneIndexByName(boneTransforms, bone))
    {
      for (unsigned int weightIndex = 0; weightIndex < bone.mNumWeights; ++weightIndex)
      {
        const auto vertexIndex = bone.mWeights[weightIndex].mVertexId;
        if (vertexIndex >= mesh.mNumVertices)
        {
          return Error{fmt::format("Invalid vertex index {}", vertexIndex)};
        }

        weightsPerVertex[vertexIndex].push_back(
          {*boneIndex, bone.mWeights[weightIndex].mWeight, bone});
      }
    }
  }

  const auto numVerts = mesh.mNumVertices;
  vertices.reserve(numVerts);

  // Add all the vertices of the mesh.
  for (unsigned int i = 0; i < numVerts; ++i)
  {
    const auto uvCoords =
      mesh.mTextureCoords[0]
        ? vm::vec2f{mesh.mTextureCoords[0][i].x, mesh.mTextureCoords[0][i].y}
        : vm::vec2f{0.0f, 0.0f};

    auto meshVertices = mesh.mVertices[i];

    // Bone indices and weights
    if (mesh.HasBones() && !boneTransforms.empty() && !weightsPerVertex[i].empty())
    {
      const auto vertWeights = weightsPerVertex[i];
      auto vertPos = aiVector3D{};

      for (const auto& vertWeight : vertWeights)
      {
        const auto boneIndex = vertWeight.m_boneIndex;
        const auto weight = vertWeight.m_weight;
        const auto& bone = vertWeight.m_bone;
        if (boneIndex < boneTransforms.size())
        {
          const auto& boneTransform = boneTransforms[boneIndex];
          auto weightedPosition = meshVertices;
          weightedPosition *= bone.mOffsetMatrix;
          weightedPosition *= boneTransform.m_globalTransform;
          weightedPosition *= weight;
          vertPos += weightedPosition;
        }
      }

      meshVertices = vertPos;
    }

    meshVertices *= transform;
    meshVertices *= axisTransform;

    vertices.emplace_back(
      vm::vec3f{meshVertices.x, meshVertices.y, meshVertices.z}, uvCoords);
  }

  return vertices;
}

AssimpComputedMeshData computeMeshData(
  const AssimpMeshWithTransforms& mesh,
  const size_t meshIndex,
  const std::vector<mdl::EntityModelVertex>& vertices)
{
  // build the mesh faces as triangles
  const auto numTriangles = mesh.m_mesh->mNumFaces;
  const auto numIndices = numTriangles * 3;

  auto size = render::IndexRangeMap::Size{};
  size.inc(render::PrimType::Triangles, numTriangles);
  auto builder =
    render::IndexRangeMapBuilder<mdl::EntityModelVertex::Type>{numIndices, size};

  for (unsigned int i = 0; i < numTriangles; ++i)
  {
    const auto& face = mesh.m_mesh->mFaces[i];

    // ignore anything that's not a triangle
    if (face.mNumIndices == 3)
    {
      builder.addTriangle(
        vertices[face.mIndices[0]],
        vertices[face.mIndices[1]],
        vertices[face.mIndices[2]]);
    }
  }

  return {meshIndex, builder.vertices(), builder.indices()};
}

bool useQuakeCoordinateSystem(const aiScene& scene)
{
  const auto isHl1 = scene.mRootNode->FindNode(AiMdlHl1NodeBodyparts) != nullptr;
  return isHl1;
}

aiMatrix4x4 getAxisTransform(const aiScene& scene)
{
  // These MUST be in32_t, or the metadata 'Get' function will get confused.
  int32_t xAxis = 0, yAxis = 0, zAxis = 0, xAxisSign = 0, yAxisSign = 0, zAxisSign = 0;
  auto unitScale = 0.0f;

  // TrenchBroom expects models to use the following coordinate system:
  // +X out of the screen (forward)
  // +Y points right
  // +Z points up
  if (!(scene.mMetaData && scene.mMetaData->Get("UpAxis", zAxis)
        && scene.mMetaData->Get("UpAxisSign", zAxisSign)
        && scene.mMetaData->Get("FrontAxis", xAxis)
        && scene.mMetaData->Get("FrontAxisSign", xAxisSign)
        && scene.mMetaData->Get("CoordAxis", yAxis)
        && scene.mMetaData->Get("CoordAxisSign", yAxisSign)
        && scene.mMetaData->Get("UnitScaleFactor", unitScale)))
  {
    // By default, all 3D data from is provided in a right-handed coordinate system.
    if (useQuakeCoordinateSystem(scene))
    {
      // Quake models loaded by assimp use the following coordinate system:
      // +X out of the screen (mapped to +X)
      // +Y upwards (mapped to +Z)
      // +Z to the left (mapped to -Y)
      xAxis = 0;
      xAxisSign = 1;
      yAxis = 2;
      yAxisSign = -1;
      zAxis = 1;
      zAxisSign = 1;
      unitScale = 1.0f;
    }
    else
    {
      // assimp defaults to the following coordinate system:
      // +X to the right (mapped to +Y)
      // +Y upwards (mapped to +Z)
      // +Z out of the screen (mapped to +X)
      xAxis = 2;
      xAxisSign = 1;
      yAxis = 0;
      yAxisSign = 1;
      zAxis = 1;
      zAxisSign = 1;
      unitScale = 1.0f;
    }
  }

  auto x = aiVector3D{};
  auto y = aiVector3D{};
  auto z = aiVector3D{};
  x[static_cast<unsigned int>(xAxis)] = float(xAxisSign) * unitScale;
  y[static_cast<unsigned int>(yAxis)] = float(yAxisSign) * unitScale;
  z[static_cast<unsigned int>(zAxis)] = float(zAxisSign) * unitScale;

  // clang-format off
  return aiMatrix4x4t{
    x.x,  x.y,  x.z,  0.0f, // X axis
    y.x,  y.y,  y.z,  0.0f, // Y axis
    z.x,  z.y,  z.z,  0.0f, // Z axis
    0.0f, 0.0f, 0.0f, 1.0f,
  };
  // clang-format on
}

Result<void> loadSceneFrame(
  const aiScene& scene,
  const size_t frameIndex,
  mdl::EntityModelData& model,
  const std::string& name)
{
  // load the animation information for the current "frame" (animation)
  const auto boneTransforms =
    frameIndex < scene.mNumAnimations
      ? getAnimationInformation(*scene.mRootNode, *scene.mAnimations[frameIndex])
      : std::vector<AssimpBoneInformation>{};

  // Assimp files import as y-up. We must multiply the root transform with an axis
  // transform matrix.
  auto meshes = std::vector<AssimpMeshWithTransforms>{};
  processRootNode(
    meshes,
    *scene.mRootNode,
    scene,
    scene.mRootNode->mTransformation,
    getAxisTransform(scene));

  auto bounds = vm::bbox3f::builder{};

  return meshes | std::views::transform([&](const auto& mesh) {
           return std::tuple{mesh, getMeshIndex(scene, *mesh.m_mesh)};
         })
         | std::views::filter([](const auto& meshAndIndex) {
             return std::get<1>(meshAndIndex) != std::nullopt;
           })
         | std::views::transform([&](const auto& meshAndIndex) {
             const auto& mesh = std::get<0>(meshAndIndex);
             const auto& meshIndex = *std::get<1>(meshAndIndex);

             return computeMeshVertices(
                      *mesh.m_mesh,
                      mesh.m_transform,
                      mesh.m_axisTransform,
                      boneTransforms)
                    | kdl::transform([&](const auto& vertices) {
                        for (const auto& v : vertices)
                        {
                          bounds.add(v.attr);
                        }

                        return computeMeshData(mesh, meshIndex, vertices);
                      });
           })
         | kdl::fold | kdl::and_then([&](const auto& meshData) -> Result<void> {
             if (!bounds.initialized())
             {
               // passing empty bounds as bbox crashes the program, don't let it happen
               return Error{"Model has no vertices. (So no valid bounding box.)"};
             }

             const auto frameBounds = bounds.bounds();
             auto& frame = model.addFrame(name, frameBounds);

             for (const auto& data : meshData)
             {
               auto& surface = model.surface(data.m_meshIndex);
               surface.addMesh(frame, data.m_vertices, data.m_indices);
             }

             return Result<void>{};
           });
}

} // namespace

AssimpLoader::AssimpLoader(std::filesystem::path path, const FileSystem& fs)
  : m_path{std::move(path)}
  , m_fs{fs}
{
}

bool AssimpLoader::canParse(const std::filesystem::path& path)
{
  // clang-format off
  static const auto supportedExtensions = std::vector<std::filesystem::path>{
    // Quake model formats have been omitted since Trenchbroom's got its own parsers
    // already.
    ".3mf",  ".dae",      ".xml",          ".blend",    ".bvh",       ".3ds",  ".ase",
    ".lwo",  ".lws",      ".md5mesh",      ".md5anim",  ".md5camera", // Lightwave and Doom 3 formats
    ".gltf", ".fbx",      ".glb",          ".ply",      ".dxf",       ".ifc",  ".iqm",
    ".nff",  ".smd",      ".vta", // .smd and .vta are uncompiled Source engine models.
    ".mdc",  ".x",        ".q30",          ".qrs",      ".ter",       ".raw",  ".ac",
    ".ac3d", ".stl",      ".dxf",          ".irrmesh",  ".irr",       ".off",
    ".obj", // .obj files will only be parsed by Assimp if the neverball importer isn't enabled
    ".mdl", // 3D GameStudio Model. It requires a palette file to load.
    ".hmp",  ".mesh.xml", ".skeleton.xml", ".material", ".ogex",      ".ms3d", ".lxo",
    ".csm",  ".ply",      ".cob",          ".scn",      ".xgl"};
  // clang-format on

  return kdl::vec_contains(supportedExtensions, kdl::path_to_lower(path.extension()));
}

Result<mdl::EntityModelData> AssimpLoader::load(tb::Logger& logger)
{
  try
  {
    constexpr auto assimpFlags = aiProcess_Triangulate | aiProcess_JoinIdenticalVertices
                                 | aiProcess_FlipWindingOrder | aiProcess_SortByPType
                                 | aiProcess_FlipUVs;

    const auto modelPath = m_path.string();

    // Import the file as an Assimp scene and populate our vectors.
    auto importer = Assimp::Importer{};
    importer.SetIOHandler(new AssimpIOSystem{m_fs});

    const auto* scene = importer.ReadFile(modelPath, assimpFlags);
    if (!scene)
    {
      return Error{fmt::format(
        "Assimp couldn't import model from '{}': {}", m_path, importer.GetErrorString())};
    }

    // Create model data.
    auto data = mdl::EntityModelData{mdl::PitchType::Normal, mdl::Orientation::Oriented};

    // create a frame for each animation in the scene
    // if we have no animations, always load 1 frame for the reference model
    const auto numSequences = std::max(scene->mNumAnimations, 1u);

    const auto goldSrcTextures = kdl::path_to_lower(m_path.extension()) == ".mdl"
                                   ? tryLoadGoldSrcTextureLibrary(m_fs, m_path, logger)
                                   : std::nullopt;

    // create a surface for each mesh in the scene and assign the skins/materials to it
    const auto numMeshes = scene->mNumMeshes;
    for (size_t i = 0; i < numMeshes; ++i)
    {
      const auto& mesh = scene->mMeshes[i];

      auto& surface = data.addSurface(scene->mMeshes[i]->mName.data, numSequences);

      // an assimp mesh will only ever have one material, but a material can have
      // multiple alternatives (this is how assimp handles skins)

      // load skins for this surface
      auto materials = loadMaterialsForMaterial(
        *scene, mesh->mMaterialIndex, m_path, m_fs, logger, goldSrcTextures);
      surface.setSkins(std::move(materials));
    }

    return std::views::iota(0u, numSequences) | std::views::transform([&](const auto i) {
             return loadSceneFrame(*scene, i, data, modelPath);
           })
           | kdl::fold | kdl::transform([&]() { return std::move(data); });
  }
  catch (const ParserException& e)
  {
    return Error{e.what()};
  }
}

} // namespace tb::io
