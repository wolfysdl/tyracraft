
#include "entities/World.hpp"
#include "renderer/models/color.hpp"
#include "math/m4x4.hpp"
#include <tyra>

// From CrossCraft
#include <stdio.h>
#include "entities/World.hpp"
#include <queue>

using Tyra::Color;
using Tyra::M4x4;

World::World(const NewGameOptions& options) {
  printf("\n\n|-----------SEED---------|");
  printf("\n|           %ld         |\n", seed);
  printf("|------------------------|\n\n");

  this->worldOptions = options;
  this->blockManager = new BlockManager();
  this->chunckManager = new ChunckManager();
  this->minWorldPos.set(OVERWORLD_MIN_DISTANCE, OVERWORLD_MIN_HEIGH,
                        OVERWORLD_MIN_DISTANCE);
  this->maxWorldPos.set(OVERWORLD_MAX_DISTANCE, OVERWORLD_MAX_HEIGH,
                        OVERWORLD_MAX_DISTANCE);

  CrossCraft_World_Init();
}

World::~World() {
  delete lastPlayerPosition;
  delete this->blockManager;
  delete this->chunckManager;

  CrossCraft_World_Deinit();
}

void World::init(Renderer* t_renderer, ItemRepository* itemRepository,
                 SoundManager* t_soundManager) {
  TYRA_ASSERT(t_renderer, "t_renderer not initialized");
  TYRA_ASSERT(itemRepository, "itemRepository not initialized");

  this->t_renderer = t_renderer;

  this->mcPip.setRenderer(&t_renderer->core);
  this->stapip.setRenderer(&t_renderer->core);

  this->blockManager->init(t_renderer, &this->mcPip);
  this->chunckManager->init();

  this->calcRawBlockBBox(&mcPip);

  this->terrain = CrossCraft_World_GetMapPtr();

  CrossCraft_World_Create_Map(WORLD_SIZE_SMALL);

  if (worldOptions.makeFlat) {
    CrossCraft_World_GenerateMap(WorldType::WORLD_TYPE_FLAT);
  } else {
    CrossCraft_World_GenerateMap(WorldType::WORLD_TYPE_ORIGINAL);
  }

  // Define global and local spawn area
  this->worldSpawnArea.set(this->defineSpawnArea());
  this->spawnArea.set(this->worldSpawnArea);
  this->buildInitialPosition();
  this->setIntialTime();
};

void World::update(Player* t_player, const Vec4& camLookPos,
                   const Vec4& camPosition) {
  this->framesCounter++;

  dayNightCycleManager.update();
  updateLightModel();

  if (this->shouldUpdateChunck()) {
    if (this->hasRemovedABlock())
      reloadChangedChunkByRemovedBlock();
    else
      reloadChangedChunkByPutedBlock();
  }

  this->chunckManager->update(
      this->t_renderer->core.renderer3D.frustumPlanes.getAll(),
      *t_player->getPosition(), &worldLightModel);
  this->updateChunkByPlayerPosition(t_player);

  this->updateTargetBlock(camLookPos, camPosition,
                          this->chunckManager->getVisibleChunks());

  this->framesCounter = this->framesCounter % 60;
};

void World::render() {
  this->t_renderer->core.setClearScreenColor(
      dayNightCycleManager.getSkyColor());

  this->chunckManager->renderer(this->t_renderer, &this->stapip,
                                this->blockManager);

  if (this->targetBlock) {
    this->renderTargetBlockHitbox(this->targetBlock);
    if (this->isBreakingBLock()) this->renderBlockDamageOverlay();
  }
};

void World::buildInitialPosition() {
  Chunck* initialChunck =
      this->chunckManager->getChunckByPosition(this->worldSpawnArea);
  if (initialChunck != nullptr) {
    initialChunck->clear();
    this->buildChunk(initialChunck);
    this->scheduleChunksNeighbors(initialChunck, true);
  }
};

const std::vector<Block*> World::getLoadedBlocks() {
  this->loadedBlocks.clear();
  this->loadedBlocks.shrink_to_fit();

  auto visibleChuncks = this->chunckManager->getVisibleChunks();
  for (u16 i = 0; i < visibleChuncks.size(); i++) {
    for (u16 j = 0; j < visibleChuncks[i]->blocks.size(); j++)
      this->loadedBlocks.push_back(visibleChuncks[i]->blocks[j]);
  }

  return this->loadedBlocks;
}

void World::updateChunkByPlayerPosition(Player* t_player) {
  Vec4 currentPlayerPos = *t_player->getPosition();
  if (lastPlayerPosition->distanceTo(currentPlayerPos) > CHUNCK_SIZE) {
    lastPlayerPosition->set(currentPlayerPos);
    Chunck* currentChunck =
        chunckManager->getChunckByPosition(currentPlayerPos);

    if (currentChunck && t_player->currentChunckId != currentChunck->id) {
      t_player->currentChunckId = currentChunck->id;
      scheduleChunksNeighbors(currentChunck, currentPlayerPos);
    }
  }

  if (framesCounter % 3 == 0) {
    unloadScheduledChunks();
    loadScheduledChunks();
  }
}

void World::reloadChangedChunkByPutedBlock() {
  Chunck* chunckToUpdate =
      this->chunckManager->getChunckByPosition(this->getModifiedPosition());

  if (this->targetBlock && this->targetBlock->isAtChunkBorder) {
    updateNeighBorsChunksByModdedBlock(this->targetBlock);
  }

  if (chunckToUpdate != nullptr) {
    chunckToUpdate->clear();
    this->buildChunk(chunckToUpdate);
    this->setChunckToUpdated();
  }
}

void World::reloadChangedChunkByRemovedBlock() {
  Block* removedBlock = this->removedBlock;
  if (removedBlock->isAtChunkBorder)
    updateNeighBorsChunksByModdedBlock(removedBlock);

  Chunck* chunckToUpdate =
      this->chunckManager->getChunckById(removedBlock->chunkId);
  chunckToUpdate->clear();
  this->buildChunk(chunckToUpdate);
  this->removedBlock = nullptr;
  this->setChunckToUpdated();
}

void World::updateNeighBorsChunksByModdedBlock(Block* changedBlock) {
  // Front
  Chunck* frontChunk = this->chunckManager->getChunckByOffset(
      Vec4(changedBlock->offset.x, changedBlock->offset.y,
           changedBlock->offset.z + 1));

  // Back
  Chunck* backChunk = this->chunckManager->getChunckByOffset(
      Vec4(changedBlock->offset.x, changedBlock->offset.y,
           changedBlock->offset.z - 1));

  // Right
  Chunck* rightChunk = this->chunckManager->getChunckByOffset(
      Vec4(changedBlock->offset.x + 1, changedBlock->offset.y,
           changedBlock->offset.z));
  // Left
  Chunck* leftChunk = this->chunckManager->getChunckByOffset(
      Vec4(changedBlock->offset.x - 1, changedBlock->offset.y,
           changedBlock->offset.z));

  if (frontChunk && frontChunk->id != changedBlock->chunkId) {
    frontChunk->clear();
    this->buildChunk(frontChunk);
  } else if (backChunk && backChunk->id != changedBlock->chunkId) {
    backChunk->clear();
    this->buildChunk(backChunk);
  }

  if (rightChunk && rightChunk->id != changedBlock->chunkId) {
    rightChunk->clear();
    this->buildChunk(rightChunk);
  } else if (leftChunk && leftChunk->id != changedBlock->chunkId) {
    leftChunk->clear();
    this->buildChunk(leftChunk);
  }
}

void World::scheduleChunksNeighbors(Chunck* t_chunck,
                                    const Vec4 currentPlayerPos,
                                    u8 force_loading) {
  auto chuncks = chunckManager->getChuncks();
  for (u16 i = 0; i < chuncks.size(); i++) {
    float distance =
        floor(t_chunck->center->distanceTo(*chuncks[i]->center) / CHUNCK_SIZE) +
        1;

    if (distance > DRAW_DISTANCE_IN_CHUNKS) {
      if (chuncks[i]->state != ChunkState::Clean)
        addChunkToUnloadAsync(chuncks[i]);
    } else {
      if (force_loading) {
        chuncks[i]->clear();
        buildChunkAsync(chuncks[i]);
      } else if (chuncks[i]->state != ChunkState::Loaded) {
        addChunkToLoadAsync(chuncks[i]);
      }
    }
  }

  if (tempChuncksToLoad.size()) sortChunksToLoad(currentPlayerPos);
}

void World::sortChunksToLoad(const Vec4& currentPlayerPos) {
  std::sort(tempChuncksToLoad.begin(), tempChuncksToLoad.end(),
            [currentPlayerPos](const Chunck* a, const Chunck* b) {
              const float distanceA =
                  (*a->center * DUBLE_BLOCK_SIZE).distanceTo(currentPlayerPos);
              const float distanceB =
                  (*b->center * DUBLE_BLOCK_SIZE).distanceTo(currentPlayerPos);
              return distanceA < distanceB;
            });
}

void World::loadScheduledChunks() {
  if (tempChuncksToLoad.size() > 0) {
    if (tempChuncksToLoad[0]->state != ChunkState::Loaded)
      return this->buildChunkAsync(tempChuncksToLoad[0]);
    tempChuncksToLoad.erase(tempChuncksToLoad.begin());
  };
}

void World::unloadScheduledChunks() {
  if (tempChuncksToUnLoad.size() > 0) {
    tempChuncksToUnLoad[0]->clear();
    tempChuncksToUnLoad.erase(tempChuncksToUnLoad.begin());
  }
}

void World::renderBlockDamageOverlay() {
  McpipBlock* overlay =
      this->blockManager->getDamageOverlay(this->targetBlock->damage);
  if (overlay != nullptr) {
    if (this->overlayData.size() > 0) {
      // Clear last overlay;
      for (u8 i = 0; i < overlayData.size(); i++) {
        delete overlayData[i]->color;
        delete overlayData[i]->model;
      }
      this->overlayData.clear();
      this->overlayData.shrink_to_fit();
    }

    M4x4 scale = M4x4();
    M4x4 translation = M4x4();

    scale.identity();
    translation.identity();
    scale.scale(BLOCK_SIZE + 0.015f);
    translation.translate(*this->targetBlock->getPosition());

    overlay->model = new M4x4(translation * scale);
    overlay->color = new Color(128.0f, 128.0f, 128.0f, 70.0f);

    this->overlayData.push_back(overlay);
    this->t_renderer->renderer3D.usePipeline(&this->mcPip);
    mcPip.render(overlayData, this->blockManager->getBlocksTexture(), false);
  }
}

void World::renderTargetBlockHitbox(Block* targetBlock) {
  this->t_renderer->renderer3D.utility.drawBox(*targetBlock->getPosition(),
                                               BLOCK_SIZE, Color(0, 0, 0));
}

void World::addChunkToLoadAsync(Chunck* t_chunck) {
  // Avoid being suplicated;
  for (size_t i = 0; i < tempChuncksToLoad.size(); i++)
    if (tempChuncksToLoad[i]->id == t_chunck->id) return;

  // Avoid unload and load the same chunk at the same time
  for (size_t i = 0; i < tempChuncksToUnLoad.size(); i++)
    if (tempChuncksToUnLoad[i]->id == t_chunck->id)
      tempChuncksToUnLoad.erase(tempChuncksToUnLoad.begin() + i);

  tempChuncksToLoad.push_back(t_chunck);
}

void World::addChunkToUnloadAsync(Chunck* t_chunck) {
  // Avoid being suplicated;
  for (size_t i = 0; i < tempChuncksToUnLoad.size(); i++)
    if (tempChuncksToUnLoad[i]->id == t_chunck->id) return;

  // Avoid unload and load the same chunk at the same time
  for (size_t i = 0; i < tempChuncksToLoad.size(); i++)
    if (tempChuncksToLoad[i]->id == t_chunck->id)
      tempChuncksToLoad.erase(tempChuncksToLoad.begin() + i);

  tempChuncksToUnLoad.push_back(t_chunck);
}

void World::updateLightModel() {
  worldLightModel.lightsPositions = lightsPositions.data();
  worldLightModel.lightIntensity = dayNightCycleManager.getLightIntensity();
  worldLightModel.sunPosition.set(dayNightCycleManager.getSunPosition());
  worldLightModel.moonPosition.set(dayNightCycleManager.getMoonPosition());
  worldLightModel.ambientLightIntensity =
      dayNightCycleManager.getAmbientLightIntesity();
}

unsigned int World::getIndexByOffset(int x, int y, int z) {
  return (y * terrain->length * terrain->width) + (z * terrain->width) + x;
}

bool World::isBlockTransparentAtPosition(const float& x, const float& y,
                                         const float& z) {
  if (BoundCheckMap(terrain, x, y, z)) {
    const u8 blockType = GetBlockFromMap(terrain, x, y, z);
    return blockType <= (u8)Blocks::AIR_BLOCK ||
           this->blockManager->isBlockTransparent(
               static_cast<Blocks>(blockType));
  } else {
    return false;
  }
}

bool World::isTopFaceVisible(const Vec4* t_blockOffset) {
  return isBlockTransparentAtPosition(t_blockOffset->x, t_blockOffset->y + 1,
                                      t_blockOffset->z);
}

bool World::isBottomFaceVisible(const Vec4* t_blockOffset) {
  return isBlockTransparentAtPosition(t_blockOffset->x, t_blockOffset->y - 1,
                                      t_blockOffset->z);
}

bool World::isFrontFaceVisible(const Vec4* t_blockOffset) {
  return isBlockTransparentAtPosition(t_blockOffset->x - 1, t_blockOffset->y,
                                      t_blockOffset->z);
}

bool World::isBackFaceVisible(const Vec4* t_blockOffset) {
  return isBlockTransparentAtPosition(t_blockOffset->x + 1, t_blockOffset->y,
                                      t_blockOffset->z);
}

bool World::isLeftFaceVisible(const Vec4* t_blockOffset) {
  return isBlockTransparentAtPosition(t_blockOffset->x, t_blockOffset->y,
                                      t_blockOffset->z - 1);
}

bool World::isRightFaceVisible(const Vec4* t_blockOffset) {
  return isBlockTransparentAtPosition(t_blockOffset->x, t_blockOffset->y,
                                      t_blockOffset->z + 1);
}

int World::getBlockVisibleFaces(const Vec4* t_blockOffset) {
  int result = 0x000000;

  // Front
  if (isFrontFaceVisible(t_blockOffset)) result = result | FRONT_VISIBLE;

  // Back
  if (isBackFaceVisible(t_blockOffset)) result = result | BACK_VISIBLE;

  // Right
  if (isRightFaceVisible(t_blockOffset)) result = result | RIGHT_VISIBLE;

  // Left
  if (isLeftFaceVisible(t_blockOffset)) result = result | LEFT_VISIBLE;

  // Top
  if (isTopFaceVisible(t_blockOffset)) result = result | TOP_VISIBLE;

  // Bottom
  if (isBottomFaceVisible(t_blockOffset)) result = result | BOTTOM_VISIBLE;

  // printf("Result for index %i -> 0x%X\n", blockIndex, result);
  return result;
}

void World::calcRawBlockBBox(MinecraftPipeline* mcPip) {
  const auto& blockData = mcPip->getBlockData();
  this->rawBlockBbox = new BBox(blockData.vertices, blockData.count);
}

u8 World::shouldUpdateTargetBlock() {
  return this->framesCounter == this->UPDATE_TARGET_LIMIT;
}

const Vec4 World::defineSpawnArea() {
  Vec4 spawPos = this->calcSpawOffset();
  return spawPos;
}

const Vec4 World::calcSpawOffset(int bias) {
  bool found = false;
  u8 airBlockCounter = 0;
  // Pick a X and Z coordinates based on the seed;
  int posX = ((seed + bias) % HALF_OVERWORLD_H_DISTANCE);
  int posZ = ((seed - bias) % HALF_OVERWORLD_H_DISTANCE);
  Vec4 result;

  for (int posY = OVERWORLD_MAX_HEIGH; posY >= OVERWORLD_MIN_HEIGH; posY--) {
    u8 type = GetBlockFromMap(terrain, posX, posY, posZ);
    if (type != (u8)Blocks::AIR_BLOCK && airBlockCounter >= 4) {
      found = true;
      result = Vec4(posX, posY + 2, posZ);
      break;
    }

    if (type == (u8)Blocks::AIR_BLOCK)
      airBlockCounter++;
    else
      airBlockCounter = 0;
  }

  if (found)
    return result * DUBLE_BLOCK_SIZE;
  else
    return calcSpawOffset(bias + 1);
}

void World::removeBlock(Block* blockToRemove) {
  SetBlockInMap(terrain, blockToRemove->offset.x, blockToRemove->offset.y,
                blockToRemove->offset.z, (u8)Blocks::AIR_BLOCK);
  this->_modifiedPosition.set(*blockToRemove->getPosition());
  this->removedBlock = blockToRemove;
  this->_shouldUpdateChunck = true;
  this->playDestroyBlockSound(blockToRemove->type);
}

void World::putBlock(const Blocks& blockToPlace, Player* t_player) {
  if (this->targetBlock == nullptr) return;

  int terrainIndex = this->targetBlock->index;
  Vec4 targetPos = ray.at(this->targetBlock->distance);
  Vec4 newBlockPos = *this->targetBlock->getPosition();

  // Front
  if (std::round(targetPos.z) ==
      this->targetBlock->bbox->getFrontFace().axisPosition) {
    terrainIndex += OVERWORLD_H_DISTANCE * OVERWORLD_V_DISTANCE;
    newBlockPos.z += DUBLE_BLOCK_SIZE;
    // Back
  } else if (std::round(targetPos.z) ==
             this->targetBlock->bbox->getBackFace().axisPosition) {
    terrainIndex -= OVERWORLD_H_DISTANCE * OVERWORLD_V_DISTANCE;
    newBlockPos.z -= DUBLE_BLOCK_SIZE;
    // Right
  } else if (std::round(targetPos.x) ==
             this->targetBlock->bbox->getRightFace().axisPosition) {
    terrainIndex += OVERWORLD_V_DISTANCE;
    newBlockPos.x += DUBLE_BLOCK_SIZE;
    // Left
  } else if (std::round(targetPos.x) ==
             this->targetBlock->bbox->getLeftFace().axisPosition) {
    terrainIndex -= OVERWORLD_V_DISTANCE;
    newBlockPos.x -= DUBLE_BLOCK_SIZE;
    // Up
  } else if (std::round(targetPos.y) ==
             this->targetBlock->bbox->getTopFace().axisPosition) {
    terrainIndex++;
    newBlockPos.y += DUBLE_BLOCK_SIZE;
    // Down
  } else if (std::round(targetPos.y) ==
             this->targetBlock->bbox->getBottomFace().axisPosition) {
    terrainIndex--;
    newBlockPos.y -= DUBLE_BLOCK_SIZE;
  }

  // Is a valid index?
  if (terrainIndex <= OVERWORLD_SIZE &&
      terrainIndex != this->targetBlock->index) {
    {
      // Prevent to put a block at the player position;
      M4x4 tempModel = M4x4();
      tempModel.identity();
      tempModel.scale(BLOCK_SIZE);
      tempModel.translate(newBlockPos);

      BBox tempBBox = this->rawBlockBbox->getTransformed(tempModel);
      Vec4 newBlockPosMin;
      Vec4 newBlockPosMax;
      tempBBox.getMinMax(&newBlockPosMin, &newBlockPosMax);

      Vec4 minPlayerCorner;
      Vec4 maxPlayerCorner;
      t_player->getHitBox().getMinMax(&minPlayerCorner, &maxPlayerCorner);

      // Will Collide to player?
      if (newBlockPosMax.x > minPlayerCorner.x &&
          newBlockPosMin.x < maxPlayerCorner.x &&
          newBlockPosMax.z > minPlayerCorner.z &&
          newBlockPosMin.z < maxPlayerCorner.z &&
          newBlockPosMax.y > minPlayerCorner.y &&
          newBlockPosMin.y < maxPlayerCorner.y)
        return;  // Return on collision
    }

    const Vec4 blockOffset = newBlockPos / BLOCK_SIZE;

    blockOffset.print();

    this->_modifiedPosition.set(newBlockPos);

    if (terrain->blocks[terrainIndex] == (u8)Blocks::AIR_BLOCK) {
      terrain->blocks[terrainIndex] = (u8)blockToPlace;
    }

    this->_shouldUpdateChunck = 1;
    this->playPutBlockSound(blockToPlace);
  }
}

void World::stopBreakTargetBlock() {
  this->_isBreakingBlock = false;
  if (this->targetBlock) this->targetBlock->damage = 0;
}

void World::breakTargetBlock(const float& deltaTime) {
  if (this->targetBlock == nullptr) return;

  if (this->_isBreakingBlock) {
    this->breaking_time_pessed += deltaTime;

    if (breaking_time_pessed >= this->blockManager->getBlockBreakingTime()) {
      // Remove block;
      this->removeBlock(this->targetBlock);

      // Target block has changed, reseting the pressed time;
      this->breaking_time_pessed = 0;
    } else {
      // Update damage overlay
      this->targetBlock->damage = breaking_time_pessed /
                                  this->blockManager->getBlockBreakingTime() *
                                  100;
      if (lastTimePlayedBreakingSfx > 0.3F) {
        this->playBreakingBlockSound(this->targetBlock->type);
        lastTimePlayedBreakingSfx = 0;
      } else {
        lastTimePlayedBreakingSfx += deltaTime;
      }
    }
  } else {
    this->breaking_time_pessed = 0;
    this->_isBreakingBlock = true;
  }
}

void World::playPutBlockSound(const Blocks& blockType) {
  if (blockType != Blocks::AIR_BLOCK) {
    SfxBlockModel* blockSfxModel =
        this->blockManager->getDigSoundByBlockType(blockType);
    if (blockSfxModel != nullptr) {
      const int ch = this->t_soundManager->getAvailableChannel();
      this->t_soundManager->playSfx(blockSfxModel->category,
                                    blockSfxModel->sound, ch);
    }
    Tyra::Threading::switchThread();
  }
}

void World::playDestroyBlockSound(const Blocks& blockType) {
  if (blockType != Blocks::AIR_BLOCK) {
    SfxBlockModel* blockSfxModel =
        this->blockManager->getDigSoundByBlockType(blockType);

    if (blockSfxModel != nullptr) {
      const int ch = this->t_soundManager->getAvailableChannel();
      this->t_soundManager->playSfx(blockSfxModel->category,
                                    blockSfxModel->sound, ch);
    }
    Tyra::Threading::switchThread();
  }
}

void World::playBreakingBlockSound(const Blocks& blockType) {
  if (blockType != Blocks::AIR_BLOCK) {
    SfxBlockModel* blockSfxModel =
        this->blockManager->getDigSoundByBlockType(blockType);

    if (blockSfxModel != nullptr) {
      const int ch = this->t_soundManager->getAvailableChannel();
      this->t_soundManager->playSfx(blockSfxModel->category,
                                    blockSfxModel->sound, ch);
    }
    Tyra::Threading::switchThread();
  }
}

u8 World::isBlockAtChunkBorder(const Vec4* blockOffset,
                               const Vec4* chunkMinOffset,
                               const Vec4* chunkMaxOffset) {
  return blockOffset->x == chunkMinOffset->x ||
         blockOffset->x == chunkMaxOffset->x ||
         blockOffset->z == chunkMinOffset->z ||
         blockOffset->z == chunkMaxOffset->z;
}

void World::buildChunk(Chunck* t_chunck) {
  for (int z = t_chunck->minOffset->z; z < t_chunck->maxOffset->z; z++) {
    for (int x = t_chunck->minOffset->x; x < t_chunck->maxOffset->x; x++) {
      for (int y = t_chunck->minOffset->y; y < t_chunck->maxOffset->y; y++) {
        unsigned int blockIndex = this->getIndexByOffset(x, y, z);
        u8 block_type = GetBlockFromMap(terrain, x, y, z);

        if (block_type <= (u8)Blocks::AIR_BLOCK ||
            !BoundCheckMap(terrain, x, y, z))
          continue;

        Vec4 tempBlockOffset = Vec4(x, y, z);
        Vec4 blockPosition = (tempBlockOffset * DUBLE_BLOCK_SIZE);

        const int visibleFaces = this->getBlockVisibleFaces(&tempBlockOffset);
        const bool isVisible = visibleFaces > 0;

        // Are block's coordinates in world range?
        if (isVisible) {
          BlockInfo* blockInfo = this->blockManager->getBlockInfoByType(
              static_cast<Blocks>(block_type));
          if (blockInfo) {
            Block* block = new Block(blockInfo);
            block->index = blockIndex;
            block->offset.set(tempBlockOffset);
            block->chunkId = t_chunck->id;
            block->visibleFaces = visibleFaces;
            block->isAtChunkBorder = isBlockAtChunkBorder(
                &tempBlockOffset, t_chunck->minOffset, t_chunck->maxOffset);

            // float bright = this->getBlockLuminosity(tempBlockOffset.y);
            // block->color = Color(bright, bright, bright, 128.0F);

            block->setPosition(blockPosition);
            block->scale.scale(BLOCK_SIZE);
            block->updateModelMatrix();

            // Calc min and max corners
            {
              BBox tempBBox = this->rawBlockBbox->getTransformed(block->model);
              block->bbox = new BBox(tempBBox);
              block->bbox->getMinMax(&block->minCorner, &block->maxCorner);
            }

            t_chunck->addBlock(block);
          }
        }
      }
    }
  }

  t_chunck->state = ChunkState::Loaded;
  t_chunck->loadDrawData();
}

void World::buildChunkAsync(Chunck* t_chunck) {
  int batchCounter = 0;
  int z = t_chunck->tempLoadingOffset->z;
  int x = t_chunck->tempLoadingOffset->x;
  int y = t_chunck->tempLoadingOffset->y;

  while (batchCounter < LOAD_CHUNK_BATCH) {
    if (z >= t_chunck->maxOffset->z) break;

    unsigned int blockIndex = this->getIndexByOffset(x, y, z);
    u8 block_type = GetBlockFromMap(terrain, x, y, z);
    if (block_type > (u8)Blocks::AIR_BLOCK &&
        block_type < (u8)Blocks::TOTAL_OF_BLOCKS) {
      Vec4 tempBlockOffset = Vec4(x, y, z);
      Vec4 blockPosition = (tempBlockOffset * DUBLE_BLOCK_SIZE);

      const int visibleFaces = this->getBlockVisibleFaces(&tempBlockOffset);
      const bool isVisible = visibleFaces > 0;

      // Are block's coordinates in world range?
      if (isVisible && BoundCheckMap(terrain, x, y, z)) {
        BlockInfo* blockInfo = this->blockManager->getBlockInfoByType(
            static_cast<Blocks>(block_type));

        if (blockInfo) {
          Block* block = new Block(blockInfo);
          block->index = blockIndex;
          block->offset.set(tempBlockOffset);
          block->chunkId = t_chunck->id;
          block->visibleFaces = visibleFaces;
          block->isAtChunkBorder = isBlockAtChunkBorder(
              &tempBlockOffset, t_chunck->minOffset, t_chunck->maxOffset);

          // float bright = this->getBlockLuminosity(tempBlockOffset.y);
          // block->color = Color(bright, bright, bright, 128.0F);

          block->setPosition(blockPosition);
          block->scale.scale(BLOCK_SIZE);
          block->updateModelMatrix();

          // Calc min and max corners
          {
            BBox tempBBox = this->rawBlockBbox->getTransformed(block->model);
            block->bbox = new BBox(tempBBox);
            block->bbox->getMinMax(&block->minCorner, &block->maxCorner);
          }

          t_chunck->addBlock(block);
        }
        batchCounter++;
      }
    }

    y++;
    if (y > t_chunck->maxOffset->y) {
      y = t_chunck->minOffset->y;
      x++;
    }
    if (x > t_chunck->maxOffset->x) {
      x = t_chunck->minOffset->x;
      z++;
    }
  }

  if (batchCounter >= LOAD_CHUNK_BATCH) {
    t_chunck->tempLoadingOffset->set(x, y, z);
    return;
  }

  t_chunck->state = ChunkState::Loaded;
  t_chunck->loadDrawData();
}

void World::updateTargetBlock(const Vec4& camLookPos, const Vec4& camPosition,
                              std::vector<Chunck*> chuncks) {
  u8 hitedABlock = 0;
  float tempTargetDistance = -1.0f;
  float tempPlayerDistance = -1.0f;
  Block* tempTargetBlock = nullptr;

  // Reset the current target block;
  this->targetBlock = nullptr;

  // Prepate the raycast
  Vec4 rayDir = camLookPos - camPosition;
  rayDir.normalize();
  ray.origin.set(camPosition);
  ray.direction.set(rayDir);

  for (u16 h = 0; h < chuncks.size(); h++) {
    for (u16 i = 0; i < chuncks[h]->blocks.size(); i++) {
      float distanceFromCurrentBlockToPlayer =
          camPosition.distanceTo(*chuncks[h]->blocks[i]->getPosition());

      if (distanceFromCurrentBlockToPlayer <= MAX_RANGE_PICKER) {
        // Reset block state
        chuncks[h]->blocks[i]->isTarget = 0;
        chuncks[h]->blocks[i]->distance = -1.0f;

        float intersectionPoint;
        if (ray.intersectBox(chuncks[h]->blocks[i]->minCorner,
                             chuncks[h]->blocks[i]->maxCorner,
                             &intersectionPoint)) {
          hitedABlock = 1;
          if (tempTargetDistance == -1.0f ||
              (distanceFromCurrentBlockToPlayer < tempPlayerDistance)) {
            tempTargetBlock = chuncks[h]->blocks[i];
            tempTargetDistance = intersectionPoint;
            tempPlayerDistance = distanceFromCurrentBlockToPlayer;
          }
        }
      }
    }
  }

  if (hitedABlock) {
    this->targetBlock = tempTargetBlock;
    this->targetBlock->isTarget = 1;
    this->targetBlock->distance = tempTargetDistance;
  }
}

// From CrossCraft
struct LightNode {
  uint16_t x, y, z;
  LightNode(uint16_t lx, uint16_t ly, uint16_t lz, uint16_t l)
      : x(lx), y(ly), z(lz), val(l) {}
  uint16_t val;
};

std::queue<LightNode> lightBfsQueue;
std::queue<LightNode> lightRemovalBfsQueue;

std::queue<LightNode> sunlightBfsQueue;
std::queue<LightNode> sunlightRemovalBfsQueue;

auto encodeID(uint16_t x, uint16_t z) -> uint32_t {
  uint16_t nx = x / 16;
  uint16_t ny = z / 16;
  uint32_t id = nx << 16 | (ny & 0xFFFF);
  return id;
}

void checkAddID(uint32_t* updateIDs, uint32_t id) {
  // Check that the ID is not already existing
  for (int i = 0; i < 10; i++) {
    if (id == updateIDs[i]) return;
  }

  // Find a slot to insert.
  for (int i = 0; i < 10; i++) {
    if (updateIDs[i] == 0xFFFFFFFF) {
      updateIDs[i] = id;
      return;
    }
  }
}

void updateID(uint16_t x, uint16_t z, uint32_t* updateIDs) {
  checkAddID(updateIDs, encodeID(x, z));
}

void propagate(uint16_t x, uint16_t y, uint16_t z, uint16_t lightLevel,
               uint32_t* updateIDs) {
  auto map = CrossCraft_World_GetMapPtr();
  if (!BoundCheckMap(map, x, y, z)) return;

  updateID(x, z, updateIDs);

  auto blk = GetBlockFromMap(map, x, y, z);

  if ((blk == 0 || blk == 20 || blk == 18 || (blk >= 8 && blk <= 11) ||
       (blk >= 37 && blk <= 40)) &&
      GetLightFromMap(map, x, y, z) + 2 <= lightLevel) {
    if (blk == 18 || (blk >= 8 && blk <= 11) || (blk >= 37 && blk <= 40)) {
      lightLevel -= 2;
    }
    SetLightInMap(map, x, y, z, lightLevel - 1);
    lightBfsQueue.emplace(x, y, z, 0);
  }
}

void propagate(uint16_t x, uint16_t y, uint16_t z, uint16_t lightLevel) {
  auto map = CrossCraft_World_GetMapPtr();
  if (!BoundCheckMap(map, x, y, z)) return;

  if (GetBlockFromMap(map, x, y, z) == 0 &&
      GetLightFromMap(map, x, y, z) + 2 <= lightLevel) {
    SetLightInMap(map, x, y, z, lightLevel - 1);
    sunlightBfsQueue.emplace(x, y, z, 0);
  }
}

void propagateRemove(uint16_t x, uint16_t y, uint16_t z, uint16_t lightLevel,
                     uint32_t* updateIDs) {
  auto map = CrossCraft_World_GetMapPtr();
  if (!BoundCheckMap(map, x, y, z)) return;

  auto neighborLevel = GetLightFromMap(map, x, y, z);

  updateID(x, z, updateIDs);

  if (neighborLevel != 0 && neighborLevel < lightLevel) {
    SetLightInMap(map, x, y, z, 0);
    lightRemovalBfsQueue.emplace(x, y, z, neighborLevel);
  } else if (neighborLevel >= lightLevel) {
    lightBfsQueue.emplace(x, y, z, 0);
  }
}

void propagateRemove(uint16_t x, uint16_t y, uint16_t z, uint16_t lightLevel) {
  auto map = CrossCraft_World_GetMapPtr();
  if (!BoundCheckMap(map, x, y, z)) return;

  auto neighborLevel = GetLightFromMap(map, x, y, z);

  if (neighborLevel != 0 && neighborLevel < lightLevel) {
    SetLightInMap(map, x, y, z, 0);
    sunlightRemovalBfsQueue.emplace(x, y, z, neighborLevel);
  } else if (neighborLevel >= lightLevel) {
    sunlightBfsQueue.emplace(x, y, z, 0);
  }
}

void updateRemove(uint32_t* updateIDs) {
  while (!lightRemovalBfsQueue.empty()) {
    auto node = lightRemovalBfsQueue.front();

    uint16_t nx = node.x;
    uint16_t ny = node.y;
    uint16_t nz = node.z;
    uint8_t lightLevel = node.val;
    lightRemovalBfsQueue.pop();

    propagateRemove(nx + 1, ny, nz, lightLevel, updateIDs);
    propagateRemove(nx - 1, ny, nz, lightLevel, updateIDs);
    propagateRemove(nx, ny + 1, nz, lightLevel, updateIDs);
    propagateRemove(nx, ny - 1, nz, lightLevel, updateIDs);
    propagateRemove(nx, ny, nz + 1, lightLevel, updateIDs);
    propagateRemove(nx, ny, nz - 1, lightLevel, updateIDs);
  }
}

void updateRemove() {
  while (!lightRemovalBfsQueue.empty()) {
    auto node = lightRemovalBfsQueue.front();

    uint16_t nx = node.x;
    uint16_t ny = node.y;
    uint16_t nz = node.z;
    uint8_t lightLevel = node.val;
    lightRemovalBfsQueue.pop();

    propagateRemove(nx + 1, ny, nz, lightLevel);
    propagateRemove(nx - 1, ny, nz, lightLevel);
    propagateRemove(nx, ny + 1, nz, lightLevel);
    propagateRemove(nx, ny - 1, nz, lightLevel);
    propagateRemove(nx, ny, nz + 1, lightLevel);
    propagateRemove(nx, ny, nz - 1, lightLevel);
  }
}

void updateSpread(uint32_t* updateIDs) {
  while (!lightBfsQueue.empty()) {
    auto node = lightBfsQueue.front();

    uint16_t nx = node.x;
    uint16_t ny = node.y;
    uint16_t nz = node.z;
    uint8_t lightLevel =
        GetLightFromMap(CrossCraft_World_GetMapPtr(), nx, ny, nz);
    lightBfsQueue.pop();

    propagate(nx + 1, ny, nz, lightLevel, updateIDs);
    propagate(nx - 1, ny, nz, lightLevel, updateIDs);
    propagate(nx, ny + 1, nz, lightLevel, updateIDs);
    propagate(nx, ny - 1, nz, lightLevel, updateIDs);
    propagate(nx, ny, nz + 1, lightLevel, updateIDs);
    propagate(nx, ny, nz - 1, lightLevel, updateIDs);
  }
}

void updateSpread() {
  while (!lightBfsQueue.empty()) {
    auto node = lightBfsQueue.front();

    uint16_t nx = node.x;
    uint16_t ny = node.y;
    uint16_t nz = node.z;
    uint8_t lightLevel =
        GetLightFromMap(CrossCraft_World_GetMapPtr(), nx, ny, nz);
    lightBfsQueue.pop();

    propagate(nx + 1, ny, nz, lightLevel);
    propagate(nx - 1, ny, nz, lightLevel);
    propagate(nx, ny + 1, nz, lightLevel);
    propagate(nx, ny - 1, nz, lightLevel);
    propagate(nx, ny, nz + 1, lightLevel);
    propagate(nx, ny, nz - 1, lightLevel);
  }
}

void updateSunlight() {
  while (!sunlightBfsQueue.empty()) {
    auto node = sunlightBfsQueue.front();

    uint16_t nx = node.x;
    uint16_t ny = node.y;
    uint16_t nz = node.z;
    int8_t lightLevel = node.val - 1;
    sunlightBfsQueue.pop();
    if (lightLevel <= 0) continue;

    propagate(nx + 1, ny, nz, lightLevel);
    propagate(nx - 1, ny, nz, lightLevel);
    propagate(nx, ny + 1, nz, lightLevel);
    propagate(nx, ny - 1, nz, lightLevel);
    propagate(nx, ny, nz + 1, lightLevel);
    propagate(nx, ny, nz - 1, lightLevel);
  }
}

void updateSunlightRemove() {
  while (!sunlightRemovalBfsQueue.empty()) {
    auto node = sunlightRemovalBfsQueue.front();

    uint16_t nx = node.x;
    uint16_t ny = node.y;
    uint16_t nz = node.z;
    int8_t lightLevel = node.val;
    sunlightRemovalBfsQueue.pop();
    if (lightLevel <= 0) continue;

    propagateRemove(nx + 1, ny, nz, lightLevel);
    propagateRemove(nx - 1, ny, nz, lightLevel);
    propagateRemove(nx, ny + 1, nz, lightLevel);
    propagateRemove(nx, ny - 1, nz, lightLevel);
    propagateRemove(nx, ny, nz + 1, lightLevel);
    propagateRemove(nx, ny, nz - 1, lightLevel);
  }
}

void CrossCraft_World_AddLight(uint16_t x, uint16_t y, uint16_t z,
                               uint16_t light, uint32_t* updateIDs) {
  SetLightInMap(CrossCraft_World_GetMapPtr(), x, y, z, light);
  updateID(x, z, updateIDs);
  lightBfsQueue.emplace(x, y, z, light);

  updateSpread(updateIDs);
}

void CrossCraft_World_RemoveLight(uint16_t x, uint16_t y, uint16_t z,
                                  uint16_t light, uint32_t* updateIDs) {
  auto map = CrossCraft_World_GetMapPtr();

  auto val = GetLightFromMap(map, x, y, z);
  lightRemovalBfsQueue.emplace(x, y, z, val);

  SetLightInMap(map, x, y, z, light);
  updateID(x, z, updateIDs);

  updateRemove(updateIDs);
  updateSpread(updateIDs);
}

void singleCheck(uint16_t x, uint16_t y, uint16_t z) {
  auto map = CrossCraft_World_GetMapPtr();

  if (y == 0) return;

  auto lv = 15;
  for (int y2 = map->height - 1; y2 >= 0; y2--) {
    auto blk = GetBlockFromMap(map, x, y2, z);

    if (blk == 18 || (blk >= 37 && blk <= 40) || (blk >= 8 && blk <= 11)) {
      lv -= 2;
    } else if (blk != 0 && blk != 20) {
      lv = 0;
    }

    auto lv2 = GetLightFromMap(map, x, y2, z);

    if (lv2 < lv) {
      SetLightInMap(map, x, y2, z, lv);
      sunlightRemovalBfsQueue.emplace(x, y2, z, 0);
    } else {
      SetLightInMap(map, x, y2, z, lv);
      sunlightBfsQueue.emplace(x, y2, z, lv);
    }
  }
}

bool CrossCraft_World_CheckSunLight(uint16_t x, uint16_t y, uint16_t z) {
  singleCheck(x, y, z);
  singleCheck(x + 1, y, z);
  singleCheck(x - 1, y, z);
  singleCheck(x, y, z + 1);
  singleCheck(x, y, z - 1);

  updateSunlightRemove();
  updateSunlight();

  return true;
}

void CrossCraft_World_PropagateSunLight(uint32_t tick) {
  auto map = CrossCraft_World_GetMapPtr();

  for (int x = 0; x < map->length; x++) {
    for (int z = 0; z < map->width; z++) {
      auto lv = 4;
      if (tick >= 0 && tick <= 12000) {
        lv = 15;
      }

      for (int y = map->height - 1; y >= 0; y--) {
        auto blk = GetBlockFromMap(map, x, y, z);

        if (blk == 18 || (blk >= 37 && blk <= 40) || (blk >= 8 && blk <= 11)) {
          lv -= 2;
        } else if (blk != 0 && blk != 20) {
          break;
        }

        if (lv < 0) break;

        SetLightInMap(map, x, y, z, lv);
        sunlightBfsQueue.emplace(x, y, z, lv);
      }
    }
  }

  updateSunlight();
}

void CrossCraft_World_Init() {
  TYRA_LOG("Generating base level template");
  srand(time(NULL));

  CrossCraft_WorldGenerator_Init(rand());

  LevelMap map = {.width = 256,
                  .length = 256,
                  .height = 64,

                  .spawnX = 128,
                  .spawnY = 59,
                  .spawnZ = 128,

                  .blocks = NULL,
                  .data = NULL};
  level.map = map;

  TYRA_LOG("Generated base level template");
}

void CrossCraft_World_Deinit() {
  TYRA_LOG("Destroying the world");

  if (level.map.blocks) free(level.map.blocks);

  if (level.map.data) free(level.map.data);

  // if (level.entities.entities) free(level.entities.entities);

  // if (level.tileEntities.entities) free(level.tileEntities.entities);

  TYRA_LOG("World freed");
}

void CrossCraft_World_Create_Map(uint8_t size) {
  switch (size) {
    case WORLD_SIZE_SMALL: {
      level.map.length = 128;
      level.map.width = 128;
      level.map.height = 64;
      break;
    }
    case WORLD_SIZE_HUGE: {
      level.map.length = 256;
      level.map.width = 256;
      level.map.height = 64;
      break;
    }
    case WORLD_SIZE_NORMAL:
    default: {
      level.map.length = 192;
      level.map.width = 192;
      level.map.height = 64;
      break;
    }
  }

  level.map.spawnX = level.map.length / 2;
  level.map.spawnY = level.map.height / 2;
  level.map.spawnZ = level.map.width / 2;

  uint32_t blockCount = level.map.length * level.map.height * level.map.width;
  level.map.blocks = new uint8_t[blockCount];
  level.map.data = new uint8_t[blockCount];
}

/**
 * @brief Generates the world
 * @TODO Offer a callback for world percentage
 */
void CrossCraft_World_GenerateMap(WorldType worldType) {
  switch (worldType) {
    case WORLD_TYPE_ORIGINAL:
      CrossCraft_WorldGenerator_Generate_Original(&level.map);
      break;
    case WORLD_TYPE_FLAT:
      CrossCraft_WorldGenerator_Generate_Flat(&level.map);
      break;
    case WORLD_TYPE_ISLAND:
      CrossCraft_WorldGenerator_Generate_Island(&level.map);
      break;
    case WORLD_TYPE_WOODS:
      CrossCraft_WorldGenerator_Generate_Woods(&level.map);
      break;
    case WORLD_TYPE_FLOATING:
      CrossCraft_WorldGenerator_Generate_Floating(&level.map);
      break;
  }
}

/**
 * @brief Spawn the player into the world
 */
void CrossCraft_World_Spawn() {}

LevelMap* CrossCraft_World_GetMapPtr() { return &level.map; }