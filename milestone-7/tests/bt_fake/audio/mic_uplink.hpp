#pragma once
#include <stdint.h>
#include <stddef.h>
namespace nbt {
typedef void (*MicDataCallback)(const uint8_t*, size_t, void*);
class MicUplink { public: bool start() { return true; } void stop() {} };
}
