#pragma once
#include <stddef.h>
#include <stdint.h>
#include "atomic.hpp"
#include "cluster/profiles/video.hpp"

namespace kombi {
namespace pip {

enum class PixelFormat { BGRA8888, RGBA8888, RGB888 };
enum class Fit { Contain, Cover, Stretch };
enum class Backend { NativeScreen, LegacyCapture };

// Observer vocabulary shared with the SDK session interface. Implementations
// report connection/setup, frame presentation, failures and shutdown here.
enum class State {
    Closed,
    Connecting,
    VideoUnavailable,
    SinkUnavailable,
    Ready,
    Streaming,
    Error,
    Stopping
};

// Operation outcomes are separate from observer state; describe() supplies the
// corresponding human-readable label. Keep ordering stable for SDK consumers.
enum class Result {
    Ok,
    Cancelled,
    Busy,
    NoMemory,
    InvalidFrame,
    InvalidVideo,
    FileError,
    VideoUnavailable,
    SinkUnavailable,
    ProtocolError,
    CaptureError,
    CleanupFailed
};
const char* describe(Result result);

// Borrowed pixels; present() consumes them synchronously and never retains them.
struct Frame {
    const uint8_t* data;
    size_t bytes;
    unsigned width, height, stride;
    PixelFormat format;
};

class StopToken {
public:
    StopToken() : value_(0) {}
    void request() {
        detail::atomic_store(&value_, 1u);
    }
    bool requested() const {
        return detail::atomic_load(&value_) != 0;
    }
    // Reset only after the previous worker has joined.
    void reset() {
        detail::atomic_store(&value_, 0u);
    }

private:
    unsigned value_;
    StopToken(const StopToken&) = delete;
    StopToken& operator=(const StopToken&) = delete;
};

struct Playback {
    bool loop;
    Fit fit;
    Playback(bool repeat = false, Fit sizing = Fit::Contain) : loop(repeat), fit(sizing) {}
};
typedef void (*Observer)(State state, void* user);

// One active session per process. Call blocking methods on a worker, not the HMI
// thread. Only StopToken::request() is intended for calls from another thread.
// NativeScreen uses the car-tested secondary QNX output and factory resource.
// LegacyCapture is the old simulator-only direct capture socket.
class Session {
public:
    enum {
        Width = nbt::cluster::VideoProfile::ContentWidth,
        Height = nbt::cluster::VideoProfile::ContentHeight
    };
    explicit Session(StopToken* stop = 0, Observer observer = 0, void* user = 0,
                     Backend backend = Backend::NativeScreen);
    ~Session();
    bool open();
    bool present(const Frame& frame, Fit fit = Fit::Contain);
    bool wait(unsigned milliseconds); // Keeps subscriptions/cookies serviced.
    bool close();                     // Native cancellation, unsubscribe, sockets.
    Result playFile(const char* path, Playback playback = Playback()); // KPV1 file.
    Result result() const;
    unsigned framesPresented() const;

private:
    struct Impl;
    Impl* impl_;
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
};

} // namespace pip
} // namespace kombi
