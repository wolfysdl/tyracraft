#pragma once

#include <vector>
#include <tamtypes.h>
#include <renderer/renderer.hpp>
#include <renderer/3d/mesh/mesh.hpp>
#include <renderer/core/texture/models/texture.hpp>
#include "renderer/3d/pipeline/minecraft/minecraft_pipeline.hpp"
#include "contants.hpp"
#include "renderer/3d/pipeline/minecraft/mcpip_block.hpp"

using Tyra::McpipBlock;
using Tyra::Mesh;
using Tyra::MinecraftPipeline;
using Tyra::Renderer;
using Tyra::Texture;

struct BlockInfo {
  BlockInfo(u8 type, u8 isSingle, const float& texOffssetX,
            const float& texOffssetY) {
    _texOffssetX = texOffssetX;
    _texOffssetY = texOffssetY;
    blockId = type;
    _isSingle = isSingle;
  };

  ~BlockInfo(){};

  float _texOffssetX;
  float _texOffssetY;
  u8 _isSingle;
  u8 blockId;
};

class BlockManager {
 public:
  BlockManager();
  ~BlockManager();
  void init(Renderer* t_renderer, MinecraftPipeline* mcPip);
  BlockInfo* getBlockTexOffsetByType(const u8& blockType);
  inline Texture* getBlocksTexture() { return this->blocksTexAtlas; };
  float getBlockBreakingTime();
  McpipBlock* getDamageOverlay(const float& damage_percentage);

 private:
  void registerBlocksTextureCoordinates(MinecraftPipeline* mcPip);
  void registerDamageOverlayBlocks(MinecraftPipeline* mcPip);
  void loadBlocksTextures(Renderer* t_renderer);

  Texture* blocksTexAtlas;
  Renderer* t_renderer;
  std::vector<BlockInfo*> blockItems;
  std::vector<McpipBlock*> damage_overlay;
};
