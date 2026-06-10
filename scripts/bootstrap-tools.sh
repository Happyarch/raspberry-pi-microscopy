#!/usr/bin/env bash
# Populate tools/bin/ with project-local dev tools (not installed system-wide).
# Re-run this after cloning or when tools/bin/ is missing.
#
# Currently installs:
#   dpkg-deb  — used by build-deb.sh to assemble .deb packages
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TOOLS_BIN="$REPO_ROOT/tools/bin"
mkdir -p "$TOOLS_BIN"

install_dpkg_deb() {
    local dest="$TOOLS_BIN/dpkg-deb"
    if [[ -x "$dest" ]]; then
        echo "==> dpkg-deb already present ($("$dest" --version | head -1))"
        return
    fi

    echo "==> Installing dpkg-deb to tools/bin/ ..."

    # --- Arch Linux: extract from pacman cache or download ---
    if command -v pacman >/dev/null 2>&1; then
        # Fetch package URL (uses cache if already downloaded)
        local pkg_url
        pkg_url=$(pacman -Sp dpkg 2>/dev/null)

        local pkg_file
        # If the URL is a local file:// path the package is already cached
        if [[ "$pkg_url" == file://* ]]; then
            pkg_file="${pkg_url#file://}"
        else
            pkg_file=$(mktemp --suffix=.pkg.tar.zst)
            trap 'rm -f "$pkg_file"' RETURN
            curl -fsSL "$pkg_url" -o "$pkg_file"
        fi

        # Extract just the dpkg-deb binary
        tar -xf "$pkg_file" --to-stdout usr/bin/dpkg-deb > "$dest"
        chmod 755 "$dest"
        echo "==> dpkg-deb installed from Arch package."
        return
    fi

    # --- Debian/Ubuntu: copy from system (already present) ---
    if command -v dpkg-deb >/dev/null 2>&1; then
        cp "$(command -v dpkg-deb)" "$dest"
        echo "==> dpkg-deb copied from system path."
        return
    fi

    echo "ERROR: Could not locate or download dpkg-deb."
    echo "  Arch:   ensure pacman is available (it will download dpkg for you)"
    echo "  Debian: dpkg-deb should already be on PATH"
    exit 1
}

install_dpkg_deb
echo "==> tools/bin/ ready."
