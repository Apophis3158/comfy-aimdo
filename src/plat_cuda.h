#include <cuda.h>

/* NOTE: cuda_runtime.h is banned here. Always use the driver APIs.
 * Add duck-types here.
 */

typedef int cudaError_t;
typedef struct CUstream_st *cudaStream_t;

static inline bool aimdo_setup_hooks() { return true; }
static inline void aimdo_teardown_hooks() {}
