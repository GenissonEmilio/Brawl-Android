// Exercises the production checkpoint primitive with value-copied jump buffers
// and a real inactive fiber-stack rollback, without commercial game fixtures.
#include "save_state_jump.h"
#include "host_context.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <signal.h>
#include <vector>

namespace {
namespace Jump = SaveStates::Jump;
HostContext::Handle g_scheduler = nullptr;
HostContext::Handle g_worker = nullptr;
Jump::Context g_checkpoint{};
char* g_top = nullptr;
int g_phase = 0;
constexpr uint64_t kOriginal = 0x123456789ABCDEF0ull;
constexpr uint64_t kChanged = 0x0FEDCBA987654321ull;

void Require(bool value, const char* message) {
    if (!value) {
        std::cerr << message << '\n';
        std::abort();
    }
}

// The test scheduler may restore the worker's upper frames. Keep this live
// switch frame well below that range until production Resume discards it.
[[noreturn, gnu::noinline, gnu::no_stack_protector]] void YieldBelow() {
    volatile char padding[64u << 10];
    padding[0] = 0;
    asm volatile("" : : "m"(padding) : "memory");
    HostContext::Switch(g_scheduler);
    asm volatile("" : : "m"(padding) : "memory");
    Jump::Resume(g_checkpoint);
}

[[gnu::noinline]] void RollbackFrame() {
    // Modified locals crossing a native longjmp must be volatile. The restored
    // inactive stack, not a C++ cached value, must supply the original canary.
    volatile uint64_t canary = kOriginal;
    volatile uint64_t lanes[4] = {kOriginal, ~kOriginal, kOriginal ^ 7u, 0xA5A5A5A5u};
    Jump::Prepare(g_checkpoint);
    if (MKW_SAVE_STATE_CHECKPOINT(g_checkpoint) == 0) {
        g_phase = 1;
        YieldBelow();
    }
    if (g_phase == 1) {
        Require(canary == kOriginal, "ordinary checkpoint resume lost stack locals");
        canary = kChanged;
        for (unsigned i = 0; i < 4; ++i) lanes[i] = kChanged + i;
        g_phase = 2;
        YieldBelow();
    }
    Require(g_phase == 2, "wrong restored continuation");
    Require(canary == kOriginal, "snapshot did not roll back volatile stack local");
    Require(lanes[0] == kOriginal && lanes[1] == ~kOriginal &&
            lanes[2] == (kOriginal ^ 7u) && lanes[3] == 0xA5A5A5A5u,
            "snapshot did not roll back stack array");
    g_phase = 3;
    HostContext::Switch(g_scheduler);
    std::abort();
}

void Worker(void*) {
    // A caller's SP bounds every local in the non-inlined callee beneath it.
    asm volatile("mov %0, sp" : "=r"(g_top));
    RollbackFrame();
    std::abort();
}

void CheckSignalMask() {
    sigset_t original{}, signal{}, current{};
    Require(sigprocmask(SIG_SETMASK, nullptr, &original) == 0, "read original signal mask");
    sigemptyset(&signal);
    sigaddset(&signal, SIGUSR2);
    Require(sigprocmask(SIG_UNBLOCK, &signal, nullptr) == 0, "unblock test signal");
    Jump::Context context{};
    Jump::Prepare(context);
    if (MKW_SAVE_STATE_CHECKPOINT(context) == 0) {
        Require(sigprocmask(SIG_BLOCK, &signal, nullptr) == 0, "block test signal");
        Jump::Resume(context);
    }
    Require(sigprocmask(SIG_SETMASK, nullptr, &current) == 0, "read resumed signal mask");
    Require(sigismember(&current, SIGUSR2) == 1, "checkpoint unexpectedly replayed the signal mask");
    Require(sigprocmask(SIG_SETMASK, &original, nullptr) == 0, "restore original signal mask");
}
} // namespace

int main() {
    CheckSignalMask();
    Require(HostContext::InitializeScheduler(&g_scheduler), "initialize scheduler");
    g_worker = HostContext::Create(1024u << 10, Worker, nullptr);
    Require(g_worker != nullptr, "create worker");
    HostContext::Switch(g_worker);
    Require(g_phase == 1 && Jump::Ready(g_checkpoint), "worker did not checkpoint");
    char* from = Jump::StackPointer(g_checkpoint) - 512;
    Require(from < g_top && g_top - from < (512u << 10), "invalid captured SP / snapshot bounds");
    const auto saved = g_checkpoint;
    const std::vector<uint8_t> bytes(from, g_top);
    HostContext::Switch(g_worker);
    Require(g_phase == 2, "worker did not reach changed state");
    std::memcpy(from, bytes.data(), bytes.size());
    // Restore a complete value-copied opaque native buffer, including cookies
    // and saved registers, just as Snapshot's FiberSlot assignment does.
    g_checkpoint = saved;
    HostContext::Switch(g_worker);
    Require(g_phase == 3, "worker did not finish rollback validation");
    HostContext::Destroy(g_worker);
    HostContext::ShutdownScheduler(g_scheduler);
    std::cout << "passed production jump-buffer copy, fiber stack rollback and signal-mask checks\n";
}
