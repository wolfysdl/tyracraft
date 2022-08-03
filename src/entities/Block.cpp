#include "entities/Block.hpp"
#include "renderer/3d/pipeline/minecraft/mcpip_block.hpp"

using Tyra::BBox;
using Tyra::M4x4;
using Tyra::McpipBlock;

Block::Block(BlockInfo* blockInfo) {
  this->type = blockInfo->blockId;
  this->isSingleTexture = blockInfo->_isSingle;
  this->textureOffset =
      Vec4(blockInfo->_texOffssetX, blockInfo->_texOffssetY, 0.0F, 1.0F);
  // this->bbox = new BBox(data.frames[index]->vertices,
  // data.frames[index]->verticesCount);
}

Block::~Block() {}

void Block::updateModelMatrix() { model = translation * rotation * scale; }

void Block::setPosition(const Vec4& v) {
  TYRA_ASSERT(v.w == 1.0F, "Vec4 must be homogeneous");
  reinterpret_cast<Vec4*>(&translation.data[3 * 4])->set(v);
}
