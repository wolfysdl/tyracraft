#pragma once

#include <tamtypes.h>
#include <engine.hpp>
#include <renderer/3d/mesh/mesh.hpp>
#include <renderer/core/3d/bbox/core_bbox.hpp>
#include <renderer/3d/bbox/bbox.hpp>
#include <math/vec4.hpp>
#include <physics/ray.hpp>
#include <fastmath.h>
#include <draw_sampling.h>
#include "constants.hpp"
#include "entities/Block.hpp"
#include "entities/chunck.hpp"
#include "entities/player/player.hpp"
#include "entities/item.hpp"
#include "entities/level.hpp"
#include "3libs/FastNoiseLite/FastNoiseLite.h"
#include "renderer/3d/pipeline/minecraft/minecraft_pipeline.hpp"
#include "managers/block_manager.hpp"
#include "managers/items_repository.hpp"
#include "managers/sound_manager.hpp"
#include "models/sfx_block_model.hpp"
#include "debug/debug.hpp"
#include <chrono>
#include "models/new_game_model.hpp"
#include <stdint.h>
#include <limits>

// From CrossCraft
#include "managers/world_generator.hpp"

using Tyra::BBox;
using Tyra::Engine;
using Tyra::Math;
using Tyra::MinecraftPipeline;
using Tyra::Plane;
using Tyra::Renderer;
using Tyra::Vec4;

enum class TreeType { Oak, Birch };

class TerrainManager {
 public:
  TerrainManager();
  ~TerrainManager();
  void init(Renderer* t_renderer, ItemRepository* itemRepository,
            MinecraftPipeline* mcPip, BlockManager* blockManager,
            SoundManager* t_soundManager);
  void update(const Vec4& camLookPos, const Vec4& camPosition,
              std::vector<Chunck*> chuncks);
  void generateNewTerrain(const NewGameOptions& options);
  inline const uint32_t getSeed() { return seed; };

  Block* targetBlock = nullptr;
  void updateTargetBlock(const Vec4& camLookPos, const Vec4& camPosition,
                         std::vector<Chunck*> chuncks);
  void removeBlock(Block* blockToRemove);
  void putBlock(const Blocks& blockType, Player* t_player);
  inline const u8 validTargetBlock() {
    return this->targetBlock != nullptr && this->targetBlock->isBreakable;
  };

  Renderer* t_renderer;
  Engine* engine;
  BlockManager* t_blockManager;
  SoundManager* t_soundManager;

  const Vec4 defineSpawnArea();
  const Vec4 calcSpawOffset(int bias = 0);
  void buildChunk(Chunck* t_chunck);
  void buildChunkAsync(Chunck* t_chunck);

  inline u8 shouldUpdateChunck() { return this->_shouldUpdateChunck; };
  inline void setChunckToUpdated() { this->_shouldUpdateChunck = false; };
  inline u8 isBreakingBLock() { return this->_isBreakingBlock; };
  void breakTargetBlock(const float& deltaTime);
  void stopBreakTargetBlock();

  /**
   * @brief Returns the modified position on putting or removing a block when
   * shouldUpdateChunck() is true. It is useful to get the exact chunk by
   * position.
   */
  inline const Vec4 getModifiedPosition() { return this->_modifiedPosition; };

  inline u8 hasRemovedABlock() { return this->removedBlock != nullptr; };
  Block* removedBlock = nullptr;

  LevelMap* terrain;

 private:
  Ray ray;
  u8 _shouldUpdateChunck = false;
  u8 framesCounter = false;
  Vec4 _modifiedPosition;

  const u8 UPDATE_TARGET_LIMIT = 3;

  ItemRepository* t_itemRepository;
  MinecraftPipeline* t_mcPip;

  // TODO: Refactor to region and cache it. See
  // https://minecraft.fandom.com/el/wiki/Region_file_format;
  Vec4 minWorldPos;
  Vec4 maxWorldPos;
  BBox* rawBlockBbox;

  // Params for noise generation;
  const float frequency = 0.01f;
  const float amplitude = 0.65f;
  const float lacunarity = 2.4f;
  const float persistance = .45f;
  int octaves = sqrt(OVERWORLD_H_DISTANCE * OVERWORLD_V_DISTANCE);
  const uint32_t seed = rand();

  int getNoise(const int& x, const int& z);
  void generateTerrainBase(const bool& makeFlat);
  void generateCaves();
  void generateTrees();
  void generateWater();

  void placeTreeAt(const int& x, const int& z, const u8& treeHeight,
                   const Blocks& logType, const Blocks& leaveType);
  void placeOakTreeAt(const int& x, const int& z, const u8& treeHeight);
  void placeBirchTreeAt(const int& x, const int& z, const u8& treeHeight);
  TreeType getTreeType(const int& x, const int& z);

  u8 getBlock(int noise, int y);
  float getContinentalness(int x, int z);
  float getErosion(int x, int z);
  float getPeaksAndValleys(int x, int z);
  float getDensity(int x, int y, int z);
  float getTemperature(int x, int z);
  float getHumidity(int x, int z);
  float getHeightScale(int x, int z);

  u8 isBlockAtChunkBorder(const Vec4* blockOffset, const Vec4* chunkMinOffset,
                          const Vec4* chunkMaxOffset);
  u8 getBlockTypeByOffset(const int& x, const int& y, const int& z);
  unsigned int getIndexByOffset(int x, int y, int z);
  unsigned int getIndexByPosition(Vec4* pos);
  bool isBlockHidden(const Vec4* blockOffset);

  /**
   * @brief Update the visible block faces
   * @return false if the block is completely hidden
   *
   */
  int getBlockVisibleFaces(const Vec4* t_blockOffset);

  inline bool isBlockTransparentAtPosition(const float& x, const float& y,
                                           const float& z);

  inline bool isTopFaceVisible(const Vec4* t_blockOffset);
  inline bool isBottomFaceVisible(const Vec4* t_blockOffset);
  inline bool isLeftFaceVisible(const Vec4* t_blockOffset);
  inline bool isRightFaceVisible(const Vec4* t_blockOffset);
  inline bool isFrontFaceVisible(const Vec4* t_blockOffset);
  inline bool isBackFaceVisible(const Vec4* t_blockOffset);

  inline bool isBlockVisible(const Vec4* blockOffset) {
    return !isBlockHidden(blockOffset);
  };

  Vec4* normalizeWorldBlockPosition(Vec4* worldPosition);
  void calcRawBlockBBox(MinecraftPipeline* mcPip);
  void getBlockMinMax(Block* t_block);
  u8 shouldUpdateTargetBlock();
  float getBlockLuminosity(const float& yPosition);

  // Breaking control
  u8 _isBreakingBlock = false;
  float breaking_time_pessed = 0.0F;
  float lastTimePlayedBreakingSfx = 0.0F;

  void playPutBlockSound(const Blocks& blockType);
  void playDestroyBlockSound(const Blocks& blockType);
  void playBreakingBlockSound(const Blocks& blockType);
};
