#!/usr/bin/env sh
set -eu

CONF_PATH="${CONF_PATH:-/etc/owt-agent/config.ini}"

print_header() {
  echo ""
  echo "===== $1 ====="
}

print_header "Environment"
date -Iseconds || true
uname -a || true

print_header "owt-agent Process"
if command -v pgrep >/dev/null 2>&1; then
  pgrep -af "owt-agent" || true
else
  ps | grep "owt-agent" | grep -v grep || true
fi

PID=""
if command -v pgrep >/dev/null 2>&1; then
  PID="$(pgrep -o owt-agent || true)"
fi

print_header "Config Snapshot"
if [ -f "$CONF_PATH" ]; then
  grep -E "^[[:space:]]*(agent_id|agent_mac|protocol_version|wss_endpoint|heartbeat_interval_ms|status_collect_interval_ms)[[:space:]]*=" "$CONF_PATH" || true
else
  echo "config not found: $CONF_PATH"
fi

print_header "Resolved MAC (runtime hint)"
if [ -n "$PID" ] && command -v logread >/dev/null 2>&1; then
  IDENTITY_LOGS="$(logread | grep -E "agent identity: agent_id=.*agent_mac=" | tail -n 3 || true)"
  if [ -n "$IDENTITY_LOGS" ]; then
    echo "$IDENTITY_LOGS"
  else
    echo "no agent identity log found; runtime may not have resolved/printed agent_mac"
  fi
else
  ip link 2>/dev/null | grep -E "link/(ether|loopback)" || true
fi

print_header "Control Channel Connections"
if [ -n "$PID" ] && command -v lsof >/dev/null 2>&1; then
  lsof -p "$PID" -nP 2>/dev/null | grep -E "TCP|owt-agent" || true
elif command -v netstat >/dev/null 2>&1; then
  netstat -antp 2>/dev/null | grep "owt-agent" || true
else
  echo "skip: lsof/netstat not found"
fi

print_header "owt-agent Runtime Logs"
if command -v logread >/dev/null 2>&1; then
  logread | grep -E "owt-agent|control channel|send register|connect failed|heartbeat dropped|unsupported protocol|agent runtime start failed" | tail -n 500 || true
else
  echo "logread not found"
fi

print_header "Control Channel Key Event Summary"
if command -v logread >/dev/null 2>&1; then
  logread | grep -E \
    "control channel connected|control channel disconnected|send register|agent.registered|server.error|agent_mac is required|missing payload\\.agent_mac" | tail -n 80 || true
  if ! logread | grep -q "agent identity: agent_id=.*agent_mac="; then
    echo "hint: no identity log found; verify config agent_mac/agent_id and runtime identity injection path"
  fi
else
  echo "logread not found"
fi

print_header "Interpretation Hints"
echo "1) repeated 'connect failed' -> endpoint/tls/network issue"
echo "2) 'control channel connected' but no register-accepted on server -> payload/version/route mismatch"
echo "3) many 'heartbeat dropped: channel not connected' -> channel flapping"
echo "4) SYN_SENT to target:22 usually means SSH probe/command path issue, not agent online state"
