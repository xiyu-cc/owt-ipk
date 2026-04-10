<script setup>
import { computed, onMounted, onUnmounted, reactive, ref, watch } from 'vue'

const urlParams = new URLSearchParams(window.location.search)
const apiBaseFromQuery = (urlParams.get('api_base') || '').trim()
const API_BASE = apiBaseFromQuery.replace(/\/+$/, '')

const paramsBusy = ref(false)
const actionBusy = ref(false)
const agentsBusy = ref(false)
const paramsOpen = ref(false)
const initialized = ref(false)
const lastError = ref('')
const lastAction = ref(null)
const probeInFlight = ref(false)
const monitoringBusy = ref(false)

const agentList = ref([])
const selectedAgentId = ref('')

let agentsTimer = null
let statusWs = null
let statusWsReconnectTimer = null
let statusWsStopped = false

const wolForm = reactive({
  mac: 'AA:BB:CC:DD:EE:FF',
  broadcast: '192.168.1.255',
  port: 9
})

const sshForm = reactive({
  host: '192.168.1.10',
  port: 22,
  user: 'root',
  password: '',
  timeout_ms: 5000
})

const probe = reactive({
  status: 'unknown',
  monitoring_enabled: null,
  message: '等待探测',
  host: '',
  cpu_usage_percent: null,
  mem_total_kb: null,
  mem_available_kb: null,
  mem_used_percent: null,
  net_rx_bytes_per_sec: null,
  net_tx_bytes_per_sec: null,
  sample_interval_ms: null,
  updated_at_ms: 0
})

const selectedAgent = computed(() => {
  return agentList.value.find((item) => item.agent_id === selectedAgentId.value) || null
})
const selectedAgentOnline = computed(() => Boolean(selectedAgent.value?.online))
const selectedAgentOnlineText = computed(() => (selectedAgentOnline.value ? '在线' : '离线'))
const selectedAgentHeartbeatText = computed(() => formatTimeMs(selectedAgent.value?.last_heartbeat_at_ms))

const controlsBusy = computed(() => paramsBusy.value || actionBusy.value || agentsBusy.value)
const canRunCommand = computed(() => Boolean(selectedAgentId.value) && !controlsBusy.value)
const probeDotColor = computed(() => {
  if (!selectedAgent.value) return 'var(--muted)'
  if (!selectedAgentOnline.value) return 'var(--danger)'
  if (probe.status === 'online') return 'var(--success)'
  if (probe.status === 'offline') return 'var(--danger)'
  if (probe.status === 'paused') return 'var(--muted)'
  return 'var(--accent)'
})
const probeStatusText = computed(() => {
  if (!selectedAgent.value) return '未选择 Agent'
  if (!selectedAgentOnline.value) return 'Agent 离线'
  if (probe.status === 'online') return '在线'
  if (probe.status === 'offline') return '离线'
  if (probe.status === 'paused') return '已暂停'
  return '待探测'
})
const monitoringStatusText = computed(() => {
  if (probe.monitoring_enabled === true) return '监控开启'
  if (probe.monitoring_enabled === false) return '监控关闭'
  return '监控未知'
})
const monitoringActionText = computed(() => {
  return probe.monitoring_enabled === false ? '开启监控' : '关闭监控'
})

function asNumberOrNull(value) {
  const n = Number(value)
  return Number.isFinite(n) ? n : null
}

function trimOutput(text) {
  const s = typeof text === 'string' ? text.trim() : ''
  if (!s) return '-'
  return s.length > 220 ? `${s.slice(0, 220)}...` : s
}

function redactSensitive(input) {
  const text = String(input ?? '')
  return text
    .replace(/("password"\s*:\s*")([^"]*)(")/gi, '$1***$3')
    .replace(/(password=)([^&\s]+)/gi, '$1***')
}

function parseJsonIfNeeded(value) {
  if (typeof value !== 'string') {
    return value
  }
  try {
    return JSON.parse(value)
  } catch (_) {
    return value
  }
}

function resultPreview(result) {
  if (result == null) return '-'
  if (typeof result === 'string') return trimOutput(result)
  try {
    return trimOutput(JSON.stringify(result))
  } catch (_) {
    return trimOutput(String(result))
  }
}

function applyParams(data) {
  if (data?.wol && typeof data.wol === 'object') {
    wolForm.mac = typeof data.wol.mac === 'string' ? data.wol.mac : wolForm.mac
    wolForm.broadcast = typeof data.wol.broadcast === 'string' ? data.wol.broadcast : wolForm.broadcast
    wolForm.port = Number(data.wol.port ?? wolForm.port)
  }

  if (data?.ssh && typeof data.ssh === 'object') {
    sshForm.host = typeof data.ssh.host === 'string' ? data.ssh.host : sshForm.host
    sshForm.port = Number(data.ssh.port ?? sshForm.port)
    sshForm.user = typeof data.ssh.user === 'string' ? data.ssh.user : sshForm.user
    if (typeof data.ssh.password === 'string' && data.ssh.password !== '***') {
      sshForm.password = data.ssh.password
    }
    sshForm.timeout_ms = Number(data.ssh.timeout_ms ?? sshForm.timeout_ms)
  }
}

function paramsPayload() {
  const ssh = {
    host: sshForm.host.trim(),
    port: Number(sshForm.port),
    user: sshForm.user.trim(),
    timeout_ms: Number(sshForm.timeout_ms)
  }
  if (sshForm.password.trim()) {
    ssh.password = sshForm.password
  }
  return {
    wol: {
      mac: wolForm.mac.trim(),
      broadcast: wolForm.broadcast.trim(),
      port: Number(wolForm.port)
    },
    ssh
  }
}

async function requestApi(path, method = 'GET', payload = null) {
  const target = API_BASE ? `${API_BASE}${path}` : path
  const headers = { 'Content-Type': 'application/json' }
  const resp = await fetch(target, {
    method,
    headers,
    body: payload ? JSON.stringify(payload) : undefined
  })

  const text = await resp.text()
  let data = null
  try {
    data = text ? JSON.parse(text) : {}
  } catch (_) {
    data = { raw: text }
  }

  if (!resp.ok) {
    const detail = data?.message || data?.raw || text || 'request failed'
    throw new Error(`HTTP ${resp.status}: ${redactSensitive(detail)}`)
  }
  return data
}

function toErrorMessage(err) {
  const msg = err instanceof Error ? err.message : String(err)
  return redactSensitive(msg)
}

function setActionResult(title, summary, fields = []) {
  lastAction.value = {
    title,
    summary,
    fields,
    at: new Date().toLocaleTimeString()
  }
}

function setProbeData(data) {
  probe.status = typeof data.status === 'string' ? data.status : 'unknown'
  if (typeof data.monitoring_enabled === 'boolean') {
    probe.monitoring_enabled = data.monitoring_enabled
  }
  probe.message = typeof data.message === 'string' ? data.message : '-'
  probe.host = typeof data.host === 'string' ? data.host : ''
  probe.cpu_usage_percent = asNumberOrNull(data.cpu_usage_percent)
  probe.mem_total_kb = asNumberOrNull(data.mem_total_kb)
  probe.mem_available_kb = asNumberOrNull(data.mem_available_kb)
  probe.mem_used_percent = asNumberOrNull(data.mem_used_percent)
  probe.net_rx_bytes_per_sec = asNumberOrNull(data.net_rx_bytes_per_sec)
  probe.net_tx_bytes_per_sec = asNumberOrNull(data.net_tx_bytes_per_sec)
  probe.sample_interval_ms = asNumberOrNull(data.sample_interval_ms)
  probe.updated_at_ms = Date.now()
}

function applyProbeFromAgentState(agentState) {
  const stats = agentState?.stats
  if (!stats || typeof stats !== 'object' || Array.isArray(stats)) {
    return
  }
  setProbeData(stats)
}

function buildStatusWsUrl() {
  const base = API_BASE ? new URL(API_BASE, window.location.href) : new URL(window.location.href)
  const protocol = base.protocol === 'https:' ? 'wss:' : 'ws:'
  return `${protocol}//${base.host}/ws/status`
}

function clearStatusWsReconnectTimer() {
  if (statusWsReconnectTimer) {
    clearTimeout(statusWsReconnectTimer)
    statusWsReconnectTimer = null
  }
}

function applyStatusSnapshot(snapshot) {
  const list = Array.isArray(snapshot?.data?.agents) ? snapshot.data.agents : []
  if (!list.length && !agentList.value.length) {
    return
  }
  agentList.value = list

  const stillExists = list.some((item) => item.agent_id === selectedAgentId.value)
  if (!stillExists) {
    const onlineFirst = list.find((item) => item.online)?.agent_id
    selectedAgentId.value = onlineFirst || (list[0]?.agent_id ?? '')
  }
  applyProbeFromAgentState(selectedAgent.value)
}

function scheduleStatusWsReconnect() {
  if (statusWsStopped || statusWsReconnectTimer) {
    return
  }
  statusWsReconnectTimer = setTimeout(() => {
    statusWsReconnectTimer = null
    connectStatusWs()
  }, 2000)
}

function connectStatusWs() {
  if (statusWsStopped) {
    return
  }
  clearStatusWsReconnectTimer()
  if (statusWs) {
    statusWs.onopen = null
    statusWs.onmessage = null
    statusWs.onclose = null
    statusWs.onerror = null
    statusWs.close()
    statusWs = null
  }

  let ws = null
  try {
    ws = new WebSocket(buildStatusWsUrl())
  } catch (_) {
    scheduleStatusWsReconnect()
    return
  }
  statusWs = ws
  ws.onmessage = (event) => {
    let message = null
    try {
      message = JSON.parse(event.data)
    } catch (_) {
      return
    }
    if (message?.type === 'STATUS_SNAPSHOT') {
      applyStatusSnapshot(message)
    }
  }
  ws.onclose = () => {
    if (statusWs === ws) {
      statusWs = null
    }
    scheduleStatusWsReconnect()
  }
  ws.onerror = () => {
    // no-op, reconnect will be handled by onclose
  }
}

function restartStatusWs() {
  statusWsStopped = false
  connectStatusWs()
}

function stopStatusWs() {
  statusWsStopped = true
  clearStatusWsReconnectTimer()
  if (!statusWs) {
    return
  }
  statusWs.onopen = null
  statusWs.onmessage = null
  statusWs.onclose = null
  statusWs.onerror = null
  statusWs.close()
  statusWs = null
}

function formatPercent(value) {
  return value == null ? '--' : `${value.toFixed(1)}%`
}

function formatMemory(totalKb, availKb) {
  if (totalKb == null || availKb == null) return '--'
  const usedMb = (totalKb - availKb) / 1024
  const totalMb = totalKb / 1024
  return `${usedMb.toFixed(0)} / ${totalMb.toFixed(0)} MB`
}

function formatRate(bytesPerSec) {
  if (bytesPerSec == null) return '--'
  const kb = bytesPerSec / 1024
  if (kb < 1024) return `${kb.toFixed(1)} KB/s`
  return `${(kb / 1024).toFixed(2)} MB/s`
}

function formatTimeMs(value) {
  const ms = Number(value)
  if (!Number.isFinite(ms) || ms <= 0) return '-'
  return new Date(ms).toLocaleString()
}

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms))
}

function isTerminalStatus(status) {
  return status === 'SUCCEEDED' || status === 'FAILED' || status === 'TIMED_OUT' || status === 'CANCELLED'
}

async function loadAgents({ silent = false } = {}) {
  agentsBusy.value = true
  if (!silent) {
    lastError.value = ''
  }
  try {
    const resp = await requestApi('/api/v1/control/agents/get?include_offline=1')
    const list = Array.isArray(resp?.data?.agents) ? resp.data.agents : []
    agentList.value = list

    const stillExists = list.some((item) => item.agent_id === selectedAgentId.value)
    if (!stillExists) {
      const onlineFirst = list.find((item) => item.online)?.agent_id
      selectedAgentId.value = onlineFirst || (list[0]?.agent_id ?? '')
    }
    applyProbeFromAgentState(selectedAgent.value)

    initialized.value = true
    if (!silent) {
      setActionResult('Agent 同步', '已刷新 Agent 列表', [
        { label: '在线数', value: String(resp?.data?.online_count ?? 0) },
        { label: '总数', value: String(resp?.data?.total_count ?? list.length) }
      ])
    }
    return true
  } catch (err) {
    if (!silent) {
      lastError.value = toErrorMessage(err)
    }
    return false
  } finally {
    agentsBusy.value = false
  }
}

async function dispatchCommand(commandType, payload, { timeoutMs = 45000, pollIntervalMs = 1000 } = {}) {
  if (!selectedAgentId.value) {
    throw new Error('请先选择目标 Agent')
  }

  const pushResp = await requestApi('/api/v1/control/command/push', 'POST', {
    agent_id: selectedAgentId.value,
    command_type: commandType,
    payload
  })

  const commandId = pushResp?.data?.command_id
  if (!commandId) {
    throw new Error('命令下发失败：服务端未返回 command_id')
  }

  const deadline = Date.now() + timeoutMs
  let lastData = null
  while (Date.now() <= deadline) {
    const detailResp = await requestApi(
      `/api/v1/control/command/get?command_id=${encodeURIComponent(commandId)}&event_limit=100`
    )
    lastData = detailResp?.data || {}
    const status = String(lastData?.command?.status || '')
    if (isTerminalStatus(status)) {
      return {
        commandId,
        status,
        result: lastData?.command?.result,
        detail: lastData,
        timedOut: false
      }
    }
    await sleep(pollIntervalMs)
  }

  return {
    commandId,
    status: String(lastData?.command?.status || 'UNKNOWN'),
    result: lastData?.command?.result,
    detail: lastData,
    timedOut: true
  }
}

async function loadParams({ silent = false } = {}) {
  paramsBusy.value = true
  if (!silent) {
    lastError.value = ''
  }
  try {
    const exec = await dispatchCommand('PARAMS_GET', {}, { timeoutMs: 30000, pollIntervalMs: 800 })
    if (exec.timedOut) {
      throw new Error('PARAMS_GET 等待超时')
    }
    if (exec.status !== 'SUCCEEDED') {
      throw new Error(`PARAMS_GET 执行失败：${exec.status}`)
    }

    const data = parseJsonIfNeeded(exec.result)
    if (!data || typeof data !== 'object') {
      throw new Error('PARAMS_GET 返回格式异常')
    }
    applyParams(data)
    initialized.value = true

    if (!silent) {
      setActionResult('参数同步', '已从 Agent 加载参数', [
        { label: 'Agent', value: selectedAgentId.value },
        { label: '目标主机', value: sshForm.host }
      ])
    }
    return true
  } catch (err) {
    if (!silent) {
      lastError.value = toErrorMessage(err)
    }
    return false
  } finally {
    paramsBusy.value = false
  }
}

async function saveParams({ silent = false } = {}) {
  paramsBusy.value = true
  if (!silent) {
    lastError.value = ''
  }
  try {
    const exec = await dispatchCommand('PARAMS_SET', paramsPayload(), { timeoutMs: 30000, pollIntervalMs: 800 })
    if (exec.timedOut) {
      throw new Error('PARAMS_SET 等待超时')
    }
    if (exec.status !== 'SUCCEEDED') {
      throw new Error(`PARAMS_SET 执行失败：${exec.status}`)
    }

    const data = parseJsonIfNeeded(exec.result)
    if (data && typeof data === 'object') {
      applyParams(data)
    }

    if (!silent) {
      setActionResult('参数保存', '参数已下发并持久化到 Agent', [
        { label: 'Agent', value: selectedAgentId.value },
        { label: '目标主机', value: sshForm.host },
        { label: 'WOL MAC', value: wolForm.mac }
      ])
    }
    return true
  } catch (err) {
    if (!silent) {
      lastError.value = toErrorMessage(err)
    }
    return false
  } finally {
    paramsBusy.value = false
  }
}

async function runControlCommand(title, commandType, payload) {
  actionBusy.value = true
  lastError.value = ''
  try {
    const exec = await dispatchCommand(commandType, payload)

    const summary = exec.timedOut
      ? '命令已下发，等待结果超时'
      : exec.status === 'SUCCEEDED'
        ? '命令执行成功'
        : `命令执行失败（${exec.status}）`

    setActionResult(title, summary, [
      { label: 'Agent', value: selectedAgentId.value },
      { label: '命令ID', value: exec.commandId },
      { label: '状态', value: exec.timedOut ? 'PENDING' : exec.status },
      { label: '结果', value: resultPreview(exec.result) }
    ])

    if (exec.timedOut) {
      lastError.value = '命令已下发，等待结果超时，可稍后用 command_id 查询'
      return null
    }
    if (exec.status !== 'SUCCEEDED') {
      lastError.value = `命令执行失败：${exec.status}`
      return null
    }
    return exec
  } catch (err) {
    lastError.value = toErrorMessage(err)
    setActionResult(title, '命令执行失败', [
      { label: '错误', value: lastError.value }
    ])
    return null
  } finally {
    actionBusy.value = false
  }
}

async function wakeHost() {
  const exec = await runControlCommand('开机', 'WOL_WAKE', {
    mac: wolForm.mac.trim(),
    broadcast: wolForm.broadcast.trim(),
    port: Number(wolForm.port)
  })
  if (exec) {
    await probeHost({ silent: true })
  }
}

async function rebootHost() {
  const payload = {
    host: sshForm.host.trim(),
    port: Number(sshForm.port),
    user: sshForm.user.trim(),
    timeout_ms: Number(sshForm.timeout_ms)
  }
  if (sshForm.password.trim()) {
    payload.password = sshForm.password
  }
  const exec = await runControlCommand('重启', 'HOST_REBOOT', {
    ...payload
  })
  if (exec) {
    await probeHost({ silent: true })
  }
}

async function poweroffHost() {
  const payload = {
    host: sshForm.host.trim(),
    port: Number(sshForm.port),
    user: sshForm.user.trim(),
    timeout_ms: Number(sshForm.timeout_ms)
  }
  if (sshForm.password.trim()) {
    payload.password = sshForm.password
  }
  const exec = await runControlCommand('关机', 'HOST_POWEROFF', {
    ...payload
  })
  if (exec) {
    await probeHost({ silent: true })
  }
}

async function probeHost({ silent = false } = {}) {
  if (probeInFlight.value || !selectedAgentId.value) {
    return
  }
  probeInFlight.value = true
  if (!silent) {
    lastError.value = ''
  }
  try {
    const exec = await dispatchCommand('HOST_PROBE_GET', {}, { timeoutMs: 30000, pollIntervalMs: 800 })
    if (exec.timedOut) {
      throw new Error('HOST_PROBE_GET 等待超时')
    }
    if (exec.status !== 'SUCCEEDED') {
      throw new Error(`HOST_PROBE_GET 执行失败：${exec.status}`)
    }

    const data = parseJsonIfNeeded(exec.result)
    if (!data || typeof data !== 'object') {
      throw new Error('HOST_PROBE_GET 返回格式异常')
    }
    setProbeData(data)

    if (!silent) {
      setActionResult('状态探测', '已刷新目标状态', [
        { label: 'Agent', value: selectedAgentId.value },
        { label: '状态', value: probeStatusText.value },
        { label: '消息', value: probe.message }
      ])
    }
  } catch (err) {
    probe.status = 'offline'
    probe.message = toErrorMessage(err)
    probe.cpu_usage_percent = null
    probe.mem_total_kb = null
    probe.mem_available_kb = null
    probe.mem_used_percent = null
    probe.net_rx_bytes_per_sec = null
    probe.net_tx_bytes_per_sec = null
    probe.sample_interval_ms = null
    if (!silent) {
      lastError.value = toErrorMessage(err)
    }
  } finally {
    probeInFlight.value = false
  }
}

async function toggleMonitoring() {
  if (!selectedAgentId.value || monitoringBusy.value) {
    return
  }
  const nextEnabled = probe.monitoring_enabled === false
  monitoringBusy.value = true
  lastError.value = ''
  try {
    const exec = await dispatchCommand('MONITORING_SET', { enabled: nextEnabled }, { timeoutMs: 30000, pollIntervalMs: 800 })
    if (exec.timedOut) {
      throw new Error('MONITORING_SET 等待超时')
    }
    if (exec.status !== 'SUCCEEDED') {
      throw new Error(`MONITORING_SET 执行失败：${exec.status}`)
    }

    let appliedEnabled = nextEnabled
    const data = parseJsonIfNeeded(exec.result)
    if (data && typeof data === 'object' && typeof data.enabled === 'boolean') {
      appliedEnabled = data.enabled
    }
    probe.monitoring_enabled = appliedEnabled
    if (!appliedEnabled) {
      probe.status = 'paused'
      probe.message = 'monitoring disabled'
    } else {
      await probeHost({ silent: true })
    }

    setActionResult('状态监控', appliedEnabled ? '已开启状态监控' : '已关闭状态监控', [
      { label: 'Agent', value: selectedAgentId.value },
      { label: '监控状态', value: appliedEnabled ? '开启' : '关闭' }
    ])
  } catch (err) {
    lastError.value = toErrorMessage(err)
    setActionResult('状态监控', '切换失败', [
      { label: '错误', value: lastError.value }
    ])
  } finally {
    monitoringBusy.value = false
  }
}

watch(
  () => selectedAgentId.value,
  async (newId, oldId) => {
    if (!newId || newId === oldId) {
      return
    }
    await loadParams({ silent: true })
    await probeHost({ silent: true })
  }
)

onMounted(async () => {
  await loadAgents({ silent: true })
  if (selectedAgentId.value) {
    await loadParams({ silent: true })
    await probeHost({ silent: true })
  }
  initialized.value = true
  restartStatusWs()
  agentsTimer = setInterval(() => {
    loadAgents({ silent: true })
  }, 30000)
})

onUnmounted(() => {
  stopStatusWs()
  if (agentsTimer) {
    clearInterval(agentsTimer)
    agentsTimer = null
  }
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
      <div class="agent-row">
        <label for="agent-select">目标 Agent</label>
        <select id="agent-select" v-model="selectedAgentId" :disabled="agentsBusy || actionBusy">
          <option value="" disabled>请选择 Agent</option>
          <option v-for="item in agentList" :key="item.agent_id" :value="item.agent_id">
            {{ item.agent_id }} · {{ item.online ? '在线' : '离线' }}
          </option>
        </select>
        <button class="ghost agent-refresh" @click="loadAgents()" :disabled="agentsBusy || actionBusy">刷新</button>
      </div>
      <p v-if="!agentList.length" class="hint">暂无 Agent，可先启动 `owt-agent` 并完成 REGISTER。</p>

      <div class="control-row">
        <button class="control-btn" @click="wakeHost" :disabled="!canRunCommand">开机</button>
        <button class="control-btn" @click="rebootHost" :disabled="!canRunCommand">重启</button>
        <button class="control-btn danger" @click="poweroffHost" :disabled="!canRunCommand">关机</button>
      </div>
    </section>

    <section class="card status">
      <h2>
        执行结果
        <span :style="{ color: probeDotColor }">●</span>
      </h2>

      <div class="probe-line">
        <span>Agent：<strong>{{ selectedAgentId || '-' }}</strong></span>
        <span>连接：<strong>{{ selectedAgentOnlineText }}</strong></span>
        <span>心跳：{{ selectedAgentHeartbeatText }}</span>
        <span>目标状态：<strong>{{ probeStatusText }}</strong></span>
        <span>监控：<strong>{{ monitoringStatusText }}</strong></span>
        <button
          class="mini-btn"
          @click="toggleMonitoring"
          :disabled="monitoringBusy || !selectedAgentId || actionBusy"
        >
          {{ monitoringActionText }}
        </button>
      </div>

      <div class="probe-grid">
        <article class="probe-item">
          <span class="label">CPU</span>
          <strong>{{ formatPercent(probe.cpu_usage_percent) }}</strong>
        </article>
        <article class="probe-item">
          <span class="label">内存</span>
          <strong>{{ formatMemory(probe.mem_total_kb, probe.mem_available_kb) }}</strong>
        </article>
        <article class="probe-item">
          <span class="label">下行</span>
          <strong>{{ formatRate(probe.net_rx_bytes_per_sec) }}</strong>
        </article>
        <article class="probe-item">
          <span class="label">上行</span>
          <strong>{{ formatRate(probe.net_tx_bytes_per_sec) }}</strong>
        </article>
      </div>

      <div v-if="lastAction" class="action-box">
        <div class="action-head">
          <strong>{{ lastAction.title }}</strong>
          <span>{{ lastAction.at }}</span>
        </div>
        <p class="action-summary">{{ lastAction.summary }}</p>
        <div class="kv-list">
          <div class="kv-row" v-for="item in lastAction.fields" :key="item.label">
            <span class="key">{{ item.label }}</span>
            <span class="value">{{ item.value }}</span>
          </div>
        </div>
      </div>

      <p v-if="lastError" class="err-text">{{ lastError }}</p>
      <p v-else-if="!initialized" class="hint">初始化中...</p>
    </section>

    <section class="card params">
      <button class="collapse-head ghost" @click="paramsOpen = !paramsOpen" :disabled="controlsBusy">
        <span>参数栏</span>
        <span>{{ paramsOpen ? '收起' : '展开' }}</span>
      </button>

      <div v-if="paramsOpen" class="params-body">
        <div class="params-grid">
          <article class="params-group">
            <h3>Wake-on-LAN</h3>
            <div class="fields">
              <label class="field full">
                <span>MAC</span>
                <input v-model="wolForm.mac" placeholder="AA:BB:CC:DD:EE:FF" />
              </label>
              <label class="field">
                <span>广播地址</span>
                <input v-model="wolForm.broadcast" placeholder="192.168.1.255" />
              </label>
              <label class="field">
                <span>端口</span>
                <input v-model.number="wolForm.port" type="number" min="1" max="65535" />
              </label>
            </div>
          </article>

          <article class="params-group">
            <h3>远程控制（SSH）</h3>
            <div class="fields">
              <label class="field full">
                <span>主机 IP</span>
                <input v-model="sshForm.host" placeholder="192.168.1.10" />
              </label>
              <label class="field">
                <span>SSH 端口</span>
                <input v-model.number="sshForm.port" type="number" min="1" max="65535" />
              </label>
              <label class="field">
                <span>用户</span>
                <input v-model="sshForm.user" placeholder="root" />
              </label>
              <label class="field full">
                <span>密码</span>
                <input v-model="sshForm.password" type="password" placeholder="留空表示保持 Agent 已保存密码" />
              </label>
              <label class="field full">
                <span>超时(ms)</span>
                <input v-model.number="sshForm.timeout_ms" type="number" min="100" />
              </label>
            </div>
          </article>
        </div>

        <div class="row params-actions">
          <button class="ghost" @click="loadParams()" :disabled="controlsBusy || !selectedAgentId">从 Agent 读取</button>
          <button @click="saveParams()" :disabled="controlsBusy || !selectedAgentId">保存到 Agent</button>
        </div>
      </div>
    </section>
  </main>
</template>
