// Stand-in for Arduino's Wire (tools/build_sim.py): the touch controller answers from the finger JavaScript
// has put on the screen (tools/sim/sim.cpp); anything else on the bus does not answer, as when it is not fitted.
#pragma once
#include <Arduino.h>

class TwoWire {
public:
  bool begin(int, int, uint32_t = 100000) { return true; }
  void setTimeOut(uint16_t) {}
  void setClock(uint32_t) {}
  void beginTransmission(uint8_t addr) { addr_ = addr; }
  size_t write(uint8_t b) { reg_ = b; return 1; }
  size_t write(const uint8_t*, size_t n) { return n; }
  uint8_t endTransmission(bool = true) { return addr_ == 0x38 ? 0 : 2; }  // 2: no answer
  uint8_t requestFrom(uint8_t addr, uint8_t n, uint8_t = true);
  int read() { return pos_ < have_ ? buf_[pos_++] : -1; }
  int available() { return have_ - pos_; }

private:
  uint8_t addr_ = 0, reg_ = 0, buf_[8] = {}, have_ = 0, pos_ = 0;
};
extern TwoWire Wire;
