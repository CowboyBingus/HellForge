// Shared runtime of the HellForge stand-ins for PartyWin.dll and PlayFabMultiplayerWin.dll (PlayFab Party 1.10.16 and
// Multiplayer 1.8.7 C APIs). The stand-ins never reach PlayFab: every call is answered locally and logged to
// %HELLFORGE_SHIM_LOG_DIR% (default %TEMP%\hellforge-shims), one unbuffered line per call, because a session ends with the job
// being terminated and DLL_PROCESS_DETACH never runs.
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <type_traits>

namespace hellforge {

struct FunctionInfo
{
    const char* name;
    bool hasOutput; // The export writes through a pointer parameter, so a default stub fails rather than succeeds.
};

// Defined by the generated source of each DLL.
extern const char g_moduleName[];
extern const uint32_t g_functionCount;
extern const FunctionInfo g_functions[];

// Result of a generated stub: HELLFORGE_SHIM_POLICY ("Name=ok,Other=fail") overrides; otherwise success only when the
// export has no output parameters, so the game never reads an output we did not write.
bool Succeeds(uint32_t index);
// Counts the call; true for the first 32 calls of a function and then every power of two.
bool CountCall(uint32_t index, uint64_t* callNumber);
void WriteLine(const char* text, size_t length);
// A free-form log line, such as a state change being delivered.
void Note(const char* format, ...);

class Line
{
public:
    void Append(const char* format, ...);
    void Argument(const char* value);
    template <class T> void Argument(T value)
    {
        if constexpr (std::is_pointer_v<T>) Append("%p", (const void*)value);
        else if constexpr (std::is_floating_point_v<T>) Append("%g", (double)value);
        else if constexpr (std::is_enum_v<T> || std::is_integral_v<T>) Append("%lld", (long long)value);
        else static_assert(std::is_pointer_v<T>, "unsupported argument type");
    }
    void Write() { WriteLine(text, length); }

private:
    char text[1024];
    size_t length = 0;
};

template <class R, class... A> void Trace(uint32_t index, R result, const A&... arguments)
{
    uint64_t call;
    if (!CountCall(index, &call)) return;
    Line line;
    line.Append("%s #%llu(", g_functions[index].name, (unsigned long long)call);
    bool first = true;
    ((line.Append(first ? "" : ", "), line.Argument(arguments), first = false), ...);
    line.Append(")");
    if constexpr (std::is_pointer_v<R>) line.Append(" -> %p", (const void*)result);
    else line.Append(" -> 0x%llx", (unsigned long long)(uint32_t)result);
    line.Write();
}

// The real libraries initialize outputs before validating, so a failed call still leaves null or zero for the caller.
// HD2 relies on it: its engine wrapper for PFLobbyGetLobbyProperty returns the output unchecked, and a stub that left
// it unwritten crashed game.dll+0x1092908 in strlen (stand-in boot 3, 2026-10-14). Clears one element of an output
// array; never pass in-out parameters.
template <class T> void Clear(T* output)
{
    if constexpr (!std::is_void_v<T> && !std::is_const_v<T>)
        if (output != nullptr) *output = T{};
}

template <class R, class... A> R Generic(uint32_t index, R ok, R fail, const A&... arguments)
{
    R result = Succeeds(index) ? ok : fail;
    Trace(index, result, arguments...);
    return result;
}

}

// An export that no public SDK header declares (the Multiplayer GDK entity-handle variants). HD2 does not import
// these; they exist so the export table matches the original. Without a signature no argument is read, which is
// safe on x64 because the caller owns the argument area.
#define HELLFORGE_UNDECLARED(name) \
    extern "C" HRESULT PFMULTIPLAYER_API name() noexcept { hellforge::Trace(hellforge::fn::name, E_NOTIMPL); return E_NOTIMPL; }
