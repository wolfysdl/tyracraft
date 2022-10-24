#pragma once

// Its context
class StateGamePlay;

class PlayingStateBase {
 public:
  PlayingStateBase(StateGamePlay* context) { this->context = context; };
  virtual ~PlayingStateBase(){};
  virtual void init() = 0;
  virtual void update() = 0;
  virtual void render() = 0;
  StateGamePlay* context;
};