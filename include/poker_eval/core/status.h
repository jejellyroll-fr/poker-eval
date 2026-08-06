#ifndef PE_STATUS_H
#define PE_STATUS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Note: pe_status_t may also be defined (with different members) by
 * <poker_eval/range.h>; the guard below lets the first definition win. */
#ifndef PE_STATUS_T_DEFINED
#define PE_STATUS_T_DEFINED
typedef enum {
    PE_STATUS_OK = 0,
    PE_STATUS_INVALID_ARGS,
    PE_STATUS_INVALID_STATE,
    PE_STATUS_OUT_OF_MEMORY,
    PE_STATUS_NOT_SUPPORTED,
    PE_STATUS_RANGE_ERROR,
    PE_STATUS_IO_ERROR,
    PE_STATUS_INTERNAL_ERROR
} pe_status_t;
#endif

const char* pe_error_string(pe_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* PE_STATUS_H */

