#include <poker_eval/core/status.h>

const char* pe_error_string(pe_status_t status) {
    switch (status) {
        case PE_STATUS_OK: return "OK";
        case PE_STATUS_INVALID_ARGS: return "Invalid arguments";
        case PE_STATUS_INVALID_STATE: return "Invalid state";
        case PE_STATUS_OUT_OF_MEMORY: return "Out of memory";
        case PE_STATUS_NOT_SUPPORTED: return "Not supported";
        case PE_STATUS_RANGE_ERROR: return "Range error";
        case PE_STATUS_IO_ERROR: return "I/O error";
        case PE_STATUS_INTERNAL_ERROR: return "Internal error";
        default: return "Unknown error";
    }
}

