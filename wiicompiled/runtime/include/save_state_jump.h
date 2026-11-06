#pragma once

// Save-state checkpoints have to be created in SwitchFiber's own frame. In
// particular, never put setjmp in a function that returns before Resume runs.
#if defined(__aarch64__)
#include <setjmp.h>
#endif

namespace SaveStates::Jump {

struct Context {
#if defined(__aarch64__)
    // Bionic's buffer contains a cookie/checksum and authenticated pointers.
    // Treat it as opaque and copy it in full with the surrounding FiberSlot.
    jmp_buf buffer{};
    void* stackPointer = nullptr;
    bool ready = false;
#else
    // Preserve the established x86 builtin ABI and its compiler-managed
    // callee-saved register spills in the checkpoint's own stack frame.
    void* buffer[5]{};
#endif
};

[[gnu::always_inline]] inline void Prepare(Context& context)
{
#if defined(__aarch64__)
    // This must be inlined so the pointer describes the checkpoint's frame.
    asm volatile("mov %0, sp" : "=r"(context.stackPointer));
    context.ready = true;
#endif
}

inline bool Ready(const Context& context)
{
#if defined(__aarch64__)
    return context.ready;
#else
    return context.buffer[1] != nullptr;
#endif
}

inline char* StackPointer(const Context& context)
{
#if defined(__aarch64__)
    return static_cast<char*>(context.stackPointer);
#else
    return static_cast<char*>(context.buffer[2]);
#endif
}

[[noreturn]] inline void Resume(Context& context)
{
#if defined(__aarch64__)
    // _setjmp/_longjmp preserve the builtin path's signal-mask semantics.
    // The buffer is restored on its original fiber and original host thread.
    _longjmp(context.buffer, 1);
#else
    __builtin_longjmp(context.buffer, 1);
#endif
}

} // namespace SaveStates::Jump

// Keep this expansion as the setjmp call itself: it is used directly in the
// controlling expression `if (MKW_SAVE_STATE_CHECKPOINT(context) == 0)`.
#if defined(__aarch64__)
#define MKW_SAVE_STATE_CHECKPOINT(context) _setjmp((context).buffer)
#else
#define MKW_SAVE_STATE_CHECKPOINT(context) __builtin_setjmp((context).buffer)
#endif
