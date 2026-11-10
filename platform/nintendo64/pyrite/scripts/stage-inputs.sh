#!/bin/sh
set -eu

if [ "$#" -lt 3 ] || [ "$#" -gt 4 ]; then
    echo "usage: $0 <package-root> <grandleon-repo> <work-dir> [project.json]" >&2
    exit 2
fi

package_root=$1
repo_root=$2
work_dir=$3
project_source=${4:-"$repo_root/games/steel_and_cedar/source/project.json"}

test -f "$repo_root/CMakeLists.txt"
test -f "$project_source"
test -x "$repo_root/platform/console_art/generate-character-art.py"

# The project is a generated stage, not user source. Recreate it so a renamed
# or removed runtime file cannot survive an incremental export and be linked
# alongside its replacement.
case "$work_dir" in
    ""|"/")
        echo "Nintendo 64 Pyrite export refused: unsafe work directory" >&2
        exit 2
        ;;
esac
rm -rf -- "$work_dir/project"
mkdir -p "$work_dir/project" "$work_dir/host-build" "$work_dir/generated-art"
cp -a "$package_root/project/." "$work_dir/project/"

cmake -S "$repo_root" -B "$work_dir/host-build" \
    -DGRANDLEON_BUILD_TESTS=OFF \
    -DGRANDLEON_CCACHE=OFF
cmake --build "$work_dir/host-build" --target grandleon_content_compile --parallel

mkdir -p "$work_dir/project/assets"
"$work_dir/host-build/grandleon_content_compile" \
    "$project_source" \
    "$work_dir/project/assets/game.gpk"

python3 "$repo_root/platform/console_art/generate-character-art.py" \
    --project "$project_source" \
    --platform n64 \
    --output "$work_dir/generated-art"
case "${GRANDLEON_N64_REVIEW_DRIVER:-0}" in
    0) set -- ;;
    1) set -- --review-driver ;;
    *) echo "GRANDLEON_N64_REVIEW_DRIVER must be 0 or 1" >&2; exit 2 ;;
esac
python3 "$package_root/scripts/prepare-project.py" "$@" \
    --project "$project_source" \
    --art-manifest "$work_dir/generated-art/manifest.json" \
    --project-root "$work_dir/project"
python3 "$package_root/scripts/generate-soft-shadow.py" \
    "$work_dir/project/assets/soft_shadow.png"
python3 "$package_root/scripts/generate-audio.py" \
    "$work_dir/project/assets"
install -m 0644 \
    "$package_root/project/asset-config/soft_shadow.png.conf" \
    "$work_dir/project/assets/soft_shadow.png.conf"

(
    cd "$work_dir/project/assets"
    find . -type f -print | LC_ALL=C sort | xargs sha256sum
) > "$work_dir/input-manifest.sha256"
printf '%s\n' "$project_source" > "$work_dir/project-source.txt"
