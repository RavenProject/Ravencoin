#!/usr/bin/env bash
# Comprehensive regression: P2AH + restricted assets (typed null-data tags/freeze).
set -euo pipefail

P2AH_REPO="${P2AH_REPO:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
LOG_DIR="${P2AH_LOG_DIR:-${P2AH_REPO}/logs/p2ah-restricted-regression}"
RUN_ID="$(date -u +%Y%m%dT%H%M%SZ)"
RUN_LOG="${LOG_DIR}/run-${RUN_ID}.log"
SUMMARY_LOG="${LOG_DIR}/summary.log"
TEST_BIN="${P2AH_REPO}/src/test/test_raven"
RAVEND_BIN="${P2AH_REPO}/src/ravend"
PYTHON="${PYTHON:-python3}"

mkdir -p "${LOG_DIR}"
cd "${P2AH_REPO}"

export RAVEND="${RAVEND_BIN}"
export RAVENCLI="${P2AH_REPO}/src/raven-cli"

TOTAL=0
PASSED=0
FAILED=0
FAILED_NAMES=()

log() { echo "[$(date -u +%H:%M:%S)] $*" | tee -a "${RUN_LOG}"; }

run_step() {
    local name="$1"
    shift
    TOTAL=$((TOTAL + 1))
    log "---- START ${name} ----"
    local start end elapsed rc
    start=$(date +%s)
    set +e
    "$@" >> "${RUN_LOG}" 2>&1
    rc=$?
    set -e
    end=$(date +%s)
    elapsed=$((end - start))
    if [[ ${rc} -eq 0 ]]; then
        PASSED=$((PASSED + 1))
        log "PASS ${name} (${elapsed}s)"
        printf '%s PASS %s (%ss)\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "${name}" "${elapsed}" >> "${SUMMARY_LOG}"
    else
        FAILED=$((FAILED + 1))
        FAILED_NAMES+=("${name}")
        log "FAIL ${name} exit=${rc} (${elapsed}s)"
        printf '%s FAIL %s exit=%s (%ss)\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "${name}" "${rc}" "${elapsed}" >> "${SUMMARY_LOG}"
    fi
    return 0
}

if [[ ! -x "${TEST_BIN}" || ! -x "${RAVEND_BIN}" ]]; then
    echo "ERROR: build ${TEST_BIN} and ${RAVEND_BIN} first" | tee -a "${RUN_LOG}"
    exit 1
fi

log "P2AH restricted regression run ${RUN_ID}"
log "repo=${P2AH_REPO} log=${RUN_LOG}"
log "ravend: $("${RAVEND_BIN}" --version | head -1)"

UNIT_SUITES=(
    null_asset_data_tests
    assetauth_tests
    script_standard_tests
    asset_tests
    asset_tx_tests
    cache_tests
)

for suite in "${UNIT_SUITES[@]}"; do
    run_step "unit:${suite}" "${TEST_BIN}" --run_test="${suite}"
done

FUNCTIONAL=(
    feature_assetauth_restricted_p2ah.py
    feature_assetauth_fixes.py
    feature_assetauth_crossfix.py
    feature_assetauth.py
    feature_restricted_assets.py
    feature_raw_restricted_assets.py
)

for script in "${FUNCTIONAL[@]}"; do
    if [[ -f "test/functional/${script}" ]]; then
        run_step "functional:${script}" "${PYTHON}" "test/functional/${script}"
    else
        log "SKIP missing test/functional/${script}"
    fi
done

log "================================================================"
log "DONE total=${TOTAL} passed=${PASSED} failed=${FAILED}"
if [[ ${FAILED} -gt 0 ]]; then
    log "Failures: ${FAILED_NAMES[*]}"
fi
log "log=${RUN_LOG}"
log "================================================================"

printf '%s DONE total=%s passed=%s failed=%s log=%s\n' \
    "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "${TOTAL}" "${PASSED}" "${FAILED}" "${RUN_LOG}" >> "${SUMMARY_LOG}"

[[ ${FAILED} -eq 0 ]]
