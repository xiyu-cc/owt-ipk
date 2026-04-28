<script setup>
import { computed, onMounted, onUnmounted, ref, watch } from 'vue'

import AgentPanel from './components/AgentPanel.vue'
import CommandPanel from './components/CommandPanel.vue'
import ParamsPanel from './components/ParamsPanel.vue'
import { useCommandBusSocket } from './composables/useCommandBusSocket'
import {
  monitoringEnabledFromStats,
  monitoringOverrideFromCommandResource,
  shouldClearMonitoringOverride
} from './monitoring-state'
import { COMMAND_BUS_EVENTS, COMMAND_BUS_ACTIONS, WS_UI_PATH } from './protocol/v5'

const DEFAULT_WOL = Object.freeze({
  mac: 'AA:BB:CC:DD:EE:FF',
  broadcast: '192.168.1.255',
  port: 9
})

const DEFAULT_SSH = Object.freeze({
  host: '192.168.1.10',
  port: 22,
  user: 'root',
  password: '',
  timeout_ms: 5000
})

const TERMINAL_STATUSES = new Set(['succeeded', 'failed', 'timed_out', 'cancelled'])

const bus = useCommandBusSocket(WS_UI_PATH)

const agents = ref([])
const selectedAgentMac = ref('')
const paramsOpen = ref(false)
const busy = ref(false)
const lastError = ref('')
const latestResult = ref(null)
const monitoringOverride = ref(null)

const wol = ref({ ...DEFAULT_WOL })
const ssh = ref({ ...DEFAULT_SSH })

const selectedAgent = computed(() => {
  return agents.value.find((item) => item.agent_mac === selectedAgentMac.value) || null
})

const selectedAgentLabel = computed(() => {
  return selectedAgent.value?.agent_id || selectedAgent.value?.agent_mac || selectedAgentMac.value || '-'
})

const selectedAgentOnline = computed(() => Boolean(selectedAgent.value?.online))

const selectedAgentOnlineText = computed(() => {
  if (!selectedAgentMac.value) return '未选择'
  return selectedAgentOnline.value ? '在线' : '离线'
})

const selectedAgentHeartbeatText = computed(() => {
  return formatTimeMs(selectedAgent.value?.last_heartbeat_at_ms)
})

const canRunCommand = computed(() => {
  return Boolean(selectedAgentMac.value) && bus.connected.value && !busy.value
})

const probeStats = computed(() => {
  const stats = selectedAgent.value?.stats
  if (!stats || typeof stats !== 'object' || Array.isArray(stats)) {
    return {}
  }
  return stats
})

const probeStatusRaw = computed(() => {
  const value = probeStats.value.status
  return typeof value === 'string' ? value : 'unknown'
})

const monitoringEnabledFromHeartbeat = computed(() => {
  return monitoringEnabledFromStats(probeStats.value)
})

const effectiveMonitoringEnabled = computed(() => {
  const override = monitoringOverride.value
  if (
    override &&
    override.agentMac === selectedAgentMac.value &&
    typeof override.enabled === 'boolean'
  ) {
    return override.enabled
  }
  return monitoringEnabledFromHeartbeat.value
})

const monitoringEnabled = computed(() => {
  return effectiveMonitoringEnabled.value === true
})

const monitoringStatusText = computed(() => {
  if (effectiveMonitoringEnabled.value === true) return '采集开启'
  if (effectiveMonitoringEnabled.value === false) return '采集停止'
  return '采集未知'
})

const probeStatusText = computed(() => {
  if (!selectedAgentMac.value) return '未选择 Agent'
  if (!selectedAgentOnline.value) return 'Agent 离线'

  if (probeStatusRaw.value === 'online') return '在线'
  if (probeStatusRaw.value === 'offline') return '离线'
  if (probeStatusRaw.value === 'paused') return '已暂停'
  return '未知'
})

const statusDotColor = computed(() => {
  if (!selectedAgentMac.value) return 'var(--muted)'
  if (!bus.connected.value) return 'var(--danger)'
  if (!selectedAgentOnline.value) return 'var(--danger)'
  if (probeStatusRaw.value === 'online') return 'var(--success)'
  if (probeStatusRaw.value === 'offline') return 'var(--danger)'
  if (probeStatusRaw.value === 'paused') return 'var(--muted)'
  return 'var(--accent)'
})

function asNumberOrNull(value) {
  const n = Number(value)
  return Number.isFinite(n) ? n : null
}

function parseInteger(value, fallback) {
  const parsed = Number.parseInt(String(value), 10)
  return Number.isFinite(parsed) ? parsed : fallback
}

function normalizeSnapshot(resource) {
  const list = Array.isArray(resource?.agents) ? resource.agents : []
  agents.value = list
  if (!agents.value.find((item) => item.agent_mac === selectedAgentMac.value)) {
    selectedAgentMac.value = agents.value[0]?.agent_mac || ''
  }
}

function upsertAgent(resource) {
  if (!resource || typeof resource !== 'object') return
  if (typeof resource.agent_mac !== 'string' || !resource.agent_mac) return
  const idx = agents.value.findIndex((item) => item.agent_mac === resource.agent_mac)
  if (idx >= 0) {
    agents.value[idx] = { ...agents.value[idx], ...resource }
  } else {
    agents.value.push(resource)
  }
}

function formatTimeMs(value) {
  const ts = Number(value)
  if (!Number.isFinite(ts) || ts <= 0) return '-'
  return new Date(ts).toLocaleString()
}

function formatPercent(value) {
  const n = asNumberOrNull(value)
  return n == null ? '--' : `${n.toFixed(1)}%`
}

function formatMemory(totalKb, availKb) {
  const total = asNumberOrNull(totalKb)
  const avail = asNumberOrNull(availKb)
  if (total == null || avail == null) return '--'
  const usedMb = (total - avail) / 1024
  const totalMb = total / 1024
  return `${usedMb.toFixed(0)} / ${totalMb.toFixed(0)} MB`
}

function formatRate(bytesPerSec) {
  const rate = asNumberOrNull(bytesPerSec)
  if (rate == null) return '--'
  const kb = rate / 1024
  if (kb < 1024) return `${kb.toFixed(1)} KB/s`
  return `${(kb / 1024).toFixed(2)} MB/s`
}

function commandTypeLabel(type) {
  const map = {
    wol_wake: '开机',
    host_reboot: '重启',
    host_poweroff: '关机',
    monitoring_set: '采集开关',
    params_set: '参数下发'
  }
  return map[type] || type || '-'
}

function eventStatusClass(status) {
  const value = String(status || '').toLowerCase()
  if (value === 'succeeded') return 'status-tag ok'
  return 'status-tag bad'
}

function isTerminalStatus(status) {
  return TERMINAL_STATUSES.has(String(status || '').toLowerCase())
}

function trimText(text, max = 220) {
  const value = String(text || '').trim()
  if (!value) return '-'
  if (value.length <= max) return value
  return `${value.slice(0, max)}...`
}

function toReadableText(value) {
  if (value == null) return '-'
  if (typeof value === 'string') return trimText(value)
  if (typeof value === 'number' || typeof value === 'boolean') return String(value)
  try {
    return trimText(JSON.stringify(value))
  } catch (_) {
    return trimText(String(value))
  }
}

function summarizeTerminal(resource) {
  const command = resource?.command || {}
  const event = resource?.event || {}
  const detail = event?.detail || {}
  const status = String(event?.status || '').toLowerCase()
  const commandType = commandTypeLabel(command?.command_type)

  if (status === 'succeeded') {
    return `${commandType}执行成功`
  }
  if (status === 'timed_out') {
    return `${commandType}执行超时`
  }
  if (status === 'cancelled') {
    return `${commandType}已取消`
  }

  if (status === 'failed') {
    const reason =
      detail?.result?.error ||
      detail?.error ||
      detail?.result?.message ||
      command?.result?.message ||
      command?.result?.error ||
      command?.result?.error_code ||
      '执行失败'
    return `${commandType}${toReadableText(reason)}`
  }

  return `${commandType}${status || '-'}`
}

function extractResultDetail(resource) {
  const command = resource?.command || {}
  const event = resource?.event || {}
  const detail = event?.detail || {}

  const candidate =
    detail?.message ||
    detail?.result?.message ||
    detail?.result?.error ||
    detail?.error ||
    command?.result?.message ||
    command?.result?.error ||
    command?.last_error ||
    '-'

  return toReadableText(candidate)
}

function formatResultTime(resource) {
  const ts =
    resource?.event?.created_at_ms ??
    resource?.command?.updated_at_ms ??
    0
  return formatTimeMs(ts)
}

function updateLatestResult(resource) {
  const command = resource?.command || {}
  const event = resource?.event || {}

  latestResult.value = {
    commandType: commandTypeLabel(command?.command_type),
    status: String(event?.status || '-'),
    summary: summarizeTerminal(resource),
    commandId: command?.command_id || '-',
    eventType: event?.event_type || '-',
    timeText: formatResultTime(resource),
    detailText: extractResultDetail(resource)
  }
}

function setMonitoringOverride(agentMac, enabled) {
  if (!agentMac || typeof enabled !== 'boolean') return
  monitoringOverride.value = { agentMac, enabled }
}

function clearMonitoringOverrideForAgent(agentMac) {
  if (!agentMac) return
  if (monitoringOverride.value?.agentMac !== agentMac) return
  monitoringOverride.value = null
}

function applyParams(data) {
  const wolData = data?.wol && typeof data.wol === 'object' ? data.wol : {}
  const sshData = data?.ssh && typeof data.ssh === 'object' ? data.ssh : {}

  wol.value = {
    mac: typeof wolData.mac === 'string' ? wolData.mac : DEFAULT_WOL.mac,
    broadcast: typeof wolData.broadcast === 'string' ? wolData.broadcast : DEFAULT_WOL.broadcast,
    port: parseInteger(wolData.port, DEFAULT_WOL.port)
  }

  const nextPassword =
    typeof sshData.password === 'string' && sshData.password !== '***'
      ? sshData.password
      : ssh.value.password

  ssh.value = {
    host: typeof sshData.host === 'string' ? sshData.host : DEFAULT_SSH.host,
    port: parseInteger(sshData.port, DEFAULT_SSH.port),
    user: typeof sshData.user === 'string' ? sshData.user : DEFAULT_SSH.user,
    password: typeof nextPassword === 'string' ? nextPassword : '',
    timeout_ms: parseInteger(sshData.timeout_ms, DEFAULT_SSH.timeout_ms)
  }
}

function paramsPayload() {
  return {
    wol: {
      mac: String(wol.value.mac || '').trim(),
      broadcast: String(wol.value.broadcast || '').trim(),
      port: parseInteger(wol.value.port, DEFAULT_WOL.port)
    },
    ssh: {
      host: String(ssh.value.host || '').trim(),
      port: parseInteger(ssh.value.port, DEFAULT_SSH.port),
      user: String(ssh.value.user || '').trim(),
      password: String(ssh.value.password || ''),
      timeout_ms: parseInteger(ssh.value.timeout_ms, DEFAULT_SSH.timeout_ms)
    }
  }
}

function wolPayload() {
  const payload = paramsPayload()
  return payload.wol
}

function sshPayload() {
  const payload = paramsPayload()
  return payload.ssh
}

async function loadParams() {
  if (!selectedAgentMac.value) return
  busy.value = true
  try {
    const res = await bus.call(COMMAND_BUS_ACTIONS.PARAMS_GET, { agent_mac: selectedAgentMac.value })
    applyParams(res?.params || {})
    lastError.value = ''
  } catch (err) {
    lastError.value = err instanceof Error ? err.message : String(err)
  } finally {
    busy.value = false
  }
}

async function saveParams() {
  if (!selectedAgentMac.value) return
  busy.value = true
  try {
    const payload = paramsPayload()
    const res = await bus.call(COMMAND_BUS_ACTIONS.PARAMS_UPDATE, {
      agent_mac: selectedAgentMac.value,
      params: payload
    })
    applyParams(res?.params || payload)
    lastError.value = ''
  } catch (err) {
    lastError.value = err instanceof Error ? err.message : String(err)
  } finally {
    busy.value = false
  }
}

async function submitCommand(commandType, payload) {
  if (!selectedAgentMac.value) {
    lastError.value = '请先选择 agent'
    return
  }

  busy.value = true
  try {
    await bus.call(COMMAND_BUS_ACTIONS.COMMAND_SUBMIT, {
      agent_mac: selectedAgentMac.value,
      agent_id: selectedAgent.value?.agent_id || selectedAgentMac.value,
      command_type: commandType,
      payload,
      timeout_ms: 5000,
      max_retry: 1
    })
    lastError.value = ''
  } catch (err) {
    lastError.value = err instanceof Error ? err.message : String(err)
  } finally {
    busy.value = false
  }
}

async function wakeHost() {
  await submitCommand('wol_wake', wolPayload())
}

async function rebootHost() {
  await submitCommand('host_reboot', sshPayload())
}

async function poweroffHost() {
  await submitCommand('host_poweroff', sshPayload())
}

async function toggleMonitoring() {
  const nextEnabled = !monitoringEnabled.value
  await submitCommand('monitoring_set', { enabled: nextEnabled })
}

const offSnapshot = bus.on(COMMAND_BUS_EVENTS.AGENT_SNAPSHOT, (params) => {
  normalizeSnapshot(params?.resource || {})
})

const offAgent = bus.on(COMMAND_BUS_EVENTS.AGENT_UPDATE, (params) => {
  upsertAgent(params?.resource)
  const overrideAgentMac = monitoringOverride.value?.agentMac || ''
  if (shouldClearMonitoringOverride(params, overrideAgentMac)) {
    clearMonitoringOverrideForAgent(overrideAgentMac)
  }
})

const offCommand = bus.on(COMMAND_BUS_EVENTS.COMMAND_EVENT, (params) => {
  const resource = params?.resource
  const agentMac = resource?.command?.agent_mac
  if (selectedAgentMac.value && agentMac && selectedAgentMac.value !== agentMac) {
    return
  }

  if (!isTerminalStatus(resource?.event?.status)) {
    return
  }

  const override = monitoringOverrideFromCommandResource(resource)
  if (override) {
    setMonitoringOverride(override.agentMac, override.enabled)
  }

  updateLatestResult(resource)
})

watch(
  () => selectedAgentMac.value,
  async (next) => {
    if (!next || !bus.connected.value) return
    latestResult.value = null
    try {
      await bus.call(COMMAND_BUS_ACTIONS.SESSION_SUBSCRIBE, { scope: 'agent', agent_mac: next })
    } catch (_) {
      // ignore
    }
    void loadParams()
  }
)

onMounted(() => {
  bus.connect()
})

onUnmounted(() => {
  offSnapshot()
  offAgent()
  offCommand()
  bus.close()
})
</script>

<template>
  <main class="page">
    <div class="bg-glow one"></div>
    <div class="bg-glow two"></div>

    <section class="card title-bar">
      <p class="kicker">OWT LAN CONTROL</p>
      <h1>局域网设备控制中心</h1>
    </section>

    <section class="card controls">
      <h2>控制操作</h2>
      <AgentPanel
        v-model:selectedAgentMac="selectedAgentMac"
        :agents="agents"
        :loading="busy"
      />
      <CommandPanel
        :busy="busy"
        :can-run="canRunCommand"
        :monitoring-enabled="monitoringEnabled"
        @wake="wakeHost"
        @reboot="rebootHost"
        @toggle-monitoring="toggleMonitoring"
        @poweroff="poweroffHost"
      />
    </section>

    <section class="card status">
      <h2>
        执行结果
        <span :style="{ color: statusDotColor }">●</span>
      </h2>

      <div class="probe-line">
        <span>Agent：<strong>{{ selectedAgentLabel }}</strong></span>
        <span>连接：<strong>{{ selectedAgentOnlineText }}</strong></span>
        <span>心跳：<strong>{{ selectedAgentHeartbeatText }}</strong></span>
        <span>目标状态：<strong>{{ probeStatusText }}</strong></span>
        <span>采集：<strong>{{ monitoringStatusText }}</strong></span>
      </div>

      <div class="probe-grid">
        <article class="probe-item">
          <span class="label">CPU</span>
          <strong>{{ formatPercent(probeStats.cpu_usage_percent) }}</strong>
        </article>
        <article class="probe-item">
          <span class="label">内存</span>
          <strong>{{ formatMemory(probeStats.mem_total_kb, probeStats.mem_available_kb) }}</strong>
        </article>
        <article class="probe-item">
          <span class="label">下行</span>
          <strong>{{ formatRate(probeStats.net_rx_bytes_per_sec) }}</strong>
        </article>
        <article class="probe-item">
          <span class="label">上行</span>
          <strong>{{ formatRate(probeStats.net_tx_bytes_per_sec) }}</strong>
        </article>
      </div>

      <p v-if="lastError" class="err-text">{{ lastError }}</p>

      <div v-if="latestResult" class="action-box">
        <div class="action-head">
          <strong>{{ latestResult.commandType }}</strong>
          <span :class="eventStatusClass(latestResult.status)">{{ latestResult.status }}</span>
        </div>
        <p class="action-summary">{{ latestResult.summary }}</p>

        <div class="kv-list">
          <div class="kv-row">
            <span class="key">命令ID</span>
            <span class="value">{{ latestResult.commandId }}</span>
          </div>
          <div class="kv-row">
            <span class="key">事件</span>
            <span class="value">{{ latestResult.eventType }}</span>
          </div>
          <div class="kv-row">
            <span class="key">时间</span>
            <span class="value">{{ latestResult.timeText }}</span>
          </div>
          <div class="kv-row">
            <span class="key">详情</span>
            <span class="value">{{ latestResult.detailText }}</span>
          </div>
        </div>
      </div>
      <p v-else class="hint">暂无终态执行结果。</p>
    </section>

    <section class="card params">
      <button class="collapse-head ghost" @click="paramsOpen = !paramsOpen" :disabled="busy">
        <span>参数栏</span>
        <span>{{ paramsOpen ? '收起' : '展开' }}</span>
      </button>

      <div v-if="paramsOpen" class="params-body">
        <ParamsPanel
          v-model:wol="wol"
          v-model:ssh="ssh"
          :busy="busy"
          @load="loadParams"
          @save="saveParams"
        />
      </div>
    </section>
  </main>
</template>
