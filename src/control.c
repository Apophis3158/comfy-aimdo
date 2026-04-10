#include "plat.h"
#include "aimdo-time.h"

uint64_t vram_capacity;
uint64_t total_vram_usage;
uint64_t total_vram_last_check;
ssize_t deficit_sync;
CUcontext aimdo_cuda_ctx;

static uint64_t timestamp_last_check;

SHARED_EXPORT
bool plat_init() {
    log_reset_shots();
    return aimdo_setup_hooks();
}

SHARED_EXPORT
void plat_cleanup() {
    aimdo_teardown_hooks();
}

bool cuda_budget_deficit(const char **prevailing_deficit_method) {
    uint64_t now = GET_TICK();
    size_t free_vram = 0;
    size_t total_vram = 0;

    if (now - timestamp_last_check < 2000) {
        return true;
    }
    timestamp_last_check = now;
    total_vram_last_check = total_vram_usage;
    if (!CHECK_CU(cuMemGetInfo(&free_vram, &total_vram))) {
        return false;
    }
    deficit_sync = (ssize_t)VRAM_HEADROOM - (ssize_t)free_vram;
    *prevailing_deficit_method = "cuMemGetInfo";
    return true;
}

SHARED_EXPORT
void aimdo_analyze() {
    size_t free_bytes = 0, total_bytes = 0;

    log(DEBUG, "--- VRAM Stats ---\n");

    CHECK_CU(cuMemGetInfo(&free_bytes, &total_bytes));
    log(DEBUG, "  Aimdo Recorded Usage:  %7zu MB\n", total_vram_usage / M);
    log(DEBUG, "  Cuda:  %7zu MB / %7zu MB Free\n", free_bytes / M, total_bytes / M);

    vbars_analyze(true);
    allocations_analyze();
}

SHARED_EXPORT
uint64_t get_total_vram_usage() {
    return total_vram_usage;
}

SHARED_EXPORT
bool init(int cuda_device_id) {
    CUdevice dev;
    char dev_name[256];

    if (!CHECK_CU(cuDeviceGet(&dev, cuda_device_id)) ||
        !CHECK_CU(cuDeviceTotalMem(&vram_capacity, dev)) ||
        !CHECK_CU(cuDevicePrimaryCtxRetain(&aimdo_cuda_ctx, dev)) ||
        !CHECK_CU(cuCtxSetCurrent(aimdo_cuda_ctx)) ||
        !allocations_init()) {
        return false;
    }
    if (!aimdo_wddm_init(dev)) {
        allocations_cleanup();
        return false;
    }

    if (!CHECK_CU(cuDeviceGetName(dev_name, sizeof(dev_name), dev))) {
        sprintf(dev_name, "<unknown>");
    }

    log(INFO, "comfy-aimdo inited for GPU: %s (VRAM: %zu MB)\n",
        dev_name, (size_t)(vram_capacity / (1024 * 1024)));
    return true;
}

SHARED_EXPORT
void cleanup() {
    aimdo_wddm_cleanup();
    allocations_cleanup();
}
