#define _GNU_SOURCE

#include "plat.h"

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
static HMODULE g_hip_module;
#else
#include <dlfcn.h>
#include <link.h>
static void *g_hip_module;
#endif

AimdoCudaDispatch g_cuda;

typedef CUresult (CUDAAPI *PFN_hipHostMalloc)(void **pp, size_t bytesize, unsigned int flags);

typedef struct {
    void **slot;
    const char *symbol;
} DispatchSymbol;

static PFN_hipHostMalloc g_hip_host_malloc;

static CUresult CUDAAPI aimdo_hip_mem_alloc_host(void **pp, size_t bytesize) {
    return g_hip_host_malloc(pp, bytesize, 0);
}

static const DispatchSymbol dispatch_symbols[] = {
    { (void **)&g_cuda.p_cuInit, "hipInit" },
    { (void **)&g_cuda.p_cuGetErrorString, "hipDrvGetErrorString" },
    { (void **)&g_cuda.p_cuCtxGetDevice, "hipGetDevice" },
    { (void **)&g_cuda.p_cuCtxSynchronize, "hipDeviceSynchronize" },
    { (void **)&g_cuda.p_cuDeviceGet, "hipDeviceGet" },
    { (void **)&g_cuda.p_cuDeviceTotalMem, "hipDeviceTotalMem" },
    { (void **)&g_cuda.p_cuDeviceGetName, "hipDeviceGetName" },
    { (void **)&g_cuda.p_cuMemGetInfo, "hipMemGetInfo" },
    { (void **)&g_cuda.p_cuMemAlloc_v2, "hipMalloc" },
    { (void **)&g_cuda.p_cuMemFree_v2, "hipFree" },
    { (void **)&g_cuda.p_cuMemAllocAsync, "hipMallocAsync" },
    { (void **)&g_cuda.p_cuMemFreeAsync, "hipFreeAsync" },
    { (void **)&g_hip_host_malloc, "hipHostMalloc" },
    { (void **)&g_cuda.p_cuMemFreeHost, "hipHostFree" },
    { (void **)&g_cuda.p_cuMemAddressReserve, "hipMemAddressReserve" },
    { (void **)&g_cuda.p_cuMemAddressFree, "hipMemAddressFree" },
    { (void **)&g_cuda.p_cuMemCreate, "hipMemCreate" },
    { (void **)&g_cuda.p_cuMemMap, "hipMemMap" },
    { (void **)&g_cuda.p_cuMemSetAccess, "hipMemSetAccess" },
    { (void **)&g_cuda.p_cuMemUnmap, "hipMemUnmap" },
    { (void **)&g_cuda.p_cuMemRelease, "hipMemRelease" },
};

static void *aimdo_hip_resolve_symbol(const char *symbol) {
#if defined(_WIN32) || defined(_WIN64)
    return (void *)GetProcAddress(g_hip_module, symbol);
#else
    return dlsym(g_hip_module, symbol);
#endif
}

#if !defined(_WIN32) && !defined(_WIN64)
static int aimdo_find_loaded_hip_module(struct dl_phdr_info *info, size_t size, void *data) {
    const char *name = info->dlpi_name;
    const char *basename;

    (void)size;
    if (!name || !*name) {
        return 0;
    }

    basename = strrchr(name, '/');
    basename = basename ? basename + 1 : name;
    if (strcmp(basename, "libamdhip64.so") == 0 || strcmp(basename, "libamdhip64.so.7") == 0 ||
        strcmp(basename, "libamdhip64.so.6") == 0) {
        *(const char **)data = name;
        return 1;
    }

    return 0;
}
#endif

bool aimdo_cuda_runtime_init(void) {
    if (g_cuda.p_cuInit) {
        return true;
    }

#if defined(_WIN32) || defined(_WIN64)
    g_hip_module = GetModuleHandleA("amdhip64.dll");
    if (!g_hip_module) {
        g_hip_module = GetModuleHandleA("amdhip64_7.dll");
    }
    if (!g_hip_module) {
        log(ERROR, "%s: failed to find an already-loaded HIP runtime library\n", __func__);
        return false;
    }
#else
    const char *loaded_hip_module = NULL;

    dl_iterate_phdr(aimdo_find_loaded_hip_module, &loaded_hip_module);
    if (loaded_hip_module) {
        g_hip_module = dlopen(loaded_hip_module, RTLD_LAZY | RTLD_LOCAL | RTLD_NOLOAD);
    }
    if (!g_hip_module) {
        g_hip_module = dlopen("libamdhip64.so.7", RTLD_LAZY | RTLD_LOCAL | RTLD_NOLOAD);
    }
    if (!g_hip_module) {
        g_hip_module = dlopen("libamdhip64.so.6", RTLD_LAZY | RTLD_LOCAL | RTLD_NOLOAD);
    }
    if (!g_hip_module) {
        g_hip_module = dlopen("libamdhip64.so", RTLD_LAZY | RTLD_LOCAL | RTLD_NOLOAD);
    }
    if (!g_hip_module) {
        log(ERROR, "%s: failed to find an already-loaded libamdhip64.so\n", __func__);
        return false;
    }
#endif

    for (size_t i = 0; i < sizeof(dispatch_symbols) / sizeof(dispatch_symbols[0]); i++) {
        void *resolved = aimdo_hip_resolve_symbol(dispatch_symbols[i].symbol);

        if (!resolved) {
            log(ERROR, "%s: failed to resolve required HIP symbol %s\n", __func__,
                dispatch_symbols[i].symbol);
            aimdo_cuda_runtime_cleanup();
            return false;
        }
        *dispatch_symbols[i].slot = resolved;
    }

    g_cuda.p_cuMemAllocHost = aimdo_hip_mem_alloc_host;
    g_cuda.p_cuMemAllocAsync_ptsz = g_cuda.p_cuMemAllocAsync;
    g_cuda.p_cuMemFreeAsync_ptsz = g_cuda.p_cuMemFreeAsync;

    {
        CUresult err = g_cuda.p_cuInit(0);

        if (err != CUDA_SUCCESS) {
            const char *desc = NULL;

            if (g_cuda.p_cuGetErrorString) {
                g_cuda.p_cuGetErrorString(err, &desc);
            }
            log(ERROR, "%s: hipInit failed with code %d%s%s\n", __func__, (int)err,
                desc ? ": " : "", desc ? desc : "");
            aimdo_cuda_runtime_cleanup();
            return false;
        }
    }

    return true;
}

void aimdo_cuda_runtime_cleanup(void) {
    memset(&g_cuda, 0, sizeof(g_cuda));
    g_hip_host_malloc = NULL;

    if (!g_hip_module) {
        return;
    }

#if !defined(_WIN32) && !defined(_WIN64)
    dlclose(g_hip_module);
#endif
    g_hip_module = NULL;
}
