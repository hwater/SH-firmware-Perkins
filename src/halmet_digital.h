#ifndef __SRC_HALMET_DIGITAL_H__
#define __SRC_HALMET_DIGITAL_H__

#include "sensesp/sensors/sensor.h"

using namespace sensesp;

// hz_out (optional): if non-null, the raw input pulse frequency (Hz) is
// mirrored here on every read, for display/diagnostics (tacho calibration).
FloatProducer* ConnectTachoSender(int pin, String name, float* hz_out = nullptr);
BoolProducer* ConnectAlarmSender(int pin, String name);

#endif
