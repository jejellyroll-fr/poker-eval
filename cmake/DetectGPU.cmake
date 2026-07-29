include(CheckCSourceRuns)

function(detect_gpu_hardware out_var)
    set(_gpu_found FALSE)

    find_package(OpenCL QUIET)
    if(OpenCL_FOUND)
        set(_saved_includes "${CMAKE_REQUIRED_INCLUDES}")
        set(_saved_libs "${CMAKE_REQUIRED_LIBRARIES}")
        set(CMAKE_REQUIRED_INCLUDES ${OpenCL_INCLUDE_DIRS})
        set(CMAKE_REQUIRED_LIBRARIES ${OpenCL_LIBRARIES})

        set(_opencl_src [[
#include <CL/cl.h>
int main(void)
{
    cl_uint platform_count = 0;
    cl_int err = clGetPlatformIDs(0, NULL, &platform_count);
    if (err != CL_SUCCESS || platform_count == 0)
        return 1;

    cl_platform_id platforms[8];
    if (platform_count > 8)
        platform_count = 8;

    err = clGetPlatformIDs(platform_count, platforms, NULL);
    if (err != CL_SUCCESS)
        return 1;

    for (cl_uint i = 0; i < platform_count; ++i) {
        cl_uint device_count = 0;
        err = clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_GPU, 0, NULL, &device_count);
        if (err == CL_SUCCESS && device_count > 0)
            return 0;
    }

    return 1;
}
        ]])

        check_c_source_runs("${_opencl_src}" PE_OPENCL_GPU_AVAILABLE)

        set(CMAKE_REQUIRED_INCLUDES "${_saved_includes}")
        set(CMAKE_REQUIRED_LIBRARIES "${_saved_libs}")

        if(PE_OPENCL_GPU_AVAILABLE)
            set(_gpu_found TRUE)
        endif()
    endif()

    set(${out_var} ${_gpu_found} PARENT_SCOPE)
endfunction()
