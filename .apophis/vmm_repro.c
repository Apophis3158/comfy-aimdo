/*
 * Minimal reproducer for hipMemAddressReserve access violation on Windows+ROCm.
 * Uses HIP driver API loaded dynamically from the venv's _rocm_sdk_core.
 *
 * Simulates real ComfyUI workload:
 *   - Two VBARs (text encoder ~1.5GB, diffusion model ~5GB)
 *   - Model swap: evict one entire VBAR, reserve cast buffer, re-populate
 *   - Interleaved hipMalloc/hipFree (PyTorch allocator activity)
 *
 * Build:
 *   cl /Zi /Fe:vmm_repro.exe .\vmm_repro.c /link
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <windows.h>

typedef int CUresult;
typedef unsigned long long CUdeviceptr;
typedef void *CUmemGenericAllocationHandle;

#define CU_MEM_ALLOCATION_TYPE_PINNED 1
#define CU_MEM_LOCATION_TYPE_DEVICE   1
#define CU_MEM_ACCESS_FLAGS_PROT_READWRITE 3

typedef struct CUmemAllocationProp_st {
    int type;          int pad0[1];
    int locationType;  int locationId;  int pad1[2];
    unsigned long long requestedHandleTypes;
    int pad2[14];
} CUmemAllocationProp;

typedef struct CUmemAccessDesc_st {
    int locationType;  int locationId;
    unsigned long long flags;
    int pad[4];
} CUmemAccessDesc;

typedef CUresult (*PFN_hipInit)(unsigned int);
typedef CUresult (*PFN_hipMemGetInfo)(size_t *, size_t *);
typedef CUresult (*PFN_hipMalloc)(CUdeviceptr *, size_t);
typedef CUresult (*PFN_hipFree)(CUdeviceptr);
typedef CUresult (*PFN_hipMemAddressReserve)(CUdeviceptr *, size_t, size_t, CUdeviceptr, unsigned long long);
typedef CUresult (*PFN_hipMemAddressFree)(CUdeviceptr, size_t);
typedef CUresult (*PFN_hipMemCreate)(CUmemGenericAllocationHandle *, size_t, CUmemAllocationProp *, unsigned long long);
typedef CUresult (*PFN_hipMemMap)(CUdeviceptr, size_t, size_t, CUmemGenericAllocationHandle, unsigned long long);
typedef CUresult (*PFN_hipMemSetAccess)(CUdeviceptr, size_t, CUmemAccessDesc *, size_t);
typedef CUresult (*PFN_hipMemUnmap)(CUdeviceptr, size_t);
typedef CUresult (*PFN_hipMemRelease)(CUmemGenericAllocationHandle);

static PFN_hipInit              hipInit;
static PFN_hipMemGetInfo        hipMemGetInfo;
static PFN_hipMalloc            hipMalloc;
static PFN_hipFree              hipFree;
static PFN_hipMemAddressReserve hipMemAddressReserve;
static PFN_hipMemAddressFree    hipMemAddressFree;
static PFN_hipMemCreate         hipMemCreate;
static PFN_hipMemMap            hipMemMap;
static PFN_hipMemSetAccess      hipMemSetAccess;
static PFN_hipMemUnmap          hipMemUnmap;
static PFN_hipMemRelease        hipMemRelease;

#define CHECK(x) do { CUresult _r = (x); if (_r != 0) { fprintf(stderr, "  FAIL %s: %d\n", #x, _r); return 1; } } while(0)

#define MB (1024 * 1024)
#define GB (1024 * 1024 * 1024)
static void print_mem(void) {
    size_t free_vram = 0, total_vram = 0;
    hipMemGetInfo(&free_vram, &total_vram);
    fprintf(stderr, "  VRAM: free=%zuMB total=%zuMB\n",
            free_vram / MB, total_vram / MB);
}

typedef struct {
    CUdeviceptr va;
    size_t size;
    size_t page_size;
    size_t nr_pages;
    size_t populated;
    CUmemGenericAllocationHandle *handles;
} VBAR;

static int vbar_create(VBAR *v, size_t total_size, size_t page_size,
                        CUmemAllocationProp *prop, CUmemAccessDesc *access) {
    v->page_size = page_size;
    v->size = total_size;
    v->nr_pages = total_size / page_size;
    v->populated = 0;
    v->handles = calloc(v->nr_pages, sizeof(CUmemGenericAllocationHandle));
    CHECK(hipMemAddressReserve(&v->va, total_size, 2 * MB, 0, 0));
    fprintf(stderr, "  VBAR VA=0x%llx size=%zuMB\n", (unsigned long long)v->va, total_size / MB);
    return 0;
}

static int vbar_populate(VBAR *v, size_t nr_pages,
                          CUmemAllocationProp *prop, CUmemAccessDesc *access) {
    for (size_t i = 0; i < nr_pages && v->populated < v->nr_pages; i++) {
        size_t idx = v->populated;
        CUmemGenericAllocationHandle h = NULL;
        CUresult err = hipMemCreate(&h, v->page_size, prop, 0);
        if (err != 0) { fprintf(stderr, "  vbar_populate: hipMemCreate failed: %d\n", err); break; }
        err = hipMemMap(v->va + idx * v->page_size, v->page_size, 0, h, 0);
        if (err != 0) { hipMemRelease(h); fprintf(stderr, "  vbar_populate: hipMemMap failed: %d\n", err); break; }
        err = hipMemSetAccess(v->va + idx * v->page_size, v->page_size, access, 1);
        if (err != 0) { fprintf(stderr, "  vbar_populate: hipMemSetAccess failed: %d\n", err); break; }
        v->handles[idx] = h;
        v->populated++;
    }
    return 0;
}

static int vbar_evict(VBAR *v, size_t nr_pages) {
    for (size_t i = 0; i < nr_pages && v->populated > 0; i++) {
        size_t idx = v->populated - 1;
        hipMemUnmap(v->va + idx * v->page_size, v->page_size);
        hipMemRelease(v->handles[idx]);
        v->handles[idx] = NULL;
        v->populated--;
    }
    return 0;
}

static void vbar_destroy(VBAR *v) {
    vbar_evict(v, v->populated);
    hipMemAddressFree(v->va, v->size);
    free(v->handles);
}

#define DLL_DIR "H:\\ROCm\\.venv\\Lib\\site-packages\\_rocm_sdk_core\\bin"
int main(int argc, char **argv) {
    int iterations = (argc > 1) ? atoi(argv[1]) : 20;

    /* Add venv bin to PATH so amdhip64_7.dll dependencies resolve */
    const char *dll_dir = DLL_DIR;
    char cur_path[32768];
    GetEnvironmentVariableA("PATH", cur_path, sizeof(cur_path));
    char new_path[65536];
    snprintf(new_path, sizeof(new_path), "%s;%s", dll_dir, cur_path);
    SetEnvironmentVariableA("PATH", new_path);

    /* Load the SAME amdhip64_7.dll that ComfyUI uses */
    const char *hip_path = DLL_DIR "\\amdhip64_7.dll";
    HMODULE hip = LoadLibraryA(hip_path);
    if (!hip) {
        fprintf(stderr, "Cannot load %s: %lu\n", hip_path, GetLastError());
        /* Fallback to system */
        hip = LoadLibraryA("amdhip64_7.dll");
    }
    if (!hip) { fprintf(stderr, "Cannot load any amdhip64_7.dll\n"); return 1; }

    char hip_real_path[MAX_PATH];
    DWORD hip_path_len = GetModuleFileNameA(hip, hip_real_path, MAX_PATH);
    if (hip_path_len)
        fprintf(stderr, "Loaded amdhip64_7.dll: %s\n", hip_real_path);

    #define LOAD(name) name = (PFN_##name) GetProcAddress(hip, #name)
    LOAD(hipInit);
    LOAD(hipMemGetInfo);
    LOAD(hipMalloc);
    LOAD(hipFree);
    LOAD(hipMemAddressReserve);
    LOAD(hipMemAddressFree);
    LOAD(hipMemCreate);
    LOAD(hipMemMap);
    LOAD(hipMemSetAccess);
    LOAD(hipMemUnmap);
    LOAD(hipMemRelease);
    #undef LOAD

    CHECK(hipInit(0));

    fprintf(stderr, "=== VMM Reproducer (realistic workload) ===\n");
    fprintf(stderr, "Iterations: %d\n", iterations);
    print_mem();

    CUmemAllocationProp prop;
    memset(&prop, 0, sizeof(prop));
    prop.type = CU_MEM_ALLOCATION_TYPE_PINNED;
    prop.locationType = CU_MEM_LOCATION_TYPE_DEVICE;
    prop.locationId = 0;

    CUmemAccessDesc accessDesc;
    memset(&accessDesc, 0, sizeof(accessDesc));
    accessDesc.locationType = CU_MEM_LOCATION_TYPE_DEVICE;
    accessDesc.locationId = 0;
    accessDesc.flags = CU_MEM_ACCESS_FLAGS_PROT_READWRITE;

    size_t page_size = 32 * MB;
    size_t cast_buffer_size = 16ULL * GB;

    /* Create two VBARs like ComfyUI's text encoder and diffusion model */
    VBAR vbar_text, vbar_diff;
    fprintf(stderr, "\n--- Creating VBARs ---\n");
    if (vbar_create(&vbar_text, 2ULL * GB, page_size, &prop, &accessDesc) != 0) return 1;
    if (vbar_create(&vbar_diff, 6ULL * GB, page_size, &prop, &accessDesc) != 0) return 1;

    /* Populate both VBARs */
    fprintf(stderr, "\n--- Populating VBARs ---\n");
    vbar_populate(&vbar_text, vbar_text.nr_pages, &prop, &accessDesc);
    vbar_populate(&vbar_diff, vbar_diff.nr_pages, &prop, &accessDesc);
    fprintf(stderr, "Text VBAR: %zu/%zu pages (%zuMB)\n",
            vbar_text.populated, vbar_text.nr_pages, vbar_text.populated * page_size / MB);
    fprintf(stderr, "Diff VBAR: %zu/%zu pages (%zuMB)\n",
            vbar_diff.populated, vbar_diff.nr_pages, vbar_diff.populated * page_size / MB);
    print_mem();

    /* Allocate some scratch buffers via hipMalloc (like PyTorch allocator) */
    #define NR_SCRATCH 8
    CUdeviceptr scratch[NR_SCRATCH] = {0};
    size_t scratch_size = 256 * MB;
    int nr_scratch_allocd = 0;
    for (int i = 0; i < NR_SCRATCH; i++) {
        if (hipMalloc(&scratch[i], scratch_size) == 0)
            nr_scratch_allocd++;
        else
            break;
    }
    fprintf(stderr, "Scratch buffers: %d x %zuMB\n", nr_scratch_allocd, scratch_size/MB);
    print_mem();

    /* Main loop: simulate model swap */
    for (int iter = 0; iter < iterations; iter++) {
        fprintf(stderr, "\n=== Iteration %d ===\n", iter + 1);

        /* Free some scratch buffers (PyTorch freeing intermediate tensors) */
        int to_free = nr_scratch_allocd > 2 ? 2 : nr_scratch_allocd;
        for (int i = 0; i < to_free; i++) {
            if (scratch[nr_scratch_allocd - 1]) {
                hipFree(scratch[nr_scratch_allocd - 1]);
                scratch[nr_scratch_allocd - 1] = 0;
                nr_scratch_allocd--;
            }
        }

        /* Evict text encoder VBAR (model swap: text out, diff stays) */
        fprintf(stderr, "Evicting text VBAR (%zu pages)...\n", vbar_text.populated);
        vbar_evict(&vbar_text, vbar_text.populated);
        print_mem();

        /* THIS IS THE CRASH POINT: reserve cast buffer */
        fprintf(stderr, ">>> hipMemAddressReserve for cast buffer <<<\n");
        CUdeviceptr cast_va = 0;
        CUresult reserve_err = hipMemAddressReserve(&cast_va, cast_buffer_size, 2*MB, 0, 0);
        if (reserve_err != 0) {
            fprintf(stderr, "hipMemAddressReserve returned error: %d\n", reserve_err);
            /* Try to continue */
        } else {
            fprintf(stderr, "Cast buffer VA=0x%llx\n", (unsigned long long)cast_va);

            /* Map some physical memory into cast buffer (like weight casting) */
            CUmemGenericAllocationHandle cast_handle = NULL;
            size_t cast_map_size = 512 * MB;
            CUresult err = hipMemCreate(&cast_handle, cast_map_size, &prop, 0);
            if (err == 0) {
                err = hipMemMap(cast_va, cast_map_size, 0, cast_handle, 0);
                if (err == 0) hipMemSetAccess(cast_va, cast_map_size, &accessDesc, 1);
                hipMemUnmap(cast_va, cast_map_size);
                hipMemRelease(cast_handle);
            }

            hipMemAddressFree(cast_va, cast_buffer_size);
            fprintf(stderr, "Cast buffer freed\n");
        }

        /* Re-populate text VBAR (model swap: text back in) */
        fprintf(stderr, "Re-populating text VBAR...\n");
        vbar_populate(&vbar_text, vbar_text.nr_pages, &prop, &accessDesc);
        fprintf(stderr, "Text VBAR: %zu/%zu pages\n", vbar_text.populated, vbar_text.nr_pages);

        /* Re-alloc scratch buffers */
        while (nr_scratch_allocd < NR_SCRATCH) {
            if (hipMalloc(&scratch[nr_scratch_allocd], scratch_size) != 0) break;
            nr_scratch_allocd++;
        }

        print_mem();

        /* Now evict diffusion model (alternate swap direction) */
        if (iter % 2 == 1) {
            fprintf(stderr, "Evicting diff VBAR (%zu pages)...\n", vbar_diff.populated);
            vbar_evict(&vbar_diff, vbar_diff.populated);
            print_mem();

            fprintf(stderr, ">>> hipMemAddressReserve for cast buffer <<<\n");
            cast_va = 0;
            reserve_err = hipMemAddressReserve(&cast_va, cast_buffer_size, 2*MB, 0, 0);
            if (reserve_err != 0) {
                fprintf(stderr, "hipMemAddressReserve returned error: %d\n", reserve_err);
            } else {
                fprintf(stderr, "Cast buffer VA=0x%llx\n", (unsigned long long)cast_va);
                hipMemAddressFree(cast_va, cast_buffer_size);
                fprintf(stderr, "Cast buffer freed\n");
            }

            fprintf(stderr, "Re-populating diff VBAR...\n");
            vbar_populate(&vbar_diff, vbar_diff.nr_pages, &prop, &accessDesc);
            fprintf(stderr, "Diff VBAR: %zu/%zu pages\n", vbar_diff.populated, vbar_diff.nr_pages);
            print_mem();
        }
    }

    fprintf(stderr, "\n--- Cleanup ---\n");
    for (int i = 0; i < nr_scratch_allocd; i++) {
        if (scratch[i]) hipFree(scratch[i]);
    }
    vbar_destroy(&vbar_text);
    vbar_destroy(&vbar_diff);
    return 0;
}
