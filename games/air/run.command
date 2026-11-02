#!/bin/zsh
set -eu
repo_dir="$(cd "$(dirname "$0")/../.." && pwd)"
exec "$repo_dir/build/games/air/Air.app/Contents/MacOS/Air" "$@"
