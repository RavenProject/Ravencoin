#!/usr/bin/env bash
# Persistent-chain stress: P2AH + restricted assets (typed null-data tags/freeze).
# Appends blocks/transactions each run; use --reset-chain for a fresh chain.
set -euo pipefail

P2AH_REPO="${P2AH_REPO:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
P2AH_DATADIR="${P2AH_RESTRICTED_DATADIR:-/tmp/p2ah-restricted-stress/chain}"
P2AH_LOG_DIR="${P2AH_LOG_DIR:-${P2AH_REPO}/logs/p2ah-restricted-stress}"
P2AH_STRESS_ROUNDS="${P2AH_STRESS_ROUNDS:-2}"
P2AH_RESET_CHAIN="${P2AH_RESET_CHAIN:-0}"
RUN_ID="$(date -u +%Y%m%dT%H%M%SZ)"
RUN_LOG="${P2AH_LOG_DIR}/run-${RUN_ID}.log"
SUMMARY_LOG="${P2AH_LOG_DIR}/summary.log"
RAVEND_BIN="${P2AH_REPO}/src/ravend"
PYTHON="${PYTHON:-python3}"

mkdir -p "${P2AH_LOG_DIR}" "${P2AH_DATADIR}"
cd "${P2AH_REPO}"

export RAVEND="${RAVEND_BIN}"
export RAVENCLI="${P2AH_REPO}/src/raven-cli"

if [[ ! -x "${RAVEND_BIN}" ]]; then
    echo "ERROR: build ${RAVEND_BIN} first" | tee -a "${RUN_LOG}"
    exit 1
fi

{
    echo "================================================================"
    echo "P2AH restricted persistent stress ${RUN_ID}"
    echo "repo=${P2AH_REPO} datadir=${P2AH_DATADIR} rounds=${P2AH_STRESS_ROUNDS}"
    echo "ravend: $("${RAVEND_BIN}" --version | head -1)"
    echo "================================================================"

    extra_args=(
        --persistent-dir="${P2AH_DATADIR}"
        --stress-rounds="${P2AH_STRESS_ROUNDS}"
        --nocleanup
    )
    if [[ "${P2AH_RESET_CHAIN}" == "1" ]]; then
        extra_args+=(--reset-chain)
    fi

    "${PYTHON}" test/functional/feature_assetauth_stress.py "${extra_args[@]}"

    echo ""
    echo "Persistent chain height: $(cat "${P2AH_DATADIR}/p2ah_chain_height" 2>/dev/null || echo '?')"
    echo "Stress run counter: $(cat "${P2AH_DATADIR}/p2ah_stress_run_counter" 2>/dev/null || echo '0')"
    echo "DONE log=${RUN_LOG}"
} 2>&1 | tee -a "${RUN_LOG}"

printf '%s PASS restricted_stress rounds=%s datadir=%s log=%s\n' \
    "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "${P2AH_STRESS_ROUNDS}" "${P2AH_DATADIR}" "${RUN_LOG}" >> "${SUMMARY_LOG}"
