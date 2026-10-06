#!/usr/bin/env bash
#
# Build reLoad (Release, universal), test it, and install the VST3 + AU for the
# current user.
#
# Safety rules:
#   * Nothing is installed unless the build, the unit tests and a pluginval run
#     on a staged copy all pass. A failed run leaves the previous install alone.
#   * Only reLoad.vst3 and reLoad.component are ever replaced. Other plugins in
#     the Plug-Ins folders are never touched.
#   * The previous install is backed up to .install-backup/ and put back if
#     validation of the newly installed copy fails.
#
# Env overrides: PLUGINVAL (path to the pluginval binary),
#                PLUGINVAL_STRICTNESS (default 5).

set -euo pipefail

PLUGIN_NAME="reLoad"
AU_TYPE="aumu"
AU_SUBTYPE="Rlod"
AU_MANUFACTURER="Rlda"
STRICTNESS="${PLUGINVAL_STRICTNESS:-5}"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT}/build"
ARTEFACTS="${BUILD_DIR}/${PLUGIN_NAME}_artefacts/Release"
STAGE_DIR="${BUILD_DIR}/install-staging"
LOG_DIR="${BUILD_DIR}/install-logs"
BACKUP_DIR="${ROOT}/.install-backup"
FINGERPRINT_FILE="${BACKUP_DIR}/installed.fingerprint"

VST3_DIR="${HOME}/Library/Audio/Plug-Ins/VST3"
AU_DIR="${HOME}/Library/Audio/Plug-Ins/Components"
VST3_DEST="${VST3_DIR}/${PLUGIN_NAME}.vst3"
AU_DEST="${AU_DIR}/${PLUGIN_NAME}.component"

PLUGINVAL="${PLUGINVAL:-${ROOT}/tools/pluginval.app/Contents/MacOS/pluginval}"

# --------------------------------------------------------------------------
say()  { printf '\n\033[1m==> %s\033[0m\n' "$*"; }
fail() { printf '\n\033[31mFAILED: %s\033[0m\n' "$*" >&2; exit 1; }

# rm -rf only on paths this script owns.
remove_owned() {
    local path="$1"
    case "${path}" in
        "${VST3_DEST}" | "${AU_DEST}" | "${BACKUP_DIR}/"* | "${STAGE_DIR}" | "${STAGE_DIR}/"*) ;;
        *) fail "refusing to delete unexpected path: ${path}" ;;
    esac
    rm -rf -- "${path}"
}

sign_and_unquarantine() {
    local bundle="$1"
    xattr -dr com.apple.quarantine "${bundle}" 2>/dev/null || true
    if xattr -lr "${bundle}" 2>/dev/null | grep -q com.apple.quarantine; then
        fail "could not clear quarantine attribute on ${bundle}"
    fi
    codesign --force --sign - --timestamp=none "${bundle}" >/dev/null 2>&1 || fail "codesign ${bundle}"
    codesign --verify --strict "${bundle}" || fail "codesign verification of ${bundle}"
}

run_pluginval() {
    local target="$1" log="$2"
    "${PLUGINVAL}" --strictness-level "${STRICTNESS}" --timeout-ms 300000 \
        --validate "${target}" >"${log}" 2>&1
}

# Hash of the metadata hosts cache during a scan (VST3 moduleinfo + AU
# component description). If it changes, the DAW needs a rescan.
fingerprint() {
    {
        cat "$1/Contents/Resources/moduleinfo.json"
        /usr/libexec/PlistBuddy -c "Print :AudioComponents" "$2/Contents/Info.plist"
    } | shasum -a 256 | cut -d' ' -f1
}

# --------------------------------------------------------------------------
[[ -x "${PLUGINVAL}" ]] || fail "pluginval not found at ${PLUGINVAL} (see README: Tools)"
command -v cmake >/dev/null || fail "cmake not found"
command -v ninja >/dev/null || fail "ninja not found"
mkdir -p "${LOG_DIR}"

say "Configure + build (Release, universal)"
cmake -S "${ROOT}" -B "${BUILD_DIR}" -G Ninja -DCMAKE_BUILD_TYPE=Release >"${LOG_DIR}/configure.log" 2>&1 \
    || { tail -30 "${LOG_DIR}/configure.log"; fail "configure (log: ${LOG_DIR}/configure.log)"; }
cmake --build "${BUILD_DIR}" --config Release >"${LOG_DIR}/build.log" 2>&1 \
    || { grep -E "error|FAILED" "${LOG_DIR}/build.log" | head -30; fail "build (log: ${LOG_DIR}/build.log)"; }
echo "build OK"

say "Unit + render tests"
ctest --test-dir "${BUILD_DIR}" --output-on-failure >"${LOG_DIR}/tests.log" 2>&1 \
    || { tail -40 "${LOG_DIR}/tests.log"; fail "tests (log: ${LOG_DIR}/tests.log)"; }
grep -E "tests passed" "${LOG_DIR}/tests.log"

say "Stage + ad-hoc sign"
remove_owned "${STAGE_DIR}"
mkdir -p "${STAGE_DIR}"
ditto "${ARTEFACTS}/VST3/${PLUGIN_NAME}.vst3" "${STAGE_DIR}/${PLUGIN_NAME}.vst3"
ditto "${ARTEFACTS}/AU/${PLUGIN_NAME}.component" "${STAGE_DIR}/${PLUGIN_NAME}.component"
sign_and_unquarantine "${STAGE_DIR}/${PLUGIN_NAME}.vst3"
sign_and_unquarantine "${STAGE_DIR}/${PLUGIN_NAME}.component"
echo "staged in ${STAGE_DIR}"

say "pluginval (strictness ${STRICTNESS}) on staged VST3 - gate before install"
run_pluginval "${STAGE_DIR}/${PLUGIN_NAME}.vst3" "${LOG_DIR}/pluginval-staged.log" \
    || { tail -40 "${LOG_DIR}/pluginval-staged.log"; fail "pluginval on staged build; previous install left untouched (log: ${LOG_DIR}/pluginval-staged.log)"; }
echo "pluginval (staged): PASS"

# --------------------------------------------------------------------------
say "Install"
mkdir -p "${VST3_DIR}" "${AU_DIR}" "${BACKUP_DIR}"

first_install=true
[[ -e "${VST3_DEST}" || -e "${AU_DEST}" ]] && first_install=false

# Back up the current install (move, so the swap is quick).
remove_owned "${BACKUP_DIR}/${PLUGIN_NAME}.vst3"
remove_owned "${BACKUP_DIR}/${PLUGIN_NAME}.component"
[[ -e "${VST3_DEST}" ]] && mv "${VST3_DEST}" "${BACKUP_DIR}/${PLUGIN_NAME}.vst3"
[[ -e "${AU_DEST}" ]] && mv "${AU_DEST}" "${BACKUP_DIR}/${PLUGIN_NAME}.component"

restore_backup() {
    printf '\n\033[33mRestoring previous install...\033[0m\n' >&2
    remove_owned "${VST3_DEST}"
    remove_owned "${AU_DEST}"
    [[ -e "${BACKUP_DIR}/${PLUGIN_NAME}.vst3" ]] && mv "${BACKUP_DIR}/${PLUGIN_NAME}.vst3" "${VST3_DEST}"
    [[ -e "${BACKUP_DIR}/${PLUGIN_NAME}.component" ]] && mv "${BACKUP_DIR}/${PLUGIN_NAME}.component" "${AU_DEST}"
    killall -9 AudioComponentRegistrar 2>/dev/null || true
}
trap 'restore_backup' ERR

ditto "${STAGE_DIR}/${PLUGIN_NAME}.vst3" "${VST3_DEST}"
ditto "${STAGE_DIR}/${PLUGIN_NAME}.component" "${AU_DEST}"
sign_and_unquarantine "${VST3_DEST}"
sign_and_unquarantine "${AU_DEST}"

# Make the AU registry pick up the new component (the daemon respawns on demand).
killall -9 AudioComponentRegistrar 2>/dev/null || true

say "pluginval (strictness ${STRICTNESS}) on installed VST3"
if ! run_pluginval "${VST3_DEST}" "${LOG_DIR}/pluginval-installed.log"; then
    tail -40 "${LOG_DIR}/pluginval-installed.log"
    restore_backup; trap - ERR
    fail "pluginval on installed copy; previous version restored (log: ${LOG_DIR}/pluginval-installed.log)"
fi
echo "pluginval (installed): PASS"

say "auval on installed AU"
if ! auval -strict -v "${AU_TYPE}" "${AU_SUBTYPE}" "${AU_MANUFACTURER}" >"${LOG_DIR}/auval.log" 2>&1; then
    tail -40 "${LOG_DIR}/auval.log"
    restore_backup; trap - ERR
    fail "auval on installed AU; previous version restored (log: ${LOG_DIR}/auval.log)"
fi
grep -E "AU VALIDATION SUCCEEDED" "${LOG_DIR}/auval.log"
trap - ERR

# --------------------------------------------------------------------------
new_fp="$(fingerprint "${VST3_DEST}" "${AU_DEST}")"
old_fp="$(cat "${FINGERPRINT_FILE}" 2>/dev/null || true)"
echo "${new_fp}" >"${FINGERPRINT_FILE}"

say "Summary"
echo "Build:      OK (Release, $(lipo -archs "${VST3_DEST}/Contents/MacOS/${PLUGIN_NAME}"))"
echo "Tests:      $(grep -Eo '[0-9]+% tests passed.*' "${LOG_DIR}/tests.log")"
echo "pluginval:  PASS at strictness ${STRICTNESS} (staged + installed)"
echo "auval:      PASS (${AU_TYPE} ${AU_SUBTYPE} ${AU_MANUFACTURER})"
echo "Installed:  ${VST3_DEST}"
echo "            ${AU_DEST}"
echo "Standalone: ${ARTEFACTS}/Standalone/${PLUGIN_NAME}.app (not installed)"
echo "Logs:       ${LOG_DIR}"
echo
if ${first_install}; then
    echo ">>> First install: RESCAN PLUGINS in your DAW to see reLoad."
elif [[ "${new_fp}" != "${old_fp}" ]]; then
    echo ">>> Plugin metadata changed: RESCAN PLUGINS in your DAW (Logic: Plug-in Manager > Reset & Rescan Selection)."
else
    echo ">>> No rescan needed. Restart the DAW (or re-open the project) to load the new build."
fi
