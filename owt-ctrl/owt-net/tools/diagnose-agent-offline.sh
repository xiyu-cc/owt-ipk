#!/usr/bin/env sh
set -eu

DB_PATH="${DB_PATH:-/var/lib/owt-net/owt-net.db}"
LOG_PATH="${LOG_PATH:-/var/lib/owt-net/logs/server_core.log}"
AGENT_MAC_FILTER="${1:-}"

print_header() {
  echo ""
  echo "===== $1 ====="
}

if [ -n "$AGENT_MAC_FILTER" ]; then
  if ! printf "%s" "$AGENT_MAC_FILTER" | grep -Eq "^[A-Fa-f0-9:]{2,17}$"; then
    echo "invalid agent_mac filter: $AGENT_MAC_FILTER" >&2
    echo "expected example: AA:BB:CC:DD:EE:FF" >&2
    exit 2
  fi
fi

print_header "Environment"
date -Iseconds || true
uname -a || true

print_header "owt-net Process"
if command -v pgrep >/dev/null 2>&1; then
  pgrep -af "owt-net" || true
else
  ps | grep "owt-net" | grep -v grep || true
fi

print_header "Socket Listeners"
if command -v ss >/dev/null 2>&1; then
  ss -lntp 2>/dev/null | grep -E ":9080|:9081" || true
elif command -v netstat >/dev/null 2>&1; then
  netstat -lntp 2>/dev/null | grep -E ":9080|:9081" || true
else
  echo "skip: ss/netstat not found"
fi

print_header "Agent Rows In DB"
if [ ! -f "$DB_PATH" ]; then
  echo "db not found: $DB_PATH"
else
  if ! command -v sqlite3 >/dev/null 2>&1; then
    echo "sqlite3 not found; install sqlite3 to query $DB_PATH"
    echo "hint: Debian/Ubuntu -> apt-get install -y sqlite3"
    echo "hint: OpenWrt -> opkg update && opkg install sqlite3-cli"
  else
    if [ -n "$AGENT_MAC_FILTER" ]; then
      sqlite3 -header -column "$DB_PATH" \
        "select agent_mac,agent_id,online,last_seen_at_ms,last_heartbeat_at_ms from agents where agent_mac='${AGENT_MAC_FILTER}' order by last_seen_at_ms desc;" || true
    else
      sqlite3 -header -column "$DB_PATH" \
        "select agent_mac,agent_id,online,last_seen_at_ms,last_heartbeat_at_ms from agents order by last_seen_at_ms desc;" || true
    fi
  fi
fi

print_header "Online/Offline Summary"
if [ -f "$DB_PATH" ] && command -v sqlite3 >/dev/null 2>&1; then
  sqlite3 -header -column "$DB_PATH" \
    "select online,count(*) as count from agents group by online order by online desc;" || true
fi

print_header "owt-net Core Log (register/heartbeat/session/error)"
if [ ! -f "$LOG_PATH" ]; then
  echo "log not found: $LOG_PATH"
else
  tail -n 500 "$LOG_PATH" | grep -E \
    "agent register accepted|reject register|ignore heartbeat|agent session closing|bind agent session failed|unbind agent session failed|unsupported_version|bad_envelope|server busy|agent_mac is required|missing payload\\.agent_mac|invalid command\\.ack payload|invalid command\\.result payload" || true
fi

print_header "Nginx Agent Route Snippet"
for c in /etc/nginx/conf.d/owt-net.conf /etc/nginx/sites-enabled/owt-net.conf /etc/nginx/nginx.conf; do
  if [ -f "$c" ]; then
    echo "-- $c"
    grep -n "ws/v5/agent" "$c" || true
  fi
done

print_header "Interpretation Hints"
echo "1) online=0 and last_heartbeat_at_ms not moving -> register/heartbeat likely not accepted by owt-net"
echo "2) duplicate/nearby agent_id with different agent_mac -> likely MAC changed (agent_mac=auto)"
echo "3) frequent 'agent session closing' -> websocket unstable (network/lb/nginx timeout or backend restart)"
echo "4) if Agent online but target status offline -> investigate SSH chain, not control channel"
