#pragma once

#include "entities/item.hpp"
#include "items_repository.hpp"
#include "contants.hpp"
#include "file/file_utils.hpp"

using Tyra::Renderer;
using Tyra::TextureRepository;

using Tyra::Sprite;

class ItemIdRepository {
 public:
 ItemId();
 ~ItemId();


  // Item Id

  // Blocks
  Item dirt;
  Item sand;
  Item stone;
  Item bricks;
  Item glass;

  // Ores and Minerals blocks
  Item coal_ore_block;
  Item diamond_ore_block;
  Item iron_ore_block;
  Item gold_ore_block;
  Item redstone_ore_block;
  Item emerald_ore_block;

  // Wood Planks
  Item oak_planks;
  Item spruce_planks;
  Item birch_planks;
  Item acacia_planks;

  // Stone Bricks
  Item stone_brick;
  Item cracked_stone_bricks;
  Item mossy_stone_bricks;
  Item chiseled_stone_bricks;

  // Stripped woods
  Item stripped_oak_wood;

  // Toolds
  Item wooden_axe;
};
