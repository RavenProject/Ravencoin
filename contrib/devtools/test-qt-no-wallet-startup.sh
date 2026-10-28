#!/usr/bin/env bash

set -Eeuo pipefail

if (( $# > 2 )); then
    echo "Usage: $0 [raven-qt path] [raven-cli path]" >&2
    exit 2
fi

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
qt_bin="${1:-${RAVEN_QT_BIN:-$repo_root/src/qt/raven-qt}}"
cli_bin="${2:-${RAVEN_CLI_BIN:-$repo_root/src/raven-cli}}"

for tool in xvfb-run python3; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "Missing required tool: $tool" >&2
        exit 2
    }
done
for binary in "$qt_bin" "$cli_bin"; do
    if [[ ! -x "$binary" ]]; then
        echo "Missing executable: $binary" >&2
        exit 2
    fi
done

work_dir="$(mktemp -d "${TMPDIR:-/tmp}/raven-qt-no-wallet.XXXXXXXX")"
data_dir="$work_dir/data"
mkdir -p "$data_dir"
qt_log="$work_dir/raven-qt.log"
rpc_log="$work_dir/raven-cli.log"
qt_pid=""
waited=0

rpc_port="${RAVEN_RPC_PORT:-$(python3 -c 'import socket; s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()')}"
cli=("$cli_bin" -datadir="$data_dir" -regtest -rpcport="$rpc_port" -rpcclienttimeout=3)

show_logs() {
    for log in "$qt_log" "$rpc_log" "$data_dir/regtest/debug.log"; do
        if [[ -f "$log" ]]; then
            echo "===== $log =====" >&2
            tail -n 100 "$log" >&2
        fi
    done
}

cleanup() {
    status=$?
    trap - EXIT
    if [[ -n "$qt_pid" && "$waited" -eq 0 ]]; then
        if kill -0 "$qt_pid" 2>/dev/null; then
            "${cli[@]}" stop >/dev/null 2>&1 || true
            for (( i = 0; i < 10; i++ )); do
                kill -0 "$qt_pid" 2>/dev/null || break
                sleep 1
            done
            if kill -0 "$qt_pid" 2>/dev/null; then
                pkill -TERM -P "$qt_pid" 2>/dev/null || true
                kill "$qt_pid" 2>/dev/null || true
            fi
        fi
        wait "$qt_pid" 2>/dev/null || true
    fi
    if (( status != 0 )); then
        show_logs
        echo "Qt no-wallet startup failed. Logs: $work_dir" >&2
    fi
    exit "$status"
}
trap cleanup EXIT

echo "Starting $qt_bin with -regtest -disablewallet under Xvfb"
QT_QPA_PLATFORM=xcb xvfb-run -a -s '-screen 0 1024x768x24' \
    "$qt_bin" -datadir="$data_dir" -regtest -disablewallet -server=1 \
    -listen=0 -connect=0 -dnsseed=0 -rpcport="$rpc_port" -splash=0 >"$qt_log" 2>&1 &
qt_pid=$!

deadline=$((SECONDS + 120))
rpc_ready=0
while (( SECONDS < deadline )); do
    if ! kill -0 "$qt_pid" 2>/dev/null; then
        if wait "$qt_pid"; then
            exit_code=0
        else
            exit_code=$?
        fi
        waited=1
        echo "Qt exited before RPC was ready (status $exit_code)" >&2
        exit 1
    fi
    if "${cli[@]}" getblockchaininfo >/dev/null 2>"$rpc_log"; then
        rpc_ready=1
        break
    fi
    sleep 1
done

if (( ! rpc_ready )); then
    echo "Qt RPC did not become ready within 120 seconds" >&2
    exit 1
fi
sleep 2
if ! kill -0 "$qt_pid" 2>/dev/null; then
    echo "Qt exited after RPC readiness" >&2
    exit 1
fi

"${cli[@]}" stop >"$rpc_log" 2>&1 || {
    echo "Qt RPC stop failed" >&2
    exit 1
}

deadline=$((SECONDS + 30))
while kill -0 "$qt_pid" 2>/dev/null; do
    if (( SECONDS >= deadline )); then
        echo "Qt did not stop within 30 seconds" >&2
        exit 1
    fi
    sleep 1
done

if wait "$qt_pid"; then
    waited=1
    echo "Qt no-wallet startup and RPC shutdown passed. Logs: $work_dir"
else
    exit_code=$?
    waited=1
    echo "Qt exited with status $exit_code after RPC stop" >&2
    exit 1
fi
