// Workaround: SensESP 3.2.0 only emits the Nullable<int> static specialization
// when ESP_ARDUINO_VERSION_MAJOR >= 3. On Arduino-ESP32 v2 with the xtensa-esp32
// toolchain, int32_t is `long`, so the int32_t specialization does not cover
// `int`, leaving Nullable<int>::invalid_value_ undefined at link time. Supply
// it here. Remove this file once SensESP drops the version guard or the project
// moves to Arduino-ESP32 v3.

#include "sensesp/types/nullable.h"

namespace sensesp {
template <>
int Nullable<int>::invalid_value_ = 0x7fffffff;
}  // namespace sensesp
