#ifndef PE_STATUS_H
#define PE_STATUS_H

#ifdef __cplusplus
extern "C" {
#endif

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

const char* pe_error_string(pe_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* PE_STATUS_H */

