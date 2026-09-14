#!/usr/bin/env bash
# P2AH continuous validation harness for cron/CI.
#
# Uses a PERSISTENT regtest chain at P2AH_DATADIR — each cron tick appends more
# blocks/transactions instead of starting over.
#
# Example crontab (every 5 minutes):
#   */5 * * * * /opt/Ravencoin/contrib/p2ah_cron_stress_test.sh >> /opt/Ravencoin/logs/p2ah-stress/cron.log 2>&1
#
# Environment overrides:
#   P2AH_REPO            — repo root (default: parent of contrib/)
#   P2AH_DATADIR         — persistent node datadirs (default: $P2AH_REPO/logs/p2ah-stress/chain)
#   P2AH_LOG_DIR         — log directory (default: $P2AH_REPO/logs/p2ah-stress)
#   P2AH_STRESS_ROUNDS   — scenario rounds per cron tick (default: 1)
#   P2AH_RUN_RELATED     — set to 0 to skip adjacent asset functional tests (default: 0)
#   P2AH_RESET_CHAIN     — set to 1 to wipe P2AH_DATADIR before the stress run
#   P2AH_SKIP_BUILD      — set to 1 to skip auto-build when binaries missing
#   P2AH_JOBS            — parallel make jobs (default: nproc)

set -euo pipefail

P2AH_REPO="${P2AH_REPO:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
P2AH_DATADIR="${P2AH_DATADIR:-${P2AH_REPO}/logs/p2ah-stress/chain}"
P2AH_LOG_DIR="${P2AH_LOG_DIR:-${P2AH_REPO}/logs/p2ah-stress}"
P2AH_STRESS_ROUNDS="${P2AH_STRESS_ROUNDS:-1}"
P2AH_RUN_RELATED="${P2AH_RUN_RELATED:-0}"
P2AH_JOBS="${P2AH_JOBS:-$(nproc)}"
LOCK_FILE="${P2AH_LOCK_FILE:-/tmp/p2ah_cron_stress_test.lock}"
PTHREAD_SHIM="${P2AH_PTHREAD_SHIM:-/tmp/pthread_yield_compat.c}"

RUN_ID="$(date -u +%Y%m%dT%H%M%SZ)"
RUN_LOG="${P2AH_LOG_DIR}/run-${RUN_ID}.log"
SUMMARY_LOG="${P2AH_LOG_DIR}/summary.log"

mkdir -p "${P2AH_LOG_DIR}" "${P2AH_DATADIR}"

exec 9>"${LOCK_FILE}"
if ! flock -n 9; then
    echo "$(date -u +%Y-%m-%dT%H:%M:%SZ) SKIP already running (lock ${LOCK_FILE})" | tee -a "${SUMMARY_LOG}"
    exit 0
fi

exec > >(tee -a "${RUN_LOG}") 2>&1

echo "================================================================"
echo "P2AH stress run ${RUN_ID}"
echo "repo=${P2AH_REPO} datadir=${P2AH_DATADIR} rounds=${P2AH_STRESS_ROUNDS}"
echo "================================================================"

cd "${P2AH_REPO}"

TEST_BIN="${P2AH_REPO}/src/test/test_raven"
RAVEND_BIN="${P2AH_REPO}/src/ravend"
PYTHON="${PYTHON:-python3}"

TOTAL=0
PASSED=0
FAILED=0
SKIPPED=0

ensure_pthread_shim() {
    if [[ ! -f /tmp/pthread_yield_compat.o ]]; then
        if [[ ! -f "${PTHREAD_SHIM}" ]]; then
            cat > "${PTHREAD_SHIM}" <<'EOF'
#include <sched.h>
int pthread_yield(void) { return sched_yield(); }
EOF
        fi
        gcc -c "${PTHREAD_SHIM}" -o /tmp/pthread_yield_compat.o
    fi
}

ensure_binaries() {
    if [[ -x "${TEST_BIN}" && -x "${RAVEND_BIN}" ]]; then
        return 0
    fi
    if [[ "${P2AH_SKIP_BUILD:-0}" == "1" ]]; then
        echo "ERROR: binaries missing and P2AH_SKIP_BUILD=1"
        return 1
    fi
    echo "Building ravend + test_raven..."
    ensure_pthread_shim
    if [[ ! -f Makefile ]]; then
        ./autogen.sh
        BDB_LIBS='-L/opt/db4/lib -ldb_cxx-4.8 -lpthread' \
        BDB_CFLAGS='-I/opt/db4/include' \
        LDFLAGS='-lpthread' \
        ./configure --disable-shared --with-pic --enable-benchmark=no --with-bignum=no --enable-module-recovery
    fi
    make -j"${P2AH_JOBS}" -C src ravend test/test_raven LIBS="/tmp/pthread_yield_compat.o"
}

run_step() {
    local name="$1"
    shift
    TOTAL=$((TOTAL + 1))
    local start end elapsed rc
    start=$(date +%s)
    echo ""
    echo "---- [${TOTAL}] ${name} ----"
    set +e
    "$@"
    rc=$?
    set -e
    end=$(date +%s)
    elapsed=$((end - start))
    if [[ ${rc} -eq 0 ]]; then
        PASSED=$((PASSED + 1))
        echo "PASS ${name} (${elapsed}s)"
        printf '%s PASS %s (%ss)\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "${name}" "${elapsed}" >> "${SUMMARY_LOG}"
    else
        FAILED=$((FAILED + 1))
        echo "FAIL ${name} exit=${rc} (${elapsed}s)"
        printf '%s FAIL %s exit=%s (%ss)\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "${name}" "${rc}" "${elapsed}" >> "${SUMMARY_LOG}"
    fi
    return 0
}

run_boost_suite() {
    local suite="$1"
    "${TEST_BIN}" --run_test="${suite}"
}

run_persistent_stress() {
    local extra_args=()
    extra_args+=(--persistent-dir="${P2AH_DATADIR}")
    extra_args+=(--stress-rounds="${P2AH_STRESS_ROUNDS}")
    extra_args+=(--nocleanup)
    if [[ "${P2AH_RESET_CHAIN:-0}" == "1" ]]; then
        extra_args+=(--reset-chain)
    fi
    export RAVEND="${RAVEND_BIN}"
    export RAVENCLI="${P2AH_REPO}/src/raven-cli"
    (cd "${P2AH_REPO}" && "${PYTHON}" test/functional/feature_assetauth_stress.py "${extra_args[@]}")
}

ensure_binaries

echo "ravend: $("${RAVEND_BIN}" --version | head -1)"
echo "test_raven: ${TEST_BIN}"
if [[ -f "${P2AH_DATADIR}/p2ah_stress_run_counter" ]]; then
    echo "persistent run counter: $(cat "${P2AH_DATADIR}/p2ah_stress_run_counter")"
fi

# --- Unit tests (stateless; run every tick) ---
UNIT_SUITES=(
    assetauth_tests
    base58_tests
    asset_tests
    asset_tx_tests
    script_standard_tests
)

for suite in "${UNIT_SUITES[@]}"; do
    run_step "unit:${suite}" run_boost_suite "${suite}"
done

# --- Persistent chain stress (appends history every tick) ---
run_step "functional:persistent_stress" run_persistent_stress

# --- Optional: related functional tests on ephemeral tmpdirs (do not touch persistent chain) ---
RELATED_FUNCTIONAL=(
    feature_assets.py
    rpc_signrawtransaction.py
)

for script in "${RELATED_FUNCTIONAL[@]}"; do
    if [[ "${P2AH_RUN_RELATED}" == "1" ]]; then
        run_step "functional:ephemeral:${script}" \
            bash -c "cd '${P2AH_REPO}' && '${PYTHON}' test/functional/test_runner.py '${script}'"
    else
        SKIPPED=$((SKIPPED + 1))
        echo "SKIP functional:ephemeral:${script} P2AH_RUN_RELATED=0"
    fi
done

# --- Extra P2AH unit passes ---
for loop in $(seq 1 2); do
    run_step "unit:assetauth_tests:repeat${loop}" run_boost_suite assetauth_tests
done

if [[ -d "${P2AH_DATADIR}/node0/regtest" ]]; then
    echo ""
    if [[ -f "${P2AH_DATADIR}/p2ah_chain_height" ]]; then
        echo "Persistent chain height: $(cat "${P2AH_DATADIR}/p2ah_chain_height")"
    else
        echo -n "Persistent chain height: "
        "${P2AH_REPO}/src/raven-cli" -regtest -datadir="${P2AH_DATADIR}/node0" getblockcount 2>/dev/null || echo "?"
    fi
    echo -n "Persistent stress runs completed: "
    cat "${P2AH_DATADIR}/p2ah_stress_run_counter" 2>/dev/null || echo "0"
fi

echo ""
echo "================================================================"
echo "P2AH stress run ${RUN_ID} complete"
echo "total=${TOTAL} passed=${PASSED} failed=${FAILED} skipped=${SKIPPED}"
echo "datadir=${P2AH_DATADIR} log=${RUN_LOG}"
echo "================================================================"

printf '%s DONE total=%s passed=%s failed=%s datadir=%s log=%s\n' \
    "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "${TOTAL}" "${PASSED}" "${FAILED}" "${P2AH_DATADIR}" "${RUN_LOG}" >> "${SUMMARY_LOG}"

if [[ ${FAILED} -gt 0 ]]; then
    exit 1
fi

exit 0
