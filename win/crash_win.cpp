// Windows crash reporter (generated tree only): prints the faulting address as an offset into darkcloud.exe and the
// return-address-looking values on the stack, so a crash can be mapped to a function with `llvm-nm -n darkcloud.exe`.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <cstdio>

namespace {

LONG WINAPI Handler(EXCEPTION_POINTERS *info) {
    const EXCEPTION_RECORD *record = info->ExceptionRecord;
    const CONTEXT          *context = info->ContextRecord;
    auto  base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
    std::uintptr_t size = nt->OptionalHeader.SizeOfImage;
    std::fprintf(stderr, "\n=== CRASH: exception 0x%08lX at RVA 0x%llX (image base 0x%llX)\n", record->ExceptionCode,
                 static_cast<unsigned long long>(context->Rip - base), static_cast<unsigned long long>(base));
    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
        std::fprintf(stderr, "    %s address 0x%llX\n", record->ExceptionInformation[0] == 0 ? "read of" : (record->ExceptionInformation[0] == 1 ? "write to" : "execute of"),
                     static_cast<unsigned long long>(record->ExceptionInformation[1]));
    }
    std::fprintf(stderr, "    rax=%llX rcx=%llX rdx=%llX r8=%llX r9=%llX rsp=%llX\n", (unsigned long long) context->Rax,
                 (unsigned long long) context->Rcx, (unsigned long long) context->Rdx, (unsigned long long) context->R8,
                 (unsigned long long) context->R9, (unsigned long long) context->Rsp);
    std::fprintf(stderr, "    stack values that point into the exe (RVAs, nearest first):");
    auto *top = reinterpret_cast<const std::uintptr_t *>(context->Rsp);
    auto *end = reinterpret_cast<const std::uintptr_t *>(reinterpret_cast<const NT_TIB *>(NtCurrentTeb())->StackBase);
    int   shown = 0;
    for (auto *p = top; p < end && p < top + 4096 && shown < 48; ++p) {
        std::uintptr_t value = *p;
        if (value > base && value < base + size) {
            std::fprintf(stderr, " %llX", static_cast<unsigned long long>(value - base));
            ++shown;
        }
    }
    std::fprintf(stderr, "\n=== (map RVAs: llvm-nm -n darkcloud.exe; RVA + 0x%llX = address in that listing)\n", 0x140000000ull);
    std::fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

void InstallCrashHandler() { SetUnhandledExceptionFilter(Handler); }
