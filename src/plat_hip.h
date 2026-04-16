#include <hip/hip_runtime.h>

#ifdef _WIN32
#define CUDAAPI __stdcall
#else
#define CUDAAPI
#endif

/* ========================================================================
 * CUDA -> HIP Type Mappings
 * ======================================================================== */
#define CUresult                        hipError_t
#define CUdevice                        hipDevice_t
#define CUdeviceptr                     hipDeviceptr_t
#define CUstream                        hipStream_t
#define cudaError_t                     hipError_t
#define cudaStream_t                    hipStream_t

/* ========================================================================
 * CUDA -> HIP Constant Mappings
 * ======================================================================== */
#define CUDA_SUCCESS                    hipSuccess
#define CUDA_ERROR_OUT_OF_MEMORY        hipErrorOutOfMemory
#define CU_MEM_ALLOCATION_TYPE_PINNED   hipMemAllocationTypePinned
#define CU_MEM_LOCATION_TYPE_DEVICE     hipMemLocationTypeDevice
#define CU_MEM_ACCESS_FLAGS_PROT_READWRITE hipMemAccessFlagsProtReadWrite

/* ========================================================================
 * CUDA -> HIP Function Mappings: Device & Context
 * ======================================================================== */
#define cuDeviceGet                     hipDeviceGet
#define cuDeviceGetName                 hipDeviceGetName
#define cuDeviceTotalMem                hipDeviceTotalMem
#define cuCtxGetDevice                  hipCtxGetDevice             // deprecated
#define cuCtxSynchronize                hipDeviceSynchronize

/* ========================================================================
 * CUDA -> HIP Function Mappings: Virtual Memory Management
 * ======================================================================== */
#define cuMemAddressReserve             hipMemAddressReserve
#define cuMemAddressFree                hipMemAddressFree
#define cuMemCreate                     hipMemCreate
#define cuMemRelease                    hipMemRelease
#define cuMemMap                        hipMemMap
#define cuMemUnmap                      hipMemUnmap
#define cuMemSetAccess                  hipMemSetAccess
#define CUmemAccessDesc                 hipMemAccessDesc
#define CUmemAllocationProp             hipMemAllocationProp
#define CUmemGenericAllocationHandle    hipMemGenericAllocationHandle_t
#define cuMemGetInfo                    hipMemGetInfo

/* ========================================================================
 * CUDA -> HIP Function Mappings: Memory Allocation
 * ======================================================================== */
#define cuMemAlloc_v2                   hipMalloc
#define cuMemFree_v2                    hipFree
#define cuMemAllocAsync                 hipMallocAsync
#define cuMemFreeAsync                  hipFreeAsync

/* ========================================================================
 * CUDA -> HIP Function Mappings: Host Memory
 * ======================================================================== */
#define cuMemAllocHost(p, size)         hipHostMalloc((p), (size), hipHostMallocDefault)
#define cuMemFreeHost                   hipHostFree

/* ========================================================================
 * CUDA -> HIP Function Mappings: Error Handling
 * ======================================================================== */
#define cuGetErrorString                hipDrvGetErrorString
