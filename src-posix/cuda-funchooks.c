#define _GNU_SOURCE

#include "plat.h"

#include <dlfcn.h>
#include <funchook.h>

static funchook_t *funchook_state;

#include "cuda-hooks-shared.h"

bool aimdo_setup_hooks(void) {
    void *h_real_cuda;
    int status;

    h_real_cuda = dlopen("libcuda.so.1", RTLD_LAZY | RTLD_NOLOAD);
    if (!h_real_cuda) {
        log(ERROR, "%s: libcuda.so.1 not found in process memory: %s\n", __func__, dlerror());
        return false;
    }

    funchook_state = funchook_create();
    if (!funchook_state) {
        log(ERROR, "%s: funchook_create failed\n", __func__);
        goto fail_close_cuda;
    }

    for (size_t i = 0; i < sizeof(hooks) / sizeof(hooks[0]); i++) {
        const char *dlerr;
        const char *detail;
        void *target_ptr;

        dlerror();
        target_ptr = dlsym(h_real_cuda, hooks[i].name);
        dlerr = dlerror();
        if (dlerr || !target_ptr) {
            log(ERROR, "%s: failed to resolve %s: %s\n", __func__, hooks[i].name,
                dlerr ? dlerr : "<missing symbol>");
            goto fail_teardown;
        }

        *hooks[i].true_ptr = target_ptr;
        status = funchook_prepare(funchook_state, hooks[i].true_ptr, hooks[i].hook_ptr);
        if (status != FUNCHOOK_ERROR_SUCCESS) {
            detail = funchook_error_message(funchook_state);
            log(ERROR, "%s: funchook_prepare(%s) failed: %d %s\n", __func__, hooks[i].name,
                status, detail ? detail : "<unknown funchook error>");
            goto fail_teardown;
        }
    }

    status = funchook_install(funchook_state, 0);
    if (status != FUNCHOOK_ERROR_SUCCESS) {
        const char *detail = funchook_error_message(funchook_state);

        log(ERROR, "%s: funchook_install failed: %d %s\n", __func__, status,
            detail ? detail : "<unknown funchook error>");
        goto fail_teardown;
    }

    dlclose(h_real_cuda);
    log(DEBUG, "%s: hooks successfully installed\n", __func__);
    return true;

fail_teardown:
    aimdo_teardown_hooks();
fail_close_cuda:
    dlclose(h_real_cuda);
    return false;
}

void aimdo_teardown_hooks(void) {
    int status;

    if (!funchook_state) {
        return;
    }

    if (((status = funchook_uninstall(funchook_state, 0)) != FUNCHOOK_ERROR_SUCCESS &&
         status != FUNCHOOK_ERROR_NOT_INSTALLED) ||
        (status = funchook_destroy(funchook_state)) != FUNCHOOK_ERROR_SUCCESS) {
        const char *detail = funchook_error_message(funchook_state);

        log(ERROR, "%s: funchook teardown failed: %d %s\n", __func__, status,
            detail ? detail : "<unknown funchook error>");
    }

    funchook_state = NULL;
}
