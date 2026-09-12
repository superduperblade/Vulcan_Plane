// VMA 3.x is single-header: this dedicated TU pulls in the implementation
// so the rest of the codebase only sees the API (and static analysis stays
// focused on project code).
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
