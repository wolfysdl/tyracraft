#include "entities/player/player.hpp"

using Tyra::Renderer3D;

// ----
// Constructors/Destructors
// ----

Player::Player(Renderer* t_renderer, SoundManager* t_soundManager,
               BlockManager* t_blockManager) {
  this->t_renderer = t_renderer;
  this->t_blockManager = t_blockManager;
  this->t_soundManager = t_soundManager;

  loadPlayerTexture();
  loadMesh();
  loadArmMesh();
  calcStaticBBox();

  isWalkingAnimationSet = false;
  isBreakingAnimationSet = false;
  isStandStillAnimationSet = false;

  isOnGround = true;
  isFlying = false;
  isBreaking = false;

  dynpip.setRenderer(&this->t_renderer->core);
  modelDynpipOptions.antiAliasingEnabled = false;
  modelDynpipOptions.frustumCulling =
      Tyra::PipelineFrustumCulling::PipelineFrustumCulling_None;
  modelDynpipOptions.shadingType = Tyra::PipelineShadingType::TyraShadingFlat;
  modelDynpipOptions.textureMappingType =
      Tyra::PipelineTextureMappingType::TyraNearest;

  // TODO: refactor to handled item, temp stuff...
  {
    this->handledItem->init(t_renderer);
    stpip.setRenderer(&t_renderer->core);
  }

  // Set render pip
  this->setRenderPip(new PlayerFirstPersonRenderPip(this));
}

Player::~Player() {
  currentBlock = nullptr;
  delete hitBox;
  delete handledItem;
  delete this->renderPip;
  t_renderer->getTextureRepository().free(playerTexture);
  walkSequence.clear();
  walkSequence.shrink_to_fit();

  breakBlockSequence.clear();
  breakBlockSequence.shrink_to_fit();

  standStillSequence.clear();
  standStillSequence.shrink_to_fit();

  armStandStillSequence.clear();
  armStandStillSequence.shrink_to_fit();

  armWalkingSequence.clear();
  armWalkingSequence.shrink_to_fit();

  armHitingSequence.clear();
  armHitingSequence.shrink_to_fit();
}

// ----
// Methods
// ----

void Player::update(const float& deltaTime, const Vec4& movementDir,
                    const Vec4& camDir,
                    const std::vector<Chunck*>& loadedChunks,
                    TerrainHeightModel* terrainHeight) {
  isMoving = movementDir.length() >= L_JOYPAD_DEAD_ZONE;
  if (isMoving) {
    // Vec4 min, max;
    // auto tempPositionMatrix = M4x4();
    // tempPositionMatrix.identity();
    Vec4 nextPlayerPos = getNextPosition(deltaTime, movementDir, camDir);

    // tempPositionMatrix.translate(nextPlayerPos);
    // const auto tempPlayerBbox =
    // getHitBox().getTransformed(tempPositionMatrix);
    // tempPlayerBbox.getMinMax(&min, &max);

    // if (min.x < MAX_WORLD_POS.x && max.x > MIN_WORLD_POS.x &&
    //     min.y < MAX_WORLD_POS.y && max.y > MIN_WORLD_POS.y &&
    //     min.z < MAX_WORLD_POS.z && max.z > MIN_WORLD_POS.z) {
    if (nextPlayerPos.collidesBox(MIN_WORLD_POS, MAX_WORLD_POS)) {
      const bool hasChangedPosition =
          this->updatePosition(loadedChunks, deltaTime, nextPlayerPos);

      if (hasChangedPosition && this->isOnGround && this->currentBlock) {
        if (lastTimePlayedWalkSfx > 0.3F) {
          this->playWalkSfx(this->currentBlock->type);
          setWalkingAnimation();
          lastTimePlayedWalkSfx = 0;
        } else {
          lastTimePlayedWalkSfx += deltaTime;
        }
      }
    }
  } else {
    unsetWalkingAnimation();
  }

  if (!isFlying) updateGravity(deltaTime, terrainHeight);

  animate();

  // this->handledItem->mesh->translation.identity();
  // this->handledItem->mesh->translation.operator*=(this->mesh->translation);
}

void Player::render() { renderPip->render(t_renderer); }

Vec4 Player::getNextPosition(const float& deltaTime, const Vec4& sensibility,
                             const Vec4& camDir) {
  Vec4 result =
      Vec4((camDir.x * -sensibility.z) + (camDir.z * -sensibility.x), 0.0F,
           (camDir.z * -sensibility.z) + (camDir.x * sensibility.x));
  result.normalize();
  result *= (this->speed * sensibility.length() * deltaTime);
  return result + *mesh->getPosition();
}

/** Update player position by gravity and update index of current block */
void Player::updateGravity(const float& deltaTime,
                           TerrainHeightModel* terrainHeight) {
  const float worldMinHeight = OVERWORLD_MIN_HEIGH * DUBLE_BLOCK_SIZE;
  const float worldMaxHeight = OVERWORLD_MAX_HEIGH * DUBLE_BLOCK_SIZE;

  // Accelerate the velocity: velocity += gravConst * deltaTime
  Vec4 acceleration = GRAVITY * deltaTime;
  this->velocity += acceleration;

  // Increase the position by velocity
  Vec4 newYPosition = *mesh->getPosition() - (this->velocity * deltaTime);

  if (newYPosition.y + hitBox->getHeight() > worldMaxHeight ||
      newYPosition.y < worldMinHeight) {
    // Maybe has died, teleport to spaw area
    TYRA_LOG("\nReseting player position to:\n");
    this->mesh->getPosition()->set(this->spawnArea);
    this->velocity = Vec4(0.0f, 0.0f, 0.0f);
    return;
  }

  const float heightLimit =
      terrainHeight->maxHeight - this->hitBox->getHeight();

  if (newYPosition.y < terrainHeight->minHeight) {
    newYPosition.y = terrainHeight->minHeight;
    this->velocity = Vec4(0.0f, 0.0f, 0.0f);
    this->isOnGround = true;
  } else if (newYPosition.y > heightLimit) {
    newYPosition.y = heightLimit;
    this->velocity = -this->velocity;
    this->isOnGround = false;
  }

  // Finally updates gravity after checks
  mesh->getPosition()->set(newYPosition);
}

/** Fly in up direction */
void Player::flyUp(const float& deltaTime,
                   const TerrainHeightModel& terrainHeight) {
  const Vec4 upDir = GRAVITY * -10.0F * deltaTime;
  this->fly(deltaTime, terrainHeight, upDir);
}

/** Fly in down direction */
void Player::flyDown(const float& deltaTime,
                     const TerrainHeightModel& terrainHeight) {
  const Vec4 downDir = GRAVITY * 10.0F * deltaTime;
  this->fly(deltaTime, terrainHeight, downDir);
}

/** Fly in given direction */
void Player::fly(const float& deltaTime,
                 const TerrainHeightModel& terrainHeight,
                 const Vec4& direction) {
  Vec4 newYPosition = *mesh->getPosition() - (direction * deltaTime);

  // Is player inside world bbox?
  if (newYPosition.y + hitBox->getHeight() >=
          OVERWORLD_MAX_HEIGH * DUBLE_BLOCK_SIZE ||
      newYPosition.y < OVERWORLD_MIN_HEIGH * DUBLE_BLOCK_SIZE) {
    return;
  }

  if (newYPosition.y < terrainHeight.minHeight) {
    newYPosition.y = terrainHeight.minHeight;
    this->isOnGround = true;
  } else if (newYPosition.y >
             terrainHeight.maxHeight + this->hitBox->getHeight()) {
    newYPosition.y = terrainHeight.maxHeight + this->hitBox->getHeight();
  }

  mesh->getPosition()->set(newYPosition);
}

u8 Player::updatePosition(const std::vector<Chunck*>& loadedChunks,
                          const float& deltaTime, const Vec4& nextPlayerPos,
                          u8 isColliding) {
  Vec4 currentPlayerPos = *this->mesh->getPosition();
  Vec4 playerMin;
  Vec4 playerMax;
  BBox playerBB = getHitBox();
  playerBB.getMinMax(&playerMin, &playerMax);

  // Set ray props
  Vec4 rayOrigin = ((playerMax - playerMin) / 2) + playerMin;
  Vec4 rayDir = nextPlayerPos - currentPlayerPos;
  rayDir.normalize();
  const Ray ray = Ray(rayOrigin, rayDir);

  float finalHitDistance = -1.0f;
  float tempHitDistance = -1.0f;
  const float maxCollidableDistance =
      currentPlayerPos.distanceTo(nextPlayerPos);

  for (size_t chunkIndex = 0; chunkIndex < loadedChunks.size(); chunkIndex++) {
    for (size_t i = 0; i < loadedChunks[chunkIndex]->blocks.size(); i++) {
      // Broad phase
      // is vertically out of range?
      if (playerBB.getBottomFace().axisPosition >=
              loadedChunks[chunkIndex]->blocks[i]->maxCorner.y ||
          playerBB.getTopFace().axisPosition <
              loadedChunks[chunkIndex]->blocks[i]->minCorner.y ||
          currentPlayerPos.distanceTo(
              *loadedChunks[chunkIndex]->blocks[i]->getPosition()) >
              DUBLE_BLOCK_SIZE * 2) {
        continue;
      };

      Vec4 tempInflatedMin;
      Vec4 tempInflatedMax;
      Utils::GetMinkowskiSum(playerMin, playerMax,
                             loadedChunks[chunkIndex]->blocks[i]->minCorner,
                             loadedChunks[chunkIndex]->blocks[i]->maxCorner,
                             &tempInflatedMin, &tempInflatedMax);

      if (ray.intersectBox(tempInflatedMin, tempInflatedMax,
                           &tempHitDistance)) {
        // Is horizontally collidable?
        if (tempHitDistance > maxCollidableDistance) continue;

        if (finalHitDistance == -1.0f || tempHitDistance < finalHitDistance) {
          finalHitDistance = tempHitDistance;
        }
      }
    }
  }

  // Will collide somewhere;
  if (finalHitDistance > -1.0f) {
    const float timeToHit = finalHitDistance / this->speed;

    // Will collide this frame;
    if (timeToHit < deltaTime ||
        finalHitDistance <
            this->mesh->getPosition()->distanceTo(nextPlayerPos)) {
      if (isColliding) return false;

      // Try to move in separated axis;
      Vec4 moveOnXOnly =
          Vec4(nextPlayerPos.x, currentPlayerPos.y, currentPlayerPos.z);
      if (updatePosition(loadedChunks, deltaTime, moveOnXOnly, 1)) return true;

      Vec4 moveOnZOnly =
          Vec4(currentPlayerPos.x, nextPlayerPos.y, currentPlayerPos.z);
      if (updatePosition(loadedChunks, deltaTime, moveOnZOnly, 1)) return true;

      return false;
    }
  }

  // Apply new position;
  mesh->getPosition()->x = nextPlayerPos.x;
  mesh->getPosition()->z = nextPlayerPos.z;
  return true;
}

TerrainHeightModel Player::getTerrainHeightAtPosition(
    const std::vector<Chunck*>& loadedChunks) {
  TerrainHeightModel model;
  BBox playerBB = this->getHitBox();
  Vec4 minPlayer, maxPlayer;
  playerBB.getMinMax(&minPlayer, &maxPlayer);

  this->currentBlock = nullptr;

  for (size_t chunkIndex = 0; chunkIndex < loadedChunks.size(); chunkIndex++) {
    for (size_t i = 0; i < loadedChunks[chunkIndex]->blocks.size(); i++) {
      // Is on block?
      if (minPlayer.x < loadedChunks[chunkIndex]->blocks[i]->maxCorner.x &&
          maxPlayer.x > loadedChunks[chunkIndex]->blocks[i]->minCorner.x &&
          minPlayer.z < loadedChunks[chunkIndex]->blocks[i]->maxCorner.z &&
          maxPlayer.z > loadedChunks[chunkIndex]->blocks[i]->minCorner.z) {
        const float underBlockHeight =
            loadedChunks[chunkIndex]->blocks[i]->maxCorner.y;
        if (minPlayer.y >= underBlockHeight &&
            underBlockHeight > model.minHeight) {
          model.minHeight = underBlockHeight;
          this->currentBlock = loadedChunks[chunkIndex]->blocks[i];
        }

        const float overBlockHeight =
            loadedChunks[chunkIndex]->blocks[i]->minCorner.y;
        if (maxPlayer.y <= overBlockHeight &&
            overBlockHeight < model.maxHeight) {
          model.maxHeight = overBlockHeight;
        }
      }
    }
  }

  return model;
}

/**
 * Inventory controllers
 *
 */

ItemId Player::getSelectedInventoryItemType() {
  return this->inventory[this->selectedInventoryIndex];
}

/**
 * @brief Return selected slot - int between 1 and 9
 *
 */
u8 Player::getSelectedInventorySlot() {
  return this->selectedInventoryIndex + 1;
}

void Player::moveSelectorToTheLeft() {
  selectedInventoryIndex--;
  if (selectedInventoryIndex < 0)
    selectedInventoryIndex = HOT_INVENTORY_SIZE - 1;
  selectedSlotHasChanged = 1;
}

void Player::moveSelectorToTheRight() {
  selectedInventoryIndex++;
  if (selectedInventoryIndex > HOT_INVENTORY_SIZE - 1)
    selectedInventoryIndex = 0;
  selectedSlotHasChanged = 1;
}

void Player::loadMesh() {
  ObjLoaderOptions options;
  options.scale = 15.0F;
  options.flipUVs = true;
  options.animation.count = 10;

  auto data =
      ObjLoader::load(FileUtils::fromCwd("models/player/player.obj"), options);
  data.get()->loadNormals = false;

  this->mesh = std::make_unique<DynamicMesh>(data.get());

  this->mesh->rotation.identity();
  this->mesh->rotation.rotateY(-3.14F);
  this->mesh->scale.identity();

  auto& materials = this->mesh.get()->materials;
  for (size_t i = 0; i < materials.size(); i++)
    playerTexture->addLink(materials[i]->id);

  this->mesh->animation.loop = true;
  this->mesh->animation.setSequence(standStillSequence);
  this->mesh->animation.speed = 0.08F;
}

void Player::loadArmMesh() {
  ObjLoaderOptions options;
  options.scale = 15.0F;
  options.flipUVs = true;
  options.animation.count = 21;

  auto data =
      ObjLoader::load(FileUtils::fromCwd("models/player_arm/arm.obj"), options);
  data.get()->loadNormals = false;

  this->armMesh = std::make_unique<DynamicMesh>(data.get());

  this->armMesh->scale.identity();
  this->armMesh->scale.scaleX(0.7F);
  this->armMesh->scale.scaleZ(1.1F);

  this->armMesh->translation.identity();
  this->armMesh->translation.translateZ(-13.5F);
  this->armMesh->translation.translateY(-8.0F);
  this->armMesh->translation.translateX(5.0F);

  this->armMesh->rotation.identity();
  this->armMesh->rotation.rotateY(-3.24);

  auto& materials = this->armMesh.get()->materials;
  for (size_t i = 0; i < materials.size(); i++)
    playerTexture->addLink(materials[i]->id);

  this->armMesh->animation.loop = true;
  this->armMesh->animation.setSequence(armStandStillSequence);
  this->armMesh->animation.speed = 0.08F;

  armDynpipOptions.antiAliasingEnabled = false;
  armDynpipOptions.frustumCulling =
      Tyra::PipelineFrustumCulling::PipelineFrustumCulling_None;
  armDynpipOptions.shadingType = Tyra::PipelineShadingType::TyraShadingFlat;
  armDynpipOptions.textureMappingType =
      Tyra::PipelineTextureMappingType::TyraNearest;
  armDynpipOptions.transformationType =
      Tyra::PipelineTransformationType::TyraMP;
}

void Player::calcStaticBBox() {
  const float width = (DUBLE_BLOCK_SIZE * 0.4F) / 2;
  const float depth = (DUBLE_BLOCK_SIZE * 0.4F) / 2;
  const float height = DUBLE_BLOCK_SIZE * 1.8F;

  Vec4 minCorner = Vec4(-width, 0, -depth);
  Vec4 maxCorner = Vec4(width, height, depth);

  const u32 count = 8;
  Vec4 vertices[count] = {Vec4(minCorner),
                          Vec4(maxCorner.x, minCorner.y, minCorner.z),
                          Vec4(minCorner.x, maxCorner.y, minCorner.z),
                          Vec4(minCorner.x, minCorner.y, maxCorner.z),
                          Vec4(maxCorner),
                          Vec4(minCorner.x, maxCorner.y, maxCorner.z),
                          Vec4(maxCorner.x, minCorner.y, maxCorner.z),
                          Vec4(maxCorner.x, maxCorner.y, minCorner.z)};

  this->hitBox = new BBox(vertices, count);

  // Hitbox Debug stuff
  // {
  //   TYRA_LOG("==================");
  //   TYRA_LOG("DUBLE_BLOCK_SIZE -> ",
  //   std::to_string(DUBLE_BLOCK_SIZE).c_str()); TYRA_LOG("minCorner");
  //   minCorner.print();
  //   TYRA_LOG("maxCorner");
  //   maxCorner.print();
  //   TYRA_LOG("Player->StaticBBox");
  //   this->hitBox->print();
  //   TYRA_LOG("==================");
  // }
}

void Player::playWalkSfx(const Blocks& blockType) {
  SfxBlockModel* blockSfxModel =
      this->t_blockManager->getStepSoundByBlockType(blockType);
  if (blockSfxModel) {
    const int ch = this->t_soundManager->getAvailableChannel();
    this->t_soundManager->setSfxVolume(75, ch);
    this->t_soundManager->playSfx(blockSfxModel->category, blockSfxModel->sound,
                                  ch);
    Tyra::Threading::switchThread();
  }
}

void Player::toggleFlying() {
  this->isFlying = !this->isFlying;
  if (this->isFlying) {
    this->t_renderer->core.renderer3D.setFov(70.0F);
    this->isOnGround = false;
  } else {
    this->t_renderer->core.renderer3D.setFov(60.0F);
  }
}

void Player::selectNextItem() {
  int currentItemId = (int)inventory[selectedInventoryIndex];
  ItemId nextItem;

  if ((currentItemId + 1) >= (int)ItemId::total_of_items) {
    nextItem = ItemId::empty;
  } else {
    nextItem = static_cast<ItemId>(currentItemId + 1);
  }

  inventory[selectedInventoryIndex] = nextItem;
  this->inventoryHasChanged = true;
}

void Player::selectPreviousItem() {
  int currentItemId = (int)inventory[selectedInventoryIndex];
  ItemId previousItem;

  if ((currentItemId - 1) < 0) {
    previousItem = ItemId::emerald_ore_block;
  } else {
    previousItem = static_cast<ItemId>(currentItemId - 1);
  }

  inventory[selectedInventoryIndex] = previousItem;
  this->inventoryHasChanged = true;
}

void Player::jump() {
  this->velocity += this->lift * this->speed;
  this->isOnGround = false;
}

void Player::setRenderPip(PlayerRenderPip* pipToSet) {
  delete this->renderPip;
  this->renderPip = pipToSet;
}

void Player::setArmBreakingAnimation() {
  if (isBreakingAnimationSet) return;

  mesh->animation.speed = baseAnimationSpeed * 3;
  mesh->animation.setSequence(breakBlockSequence);

  if (isHandFree()) {
    armMesh->animation.speed = baseAnimationSpeed * 6;
    armMesh->animation.setSequence(armHitingSequence);
  }

  isBreakingAnimationSet = true;
  isBreaking = true;
}

void Player::unsetArmBreakingAnimation() {
  if (!isBreakingAnimationSet) return;

  isBreaking = false;
  isBreakingAnimationSet = false;
  mesh->animation.setSequence(standStillSequence);
  armMesh->animation.setSequence(armStandStillSequence);
}

void Player::setWalkingAnimation() {
  if (isWalkingAnimationSet) return;

  this->mesh->animation.speed = baseAnimationSpeed;
  this->mesh->animation.setSequence(walkSequence);

  if (isHandFree()) {
    this->armMesh->animation.speed = baseAnimationSpeed * 3;
    this->armMesh->animation.setSequence(armWalkingSequence);
  }

  isWalkingAnimationSet = true;
}

void Player::unsetWalkingAnimation() {
  if (!isWalkingAnimationSet) return;

  isWalkingAnimationSet = false;
  armMesh->animation.setSequence(armStandStillSequence);
  mesh->animation.setSequence(standStillSequence);
}

void Player::animate() {
  // TODO: check cam type before animate
  this->mesh->update();
  if (isHandFree()) this->armMesh->update();
}

void Player::shiftItemToInventory(const ItemId& itemToShift) {
  for (size_t i = HOT_INVENTORY_SIZE - 1; i > 0; i--) {
    inventory[i] = inventory[i - 1];
  }

  inventory[0] = itemToShift;
  inventoryHasChanged = true;
}

void Player::setItemToInventory(const ItemId& itemToShift) {
  inventory[selectedInventoryIndex] = itemToShift;
  inventoryHasChanged = true;
}

void Player::loadPlayerTexture() {
  playerTexture = t_renderer->getTextureRepository().add(
      FileUtils::fromCwd("textures/entity/player/steve.png"));
}
