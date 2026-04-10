#include "plat.h"
#include "aimdo-time.h"

_Thread_local AimdoContext *g_devctx;
CUcontext aimdo_cuda_ctx;

static AimdoContext *g_all_devctxs;
static size_t g_all_devctx_count;

SHARED_EXPORT
void *get_devctx(int device_id) {
    for (size_t i = 0; i < g_all_devctx_count; i++) {
        if (g_all_devctxs[i]._device_id == device_id) {
            return &g_all_devctxs[i];
        }
    }

    return NULL;
}

bool set_devctx_for_device(int device_id) {
    for (size_t i = 0; i < g_all_devctx_count; i++) {
        if (g_all_devctxs[i]._device_id == device_id) {
            set_devctx(&g_all_devctxs[i]);
            return true;
        }
    }

    log(ERROR, "%s: no Aimdo context for device %d\n", __func__, device_id);
    set_devctx(NULL);
    return false;
}

bool set_devctx_for_current_cuda_ctx(void) {
    CUcontext cuda_ctx = NULL;

    if (cuCtxGetCurrent(&cuda_ctx) != CUDA_SUCCESS || !cuda_ctx) {
        goto fail;
    }

    for (size_t i = 0; i < g_all_devctx_count; i++) {
        if (g_all_devctxs[i]._cuda_ctx == cuda_ctx) {
            set_devctx(&g_all_devctxs[i]);
            return true;
        }
    }

fail:
    set_devctx(NULL);
    return false;
}

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

    if (now - control_timestamp_last_check < 2000) {
        return true;
    }
    control_timestamp_last_check = now;
    total_vram_last_check = total_vram_usage;
    if (!CHECK_CU(cuMemGetInfo(&free_vram, &total_vram))) {
        return false;
    }
    deficit_sync = (ssize_t)VRAM_HEADROOM - (ssize_t)free_vram;
    *prevailing_deficit_method = "cuMemGetInfo";
    return true;
}

SHARED_EXPORT
void aimdo_analyze(void *devctx) {
    size_t free_bytes = 0, total_bytes = 0;

    set_devctx((AimdoContext *)devctx);

    log(DEBUG, "--- VRAM Stats ---\n");

    CHECK_CU(cuMemGetInfo(&free_bytes, &total_bytes));
    log(DEBUG, "  Aimdo Recorded Usage:  %7zu MB\n", total_vram_usage / M);
    log(DEBUG, "  Cuda:  %7zu MB / %7zu MB Free\n", free_bytes / M, total_bytes / M);

    vbars_analyze(devctx, true);
    allocations_analyze();
}

SHARED_EXPORT
uint64_t get_total_vram_usage(void *devctx) {
    set_devctx((AimdoContext *)devctx);
    return total_vram_usage;
}

SHARED_EXPORT
bool init(const int *cuda_device_ids, size_t num_devices) {
    size_t i;

    if (!cuda_device_ids || !num_devices || g_all_devctxs) {
        return false;
    }
    aimdo_cuda_ctx = NULL;

    if (!(g_all_devctxs = calloc(num_devices, sizeof(*g_all_devctxs)))) {
        return false;
    }
    g_all_devctx_count = num_devices;

    for (i = 0; i < num_devices; i++) {
        CUdevice dev;
        char dev_name[256];
        AimdoContext *devctx = &g_all_devctxs[i];

        devctx->_device_id = cuda_device_ids[i];
        set_devctx(devctx);

        if (!allocations_init()) {
            goto fail;
        }

        if (!CHECK_CU(cuDeviceGet(&dev, cuda_device_ids[i])) ||
            !CHECK_CU(cuDeviceTotalMem(&vram_capacity, dev)) ||
            !CHECK_CU(cuDevicePrimaryCtxRetain(&devctx->_cuda_ctx, dev)) ||
            !CHECK_CU(cuCtxSetCurrent(devctx->_cuda_ctx)) ||
            !aimdo_wddm_init(dev)) {
            goto fail;
        }
        if (i == 0) {
            aimdo_cuda_ctx = devctx->_cuda_ctx;
        }

        if (!CHECK_CU(cuDeviceGetName(dev_name, sizeof(dev_name), dev))) {
            sprintf(dev_name, "<unknown>");
        }

        log(INFO, "comfy-aimdo inited for GPU: %s (VRAM: %zu MB)\n",
            dev_name, (size_t)(vram_capacity / (1024 * 1024)));
    }

    set_devctx(NULL);
    return true;

fail:
    cleanup();
    return false;
}

SHARED_EXPORT
void cleanup(void) {
    for (size_t i = 0; i < g_all_devctx_count; i++) {
        CUdevice dev;

        set_devctx(&g_all_devctxs[i]);
        aimdo_wddm_cleanup();
        allocations_cleanup();

        if (g_all_devctxs[i]._cuda_ctx &&
            CHECK_CU(cuDeviceGet(&dev, g_all_devctxs[i]._device_id))) {
            CHECK_CU(cuDevicePrimaryCtxRelease(dev));
            g_all_devctxs[i]._cuda_ctx = NULL;
        }

        free(highest_priority_p); /* FIXME: move the model_vbar. */
    }

    free(g_all_devctxs);
    g_all_devctxs = NULL;
    g_all_devctx_count = 0;
    aimdo_cuda_ctx = NULL;
    set_devctx(NULL);
}
