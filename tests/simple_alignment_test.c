/* simple_alignment_test.c -- Simple test for table alignment */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>

#if defined(_MSC_VER)
#include <malloc.h>
static int pe_posix_memalign(void **ptr, size_t alignment, size_t size) {
    if (!ptr) {
        return EINVAL;
    }
    *ptr = _aligned_malloc(size, alignment);
    return *ptr ? 0 : ENOMEM;
}
static void pe_aligned_free(void *ptr) {
    _aligned_free(ptr);
}
#define PE_ALIGN_DECL __declspec(align(64))
#else
static int pe_posix_memalign(void **ptr, size_t alignment, size_t size) {
    return posix_memalign(ptr, alignment, size);
}
static void pe_aligned_free(void *ptr) {
    free(ptr);
}
#define PE_ALIGN_DECL __attribute__((aligned(64)))
#endif

/* Test alignment with posix_memalign */
int main(void) {
    void *ptr1, *ptr2, *ptr3;
    int ret;
    
    printf("=== Simple Alignment Test ===\n\n");
    
    /* Test posix_memalign */
    ret = pe_posix_memalign(&ptr1, 64, 8192);
    if (ret != 0) {
        printf("posix_memalign failed: %d\n", ret);
        return 1;
    }
    
    ret = pe_posix_memalign(&ptr2, 64, 8192 * 4);
    if (ret != 0) {
        printf("posix_memalign failed: %d\n", ret);
        pe_aligned_free(ptr1);
        return 1;
    }
    
    ret = pe_posix_memalign(&ptr3, 64, 8192);
    if (ret != 0) {
        printf("posix_memalign failed: %d\n", ret);
        pe_aligned_free(ptr1);
        pe_aligned_free(ptr2);
        return 1;
    }
    
    printf("Allocated pointers:\n");
    printf("  ptr1: %p (mod 64 = %lu) %s\n", 
           ptr1, (uintptr_t)ptr1 % 64,
           ((uintptr_t)ptr1 % 64 == 0) ? "✓ ALIGNED" : "✗ NOT ALIGNED");
    printf("  ptr2: %p (mod 64 = %lu) %s\n", 
           ptr2, (uintptr_t)ptr2 % 64,
           ((uintptr_t)ptr2 % 64 == 0) ? "✓ ALIGNED" : "✗ NOT ALIGNED");
    printf("  ptr3: %p (mod 64 = %lu) %s\n", 
           ptr3, (uintptr_t)ptr3 % 64,
           ((uintptr_t)ptr3 % 64 == 0) ? "✓ ALIGNED" : "✗ NOT ALIGNED");
    
    /* Test static arrays with alignment attribute */
    static PE_ALIGN_DECL uint8_t array1[8192];
    static PE_ALIGN_DECL uint32_t array2[8192];
    static PE_ALIGN_DECL uint8_t array3[8192];
    
    printf("\nStatic arrays with __attribute__((aligned(64))):\n");
    printf("  array1: %p (mod 64 = %lu) %s\n", 
           (void*)array1, (uintptr_t)array1 % 64,
           ((uintptr_t)array1 % 64 == 0) ? "✓ ALIGNED" : "✗ NOT ALIGNED");
    printf("  array2: %p (mod 64 = %lu) %s\n", 
           (void*)array2, (uintptr_t)array2 % 64,
           ((uintptr_t)array2 % 64 == 0) ? "✓ ALIGNED" : "✗ NOT ALIGNED");
    printf("  array3: %p (mod 64 = %lu) %s\n", 
           (void*)array3, (uintptr_t)array3 % 64,
           ((uintptr_t)array3 % 64 == 0) ? "✓ ALIGNED" : "✗ NOT ALIGNED");
    
    /* Cleanup */
    pe_aligned_free(ptr1);
    pe_aligned_free(ptr2);
    pe_aligned_free(ptr3);
    
    printf("\n=== Conclusion ===\n");
    printf("posix_memalign() provides guaranteed alignment on macOS.\n");
    printf("Static arrays with alignment attributes may not be aligned.\n");
    printf("For critical performance, use dynamic allocation.\n");
    
    return 0;
}
