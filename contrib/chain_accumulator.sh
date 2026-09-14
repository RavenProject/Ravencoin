#!/usr/bin/env bash
# Persistent chain accumulator — grows one regtest chain with diverse assets + failures.
#
# One shot:
#   /opt/Ravencoin/contrib/chain_accumulator.sh
#
# Continuous daemon (log-safe; does not die on parent stdout SIGPIPE):
#   CHAIN_ACCUMULATOR_LOOP=1 CHAIN_ACCUMULATOR_SLEEP_SEC=300 \
#   CHAIN_ACCUMULATOR_RUN_P2AH=1 \
#     nohup /opt/Ravencoin/contrib/chain_accumulator.sh \
#       >> /opt/Ravencoin/logs/chain-accumulator/daemon.log 2>&1 &
#
# Environment:
#   CHAIN_ACCUMULATOR_REPO
#   CHAIN_ACCUMULATOR_DATADIR       default: /tmp/rvn-chain-accumulator/chain
#   CHAIN_ACCUMULATOR_LOG_DIR       default: $REPO/logs/chain-accumulator
#   CHAIN_ACCUMULATOR_TICKS         default: 1
#   CHAIN_ACCUMULATOR_RESET_CHAIN   set 1 to wipe once
#   CHAIN_ACCUMULATOR_LOOP          set 1 for daemon
#   CHAIN_ACCUMULATOR_SLEEP_SEC     default: 300
#   CHAIN_ACCUMULATOR_RUN_P2AH      set 1 to run P2AH stress after base (default: 0)
#   CHAIN_ACCUMULATOR_P2AH_ROUNDS   default: 1
#   CHAIN_ACCUMULATOR_INCLUDE_P2AH  set 1 to include P2AH in core accumulator
#   CHAIN_ACCUMULATOR_MAX_RUNS_BEFORE_RESET
#                                    optional crude reset; default 0 = disabled
#                                    (tests mine coinbase into the wallet when low)
#   CHAIN_ACCUMULATOR_STOP_ON_FAIL  set 1 to exit daemon on first failure
#   CHAIN_ACCUMULATOR_LOCK_FILE
#   CHAIN_ACCUMULATOR_SKIP_BUILD

set -euo pipefail

REPO="${CHAIN_ACCUMULATOR_REPO:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
DATADIR="${CHAIN_ACCUMULATOR_DATADIR:-/tmp/rvn-chain-accumulator/chain}"
LOG_DIR="${CHAIN_ACCUMULATOR_LOG_DIR:-${REPO}/logs/chain-accumulator}"
TICKS="${CHAIN_ACCUMULATOR_TICKS:-1}"
RESET_CHAIN="${CHAIN_ACCUMULATOR_RESET_CHAIN:-0}"
LOOP="${CHAIN_ACCUMULATOR_LOOP:-0}"
SLEEP_SEC="${CHAIN_ACCUMULATOR_SLEEP_SEC:-300}"
RUN_P2AH="${CHAIN_ACCUMULATOR_RUN_P2AH:-0}"
P2AH_ROUNDS="${CHAIN_ACCUMULATOR_P2AH_ROUNDS:-1}"
INCLUDE_P2AH="${CHAIN_ACCUMULATOR_INCLUDE_P2AH:-0}"
MAX_RUNS_BEFORE_RESET="${CHAIN_ACCUMULATOR_MAX_RUNS_BEFORE_RESET:-0}"
STOP_ON_FAIL="${CHAIN_ACCUMULATOR_STOP_ON_FAIL:-0}"
LOCK_FILE="${CHAIN_ACCUMULATOR_LOCK_FILE:-/tmp/rvn_chain_accumulator.lock}"
PYTHON="${PYTHON:-python3}"
RAVEND_BIN="${REPO}/src/ravend"
RAVENCLI="${REPO}/src/raven-cli"
SUMMARY_LOG="${LOG_DIR}/summary.log"
FORCE_RESET_NEXT=0

mkdir -p "${LOG_DIR}" "${DATADIR}"

ensure_binaries() {
    if [[ -x "${RAVEND_BIN}" ]]; then
        return 0
    fi
    if [[ "${CHAIN_ACCUMULATOR_SKIP_BUILD:-0}" == "1" ]]; then
        echo "ERROR: ${RAVEND_BIN} missing and CHAIN_ACCUMULATOR_SKIP_BUILD=1"
        return 1
    fi
    echo "Building ravend..."
    cd "${REPO}"
    if [[ ! -f Makefile ]]; then
        ./autogen.sh
        BDB_LIBS='-L/opt/db4/lib -ldb_cxx-4.8 -lpthread' \
        BDB_CFLAGS='-I/opt/db4/include' \
        LDFLAGS='-lpthread' \
        ./configure --disable-shared --with-pic --enable-benchmark=no --with-bignum=no --enable-module-recovery
    fi
    make -j"${CHAIN_ACCUMULATOR_JOBS:-2}" -C src ravend
}

run_one_iteration() {
    local run_id run_log rc reset_this_run prior_runs overall_rc acc_rc stress_rc
    run_id="$(date -u +%Y%m%dT%H%M%SZ)"
    run_log="${LOG_DIR}/run-${run_id}.log"
    reset_this_run="${RESET_CHAIN}"
    prior_runs=0
    if [[ -f "${DATADIR}/chain_accumulator_run_counter" ]]; then
        prior_runs="$(cat "${DATADIR}/chain_accumulator_run_counter" 2>/dev/null || echo 0)"
    fi
    if [[ "${FORCE_RESET_NEXT}" == "1" ]]; then
        reset_this_run=1
        FORCE_RESET_NEXT=0
    fi
    if [[ "${reset_this_run}" != "1" && "${MAX_RUNS_BEFORE_RESET}" != "0" && "${prior_runs}" -ge "${MAX_RUNS_BEFORE_RESET}" ]]; then
        reset_this_run=1
    fi

    # Write only to the run log. Avoid tee->stdout so a closed parent pipe
    # cannot SIGPIPE the daemon (exit 141 / empty logs).
    {
        echo "================================================================"
        echo "Chain accumulator ${run_id}"
        echo "repo=${REPO} datadir=${DATADIR} ticks=${TICKS} p2ah=${RUN_P2AH} rounds=${P2AH_ROUNDS}"
        echo "ravend: $("${RAVEND_BIN}" --version | head -1)"
        if [[ "${reset_this_run}" == "1" ]]; then
            echo "Resetting persistent chain this iteration"
        fi
        if [[ -f "${DATADIR}/chain_accumulator_run_counter" ]]; then
            echo "accumulator runs: $(cat "${DATADIR}/chain_accumulator_run_counter")"
        fi
        if [[ -f "${DATADIR}/p2ah_stress_run_counter" ]]; then
            echo "p2ah stress runs: $(cat "${DATADIR}/p2ah_stress_run_counter")"
        fi
        echo "================================================================"

        export RAVEND="${RAVEND_BIN}"
        export RAVENCLI="${RAVENCLI}"
        cd "${REPO}"

        local acc_args=(
            --persistent-dir="${DATADIR}"
            --ticks="${TICKS}"
            --nocleanup
        )
        if [[ "${reset_this_run}" == "1" ]]; then
            acc_args+=(--reset-chain)
        fi
        if [[ "${INCLUDE_P2AH}" == "1" ]]; then
            acc_args+=(--include-p2ah)
        fi

        overall_rc=0
        acc_rc=0
        stress_rc=0

        echo ""
        echo "---- asset diversity accumulator ----"
        set +e
        "${PYTHON}" test/functional/feature_chain_accumulator.py "${acc_args[@]}"
        acc_rc=$?
        set -e
        if [[ ${acc_rc} -ne 0 ]]; then
            overall_rc=${acc_rc}
            echo "Accumulator phase failed with exit=${acc_rc}"
            if grep -q "exhausted practical coinbase" "${run_log}" 2>/dev/null; then
                echo "Detected coinbase/burn exhaustion - will reset chain next iteration"
                FORCE_RESET_NEXT=1
            fi
        fi

        if [[ "${RUN_P2AH}" == "1" && ${acc_rc} -eq 0 ]]; then
            echo ""
            echo "---- P2AH + restricted stress (append) ----"
            local stress_args=(
                --persistent-dir="${DATADIR}"
                --stress-rounds="${P2AH_ROUNDS}"
                --nocleanup
            )
            set +e
            "${PYTHON}" test/functional/feature_assetauth_stress.py "${stress_args[@]}"
            stress_rc=$?
            set -e
            if [[ ${stress_rc} -ne 0 ]]; then
                overall_rc=${stress_rc}
                echo "P2AH stress phase failed with exit=${stress_rc}"
                if grep -q "exhausted practical coinbase" "${run_log}" 2>/dev/null; then
                    echo "Detected coinbase/burn exhaustion - will reset chain next iteration"
                    FORCE_RESET_NEXT=1
                fi
            fi
        elif [[ "${RUN_P2AH}" == "1" ]]; then
            echo ""
            echo "---- P2AH + restricted stress (append) ----"
            echo "Skipping P2AH stress because accumulator phase failed"
        fi

        echo ""
        local _height=""
        if [[ -f "${DATADIR}/p2ah_chain_height" ]]; then
            _height="$(cat "${DATADIR}/p2ah_chain_height")"
        elif [[ -f "${DATADIR}/chain_accumulator_height" ]]; then
            _height="$(cat "${DATADIR}/chain_accumulator_height")"
        else
            _height="?"
        fi
        echo "Persistent chain height: ${_height}"
        echo "Accumulator runs: $(cat "${DATADIR}/chain_accumulator_run_counter" 2>/dev/null || echo '0')"
        echo "P2AH stress runs: $(cat "${DATADIR}/p2ah_stress_run_counter" 2>/dev/null || echo '0')"
        echo "DONE log=${run_log}"
    } >"${run_log}" 2>&1

    # Brace group shares this shell, so overall_rc set inside persists.
    # Do not `return` inside the redirected group; that skips summary writes.
    rc="${overall_rc:-1}"
    RESET_CHAIN=0
    if [[ ${rc} -eq 0 ]]; then
        printf '%s PASS height=%s datadir=%s log=%s\n' \
            "$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
            "$(cat "${DATADIR}/p2ah_chain_height" 2>/dev/null || cat "${DATADIR}/chain_accumulator_height" 2>/dev/null || echo '?')" \
            "${DATADIR}" "${run_log}" >> "${SUMMARY_LOG}"
        echo "PASS ${run_id} log=${run_log}"
    else
        printf '%s FAIL exit=%s datadir=%s log=%s\n' \
            "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "${rc}" "${DATADIR}" "${run_log}" >> "${SUMMARY_LOG}"
        echo "FAIL ${run_id} exit=${rc} log=${run_log}"
        tail -n 20 "${run_log}" || true
    fi
    return "${rc}"
}

ensure_binaries

exec 9>"${LOCK_FILE}"
if ! flock -n 9; then
    echo "$(date -u +%Y-%m-%dT%H:%M:%SZ) SKIP already running (lock ${LOCK_FILE})" | tee -a "${SUMMARY_LOG}"
    exit 0
fi

if [[ "${LOOP}" == "1" ]]; then
    echo "Chain accumulator daemon: datadir=${DATADIR} sleep=${SLEEP_SEC}s p2ah=${RUN_P2AH}"
    trap 'pkill -P $$ 2>/dev/null || true; exec 9>&-; echo "Stopping chain accumulator daemon"; exit 0' INT TERM
    while true; do
        set +e
        run_one_iteration
        rc=$?
        set -e
        if [[ ${rc} -ne 0 && "${STOP_ON_FAIL}" == "1" ]]; then
            exit "${rc}"
        fi
        # Do not let the sleep child inherit the flock fd; orphan sleep can
        # otherwise hold the lock after the parent is killed.
        sleep "${SLEEP_SEC}" 9<&-
    done
else
    run_one_iteration
fi
