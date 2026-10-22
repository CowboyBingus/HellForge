// The stand-ins' connection to the local HellForge backend: the named pipe in %HELLFORGE_PIPE% (HellForge.LocalBackend --pipe). Frames are
// a type and a list of byte strings, encoded exactly like src/HellForge.LocalBackend/Multiplayer/Frame.cs: u32 length, u16
// field count, then per field a u32 length and the bytes (field 0 is the type), little-endian. A reader thread queues
// incoming frames; the game's thread drains them while it processes state changes.
#pragma once
#include "shim_common.h"
#include <map>
#include <string>
#include <vector>

namespace hellforge {

struct Frame
{
    std::string type;
    std::vector<std::string> fields;

    explicit Frame(std::string frameType = "") : type(std::move(frameType)) {}
    Frame& Add(const std::string& text) { fields.push_back(text); return *this; }
    Frame& Add(const char* text) { fields.emplace_back(text != nullptr ? text : ""); return *this; }
    Frame& Add(long long number) { fields.push_back(std::to_string(number)); return *this; }
    Frame& AddProperties(const std::map<std::string, std::string>& properties);
    std::string Encode() const;
};

// Reads a frame's fields in order; a missing or malformed field makes ok false and returns empty values.
class FrameReader
{
public:
    explicit FrameReader(const Frame& frame, size_t start = 0) : frame_(frame), index_(start) {}
    std::string Text();
    long long Int();
    std::map<std::string, std::string> Properties();
    // Changes: key -> value, where a removed key has no value.
    struct Change { std::string key; bool remove; std::string value; };
    std::vector<Change> Changes();
    bool ok = true;

private:
    const Frame& frame_;
    size_t index_;
};

class Link
{
public:
    // Connects to %HELLFORGE_PIPE% and introduces this DLL as component. False when the variable is unset or the backend
    // does not answer; the stand-in then works for this machine only.
    bool Connect(const char* component);
    bool Connected() const { return pipe_ != nullptr && !broken_; }
    // False once the connection has failed.
    bool Send(const Frame& frame);
    // Frames received since the last call, in order.
    std::vector<Frame> Drain();
    // True once a connection that existed has ended (the backend went away).
    bool Broken() const { return broken_; }

private:
    static DWORD WINAPI ReaderThread(void* self);
    void Read();
    HANDLE pipe_ = nullptr;
    HANDLE writeEvent_ = nullptr;
    volatile bool broken_ = false;
    SRWLOCK writeLock_ = SRWLOCK_INIT;
    SRWLOCK inboxLock_ = SRWLOCK_INIT;
    std::vector<Frame> inbox_;
};

}
