#include "plat.h"
#include <windows.h>
#include <detours.h>

#if defined(__HIP_PLATFORM_AMD__)

typedef struct {
    void **true_ptr;
    void *hook_ptr;
    const char *name;
} HookEntry;

static int (*true_cuMemAlloc_v2)(CUdeviceptr*, size_t);
static int (*true_cuMemFree_v2)(CUdeviceptr);
static int (*true_cuMemAllocAsync)(CUdeviceptr*, size_t, CUstream);
static int (*true_cuMemFreeAsync)(CUdeviceptr, CUstream);

static int aimdo_cuMemAlloc_v2(CUdeviceptr* dptr, size_t size) {
    return aimdo_cuda_malloc(dptr, size, true_cuMemAlloc_v2);
}

static int aimdo_cuMemFree_v2(CUdeviceptr dptr) {
    return aimdo_cuda_free(dptr, true_cuMemFree_v2);
}

static int aimdo_cuMemAllocAsync(CUdeviceptr* dptr, size_t size, CUstream hStream) {
    return aimdo_cuda_malloc_async(dptr, size, hStream, true_cuMemAllocAsync);
}

static int aimdo_cuMemFreeAsync(CUdeviceptr dptr, CUstream hStream) {
    return aimdo_cuda_free_async(dptr, hStream, true_cuMemFreeAsync);
}

static const HookEntry hooks[] = {
    { (void **)&true_cuMemAlloc_v2, (void *)aimdo_cuMemAlloc_v2, "hipMalloc" },
    { (void **)&true_cuMemFree_v2, (void *)aimdo_cuMemFree_v2, "hipFree" },
    { (void **)&true_cuMemAllocAsync, (void *)aimdo_cuMemAllocAsync, "hipMallocAsync" },
    { (void **)&true_cuMemFreeAsync, (void *)aimdo_cuMemFreeAsync, "hipFreeAsync" },
};

static inline bool install_hook_entries(HMODULE module, const HookEntry *hook_entries,
                                        size_t num_hooks) {
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    int status;

    for (size_t i = 0; i < num_hooks; i++) {
        *hook_entries[i].true_ptr = (void *)GetProcAddress(module, hook_entries[i].name);
        if (!*hook_entries[i].true_ptr ||
            DetourAttach(hook_entries[i].true_ptr, hook_entries[i].hook_ptr) != 0) {
            log(ERROR, "%s: Hook %s failed %p\n", __func__, hook_entries[i].name,
                *hook_entries[i].true_ptr);
            DetourTransactionAbort();
            return false;
        }
    }

    status = (int)DetourTransactionCommit();
    if (status != 0) {
        log(ERROR, "%s: DetourTransactionCommit failed: %d", __func__, status);
        return false;
    }

    log(DEBUG, "%s: hooks successfully installed\n", __func__);
    return true;
}

bool aimdo_setup_hooks() {
    HMODULE hip_module = GetModuleHandleA("amdhip64.dll");

    if (!hip_module) {
        hip_module = GetModuleHandleA("amdhip64_7.dll");
    }
    if (!hip_module) {
        log(ERROR, "%s: No suitable driver found in process memory", __func__);
        return false;
    }

    log(INFO, "%s: found driver at %p, installing %zu hooks\n", __func__, hip_module,
        sizeof(hooks) / sizeof(hooks[0]));

    return install_hook_entries(hip_module, hooks, sizeof(hooks) / sizeof(hooks[0]));
}

void aimdo_teardown_hooks() {
    int status;

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    for (size_t i = 0; i < sizeof(hooks) / sizeof(hooks[0]); i++) {
        if (*hooks[i].true_ptr) {
            DetourDetach(hooks[i].true_ptr, hooks[i].hook_ptr);
        }
    }

    status = (int)DetourTransactionCommit();
    if (status != 0) {
        log(ERROR, "%s: DetourDetach failed: %d", __func__, status);
    } else {
        log(DEBUG, "%s: hooks successfully removed\n", __func__);
    }
}

#else

#include "cuda-hooks-shared.h"

static inline bool install_hook_entries(HookEntry *hooks, size_t num_hooks) {
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    int status;

    for (size_t i = 0; i < num_hooks; i++) {
        *hooks[i].true_ptr = *hooks[i].target_ptr;
        if (!*hooks[i].true_ptr ||
            DetourAttach(hooks[i].true_ptr, hooks[i].hook_ptr) != 0) {
            log(ERROR, "%s: Hook %s failed %p\n", __func__, hooks[i].name, *hooks[i].true_ptr);
            DetourTransactionAbort();
            return false;
        }
    }

    status = (int)DetourTransactionCommit();
    if (status != 0) {
        log(ERROR, "%s: DetourTransactionCommit failed: %d", __func__, status);
        return false;
    }

    log(DEBUG, "%s: hooks successfully installed\n", __func__);
    return true;
}

bool aimdo_setup_hooks() {
    if (!g_cuda.p_cuGetProcAddress) {
        log(ERROR, "%s: CUDA runtime dispatch is not initialized\n", __func__);
        return false;
    }

    log(INFO, "%s: installing %zu hooks\n", __func__, sizeof(hooks) / sizeof(HookEntry));

    return install_hook_entries((HookEntry *)hooks, sizeof(hooks) / sizeof(hooks[0]));
}

void aimdo_teardown_hooks() {
    int status;

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    for (size_t i = 0; i < sizeof(hooks) / sizeof(hooks[0]); i++) {
        /* Only detach if we actually successfully resolved the pointer */
        if (*hooks[i].true_ptr) {
            DetourDetach(hooks[i].true_ptr, hooks[i].hook_ptr);
        }
    }

    status = (int)DetourTransactionCommit();
    if (status != 0) {
        log(ERROR, "%s: DetourDetach failed: %d", __func__, status);
    } else {
        log(DEBUG, "%s: hooks successfully removed\n", __func__);
    }
}

#endif
