#!/bin/sh

set -eu

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

if [ "$#" -lt 1 ]; then
    echo "Usage: $0 NAME [REPORT]" >&2
    echo "Example: $0 managed-memory" >&2
    exit 1
fi

NAME="$1"
REPORT="${2:-$REPO_ROOT/build/cuda-profile/report.nsys-rep}"

case "$NAME" in
    ""|*[!A-Za-z0-9._-]*)
        echo "Error: NAME may contain only letters, numbers, '.', '_' and '-'." >&2
        exit 1
        ;;
esac

if ! command -v nsys >/dev/null 2>&1; then
    echo "Error: nsys was not found." >&2
    exit 1
fi

if [ ! -f "$REPORT" ]; then
    echo "Error: report not found: $REPORT" >&2
    exit 1
fi

TIMESTAMP="$(date -u '+%Y%m%dT%H%M%SZ')"
RESULTS_DIR="$REPO_ROOT/benchmark-results"
OUTPUT="$RESULTS_DIR/cuda_report-$TIMESTAMP-$NAME.md"

mkdir -p "$RESULTS_DIR"

# Remove an old SQLite export so it cannot contain data from an older report.
SQLITE="${REPORT%.nsys-rep}.sqlite"
rm -f "$SQLITE"

GIT_COMMIT="$(git -C "$REPO_ROOT" rev-parse HEAD)"
GIT_SHORT_COMMIT="$(git -C "$REPO_ROOT" rev-parse --short HEAD)"
GIT_BRANCH="$(git -C "$REPO_ROOT" rev-parse --abbrev-ref HEAD)"
GIT_SUBJECT="$(git -C "$REPO_ROOT" show -s --format='%s' HEAD)"
GIT_AUTHOR="$(git -C "$REPO_ROOT" show -s --format='%an' HEAD)"
GIT_COMMIT_DATE="$(git -C "$REPO_ROOT" show -s --format='%cI' HEAD)"

if [ -n "$(git -C "$REPO_ROOT" status --porcelain)" ]; then
    GIT_STATUS="dirty"
else
    GIT_STATUS="clean"
fi

HOST_NAME="$(hostname)"
OPERATING_SYSTEM="$(uname -srmo)"

if command -v nvidia-smi >/dev/null 2>&1; then
    GPU_INFO="$(
        nvidia-smi \
            --query-gpu=name,driver_version,memory.total,compute_cap \
            --format=csv,noheader |
            head -n 1
    )"
else
    GPU_INFO="Unavailable"
fi

if command -v nvcc >/dev/null 2>&1; then
    CUDA_VERSION="$(nvcc --version | tail -n 1)"
else
    CUDA_VERSION="Unavailable"
fi

NSYS_VERSION="$(nsys --version | head -n 1)"

CMAKE_CACHE="$REPO_ROOT/build/cuda-profile/CMakeCache.txt"

if [ -f "$CMAKE_CACHE" ]; then
    BUILD_TYPE="$(
        sed -n 's/^CMAKE_BUILD_TYPE:STRING=//p' "$CMAKE_CACHE" |
            head -n 1
    )"
    CUDA_ARCHITECTURES="$(
        sed -n 's/^CMAKE_CUDA_ARCHITECTURES:UNINITIALIZED=//p' "$CMAKE_CACHE" |
            head -n 1
    )"

    BUILD_TYPE="${BUILD_TYPE:-Unknown}"
    CUDA_ARCHITECTURES="${CUDA_ARCHITECTURES:-Unknown}"
else
    BUILD_TYPE="Unknown"
    CUDA_ARCHITECTURES="Unknown"
fi

section() {
    title="$1"
    report_name="$2"

    echo "## $title"
    echo
    echo '```text'

    # Remove Nsight's SQLite and processing notices from the Markdown output.
    nsys stats --report "$report_name" "$REPORT" |
        sed -n '/^[[:space:]]*\*\* /,$p'

    echo '```'
    echo
}

{
    echo "# NVIDIA Nsight Systems Report"
    echo
    echo "## Report Metadata"
    echo
    echo "- Name: \`$NAME\`"
    echo "- Generated: \`$TIMESTAMP\`"
    echo "- Source report: \`$REPORT\`"
    echo "- Build type: \`$BUILD_TYPE\`"
    echo "- CUDA architectures: \`$CUDA_ARCHITECTURES\`"
    echo
    echo "## Git Metadata"
    echo
    echo "- Branch: \`$GIT_BRANCH\`"
    echo "- Commit: \`$GIT_COMMIT\`"
    echo "- Short commit: \`$GIT_SHORT_COMMIT\`"
    echo "- Commit subject: $GIT_SUBJECT"
    echo "- Commit author: $GIT_AUTHOR"
    echo "- Commit date: \`$GIT_COMMIT_DATE\`"
    echo "- Working tree: \`$GIT_STATUS\`"
    echo
    echo "## System Metadata"
    echo
    echo "- Host: \`$HOST_NAME\`"
    echo "- Operating system: \`$OPERATING_SYSTEM\`"
    echo "- GPU: \`$GPU_INFO\`"
    echo "- CUDA compiler: \`$CUDA_VERSION\`"
    echo "- Nsight Systems: \`$NSYS_VERSION\`"
    echo

    section "GPU Kernels" "cuda_gpu_kern_sum"
    section "GPU Memory Transfer Time" "cuda_gpu_mem_time_sum"
    section "GPU Memory Transfer Size" "cuda_gpu_mem_size_sum"
    section "CUDA API Calls" "cuda_api_sum"
} > "$OUTPUT"

echo "Generated: $OUTPUT"
