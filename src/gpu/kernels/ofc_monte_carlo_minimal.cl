/*
 * ofc_monte_carlo_minimal.cl - Minimal OpenCL test kernel
 *
 * Extremely simple kernel to test Intel GPU compatibility
 */

__kernel void ofc_monte_carlo_simple(
    __global float* input,
    __global float* output,
    const int size
) {
    int gid = get_global_id(0);

    if (gid >= size) return;

    // Simple test operation
    output[gid] = input[gid] * 2.0f;
}