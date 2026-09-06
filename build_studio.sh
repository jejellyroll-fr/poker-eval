#!/usr/bin/env bash
# Build script for poker-eval Studio native GUI application

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
STUDIO_BUILD_DIR="${SCRIPT_DIR}/build-studio"
NAPPGUI_SRC_DIR="${STUDIO_BUILD_DIR}/nappgui_src"

echo "=== Building poker-eval Studio Native UI ==="

# 1. Clone NAppGUI SDK if missing
if [ ! -d "${NAPPGUI_SRC_DIR}" ]; then
    echo "--> Cloning NAppGUI SDK into ${NAPPGUI_SRC_DIR}..."
    mkdir -p "${STUDIO_BUILD_DIR}"
    git clone --depth 1 https://github.com/frang75/nappgui_src.git "${NAPPGUI_SRC_DIR}"
else
    echo "--> NAppGUI SDK found at ${NAPPGUI_SRC_DIR}"
fi

# 1b. Harden vendored bproc wait paths (idempotent): a process killed by
# a signal must report 128+signo, not 0. Without this, a force-killed
# solve shows "exit_code=0" and looks like a clean exit.
BPROC_UNIX="${NAPPGUI_SRC_DIR}/src/osbs/unix/bproc.c"
if [ -f "${BPROC_UNIX}" ] && ! grep -q "i_exit_code" "${BPROC_UNIX}"; then
    echo "--> Patching vendored bproc.c (signal-aware exit codes)..."
    python3 - "${BPROC_UNIX}" <<'PYEOF'
import sys
path = sys.argv[1]
src = open(path).read()
helper = """/* Maps a waitpid status to a Studio exit code.  A process killed by a
 * signal must NOT report 0 (WEXITSTATUS on a signal status yields 0 on
 * BSD/macOS, which made force-killed solves look like clean exits). */
static uint32_t i_exit_code(int status)
{
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status))
        return (uint32_t)(128 + WTERMSIG(status));
    return UINT32_MAX;
}

"""
anchor = "uint32_t bproc_wait(Proc *proc)"
assert anchor in src and "i_exit_code" not in src
# NOTE: replacements run BEFORE the helper is inserted, so the helper's
# own "return WEXITSTATUS(status);" is never rewritten (else infinite
# recursion).  bproc_wait holds the only such return; both ptr_assign
# sites map waitpid statuses.
src = src.replace("return WEXITSTATUS(status);", "return i_exit_code(status);", 1)
src = src.replace("ptr_assign(code, (uint32_t)WEXITSTATUS(status));",
                  "ptr_assign(code, i_exit_code(status));")
src = src.replace(anchor, helper + anchor, 1)
open(path, "w").write(src)
print("bproc.c patched.")
PYEOF
else
    echo "--> bproc.c signal patch already applied (or file absent)."
fi

# 2. Configure CMake
echo "--> Configuring CMake build in ${STUDIO_BUILD_DIR}..."
# Forward OpenMP hints if the user provided them (the root CMakeLists now
# auto-detects Homebrew/MacPorts libomp on macOS, but explicit overrides win).
CMAKE_EXTRA_ARGS=()
if [ -n "${OPENMP_ROOT:-}" ]; then
    CMAKE_EXTRA_ARGS+=("-DOpenMP_ROOT=${OPENMP_ROOT}")
fi
cmake -S "${SCRIPT_DIR}" -B "${STUDIO_BUILD_DIR}" \
    -DPE_NAPPGUI_ROOT="${NAPPGUI_SRC_DIR}" \
    -DBUILD_TESTS=OFF \
    -DBUILD_EXAMPLES=OFF \
    -DBUILD_BINDINGS=OFF \
    -DBUILD_GPU=OFF \
    "${CMAKE_EXTRA_ARGS[@]}"

# 2b. Surface the OpenMP decision (auto-detected or forced) so the user
# can see whether the build will use multi-threaded solvers.
OPENMP_LINE=$(grep -E "^\-\-.*OpenMP (found|not available|runtime detected)" \
    "${STUDIO_BUILD_DIR}/CMakeFiles/CMakeOutput.log" 2>/dev/null | tail -3 || true)
if [ -n "${OPENMP_LINE}" ]; then
    echo "==> OpenMP detection:"
    echo "${OPENMP_LINE}" | sed 's/^/    /'
fi

# 3. Build target poker-eval-studio
PARALLEL=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
echo "--> Compiling poker-eval-studiowith ${PARALLEL} jobs..."
cmake --build "${STUDIO_BUILD_DIR}" --target poker-eval-studio -j "${PARALLEL}"

# 4. Verify executable exists
STUDIO_BIN="${STUDIO_BUILD_DIR}/tools/poker-eval-studio"
if [ -x "${STUDIO_BIN}" ]; then
    echo "======================================================="
    echo " SUCCESS: Built poker-eval Studio executable!"
    echo " Executable path: ${STUDIO_BIN}"
    echo " Related solvers built beside it:"
    echo "   - ${STUDIO_BUILD_DIR}/tools/pe-preflop-solve"
    echo "   - ${STUDIO_BUILD_DIR}/tools/pe-vector-sim"
    echo "======================================================="
else
    echo "ERROR: poker-eval-studio binary was not found at ${STUDIO_BIN}"
    exit 1
fi