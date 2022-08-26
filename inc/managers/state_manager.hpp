#pragma once
#include <tamtypes.h>
#include "states/splash_screen/state_splash_screen.hpp"
#include "states/context.hpp"
#include "camera.hpp"
#include "tyra"

using Tyra::Engine;

class StateManager {
 public:
  StateManager(Engine* t_engine);
  ~StateManager();

  void update(const float& deltaTime, Camera* t_camera);

  Context* context;
  Engine* engine;
};
