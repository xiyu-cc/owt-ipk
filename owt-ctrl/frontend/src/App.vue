<script setup>
import { computed, onMounted, onUnmounted, reactive, ref } from 'vue'

const gatewayHost = window.location.hostname || '192.168.1.1'
const API_BASE = `http://${gatewayHost}:9527`

const paramsBusy = ref(false)
const actionBusy = ref(false)
const paramsOpen = ref(false)
const initialized = ref(false)
const lastError = ref('')
const lastAction = ref(null)
const probeInFlight = ref(false)
const monitoringBusy = ref(false)
const monitoringEnabled = ref(true)

let probeTimer = null

const wolForm = reactive({
  mac: 'AA:BB:CC:DD:EE:FF',
  broadcast: '192.168.1.255',
  port: 9
})

const sshForm = reactive({
  host: '192.168.1.10',
  port: 22,
  user: 'root',
  password: 'password',
  timeout_ms: 5000
})

const probe = reactive({
  status: 'unknown',
  message: '等待探针',
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

const controlsBusy = computed(() => paramsBusy.value || actionBusy.value)
const probeDotColor = computed(() => {
  if (probe.status === 'online') return 'var(--success)'
  if (probe.status === 'offline') return 'var(--danger)'
  if (probe.status === 'paused') return 'var(--muted)'
  return 'var(--muted)'
})
const probeStatusText = computed(() => {
  if (probe.status === 'online') return '在线'
  if (probe.status === 'offline') return '离线'
  if (probe.status === 'paused') return '已暂停'
  return '未知'
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
    sshForm.password = typeof data.ssh.password === 'string' ? data.ssh.password : sshForm.password
    sshForm.timeout_ms = Number(data.ssh.timeout_ms ?? sshForm.timeout_ms)
  }
}

function paramsPayload() {
  return {
    wol: {
      mac: wolForm.mac.trim(),
      broadcast: wolForm.broadcast.trim(),
      port: Number(wolForm.port)
    },
    ssh: {
      host: sshForm.host.trim(),
      port: Number(sshForm.port),
      user: sshForm.user.trim(),
      password: sshForm.password,
      timeout_ms: Number(sshForm.timeout_ms)
    }
  }
}

async function requestApi(path, method = 'GET', payload = null) {
  const target = `${API_BASE}${path}`
  const resp = await fetch(target, {
    method,
    headers: { 'Content-Type': 'application/json' },
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
  if (typeof data.monitoring_enabled === 'boolean') {
    monitoringEnabled.value = data.monitoring_enabled
  }
  probe.status = typeof data.status === 'string' ? data.status : 'unknown'
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

async function loadParams({ silent = false } = {}) {
  paramsBusy.value = true
  if (!silent) {
    lastError.value = ''
  }
  try {
    const resp = await requestApi('/api/v1/params/get')
    applyParams(resp.data || {})
    initialized.value = true
    if (!silent) {
      setActionResult('参数同步', '已从服务端加载参数', [
        { label: '目标主机', value: sshForm.host },
        { label: 'SSH 用户', value: sshForm.user }
      ])
    }
    return true
  } catch (err) {
    lastError.value = toErrorMessage(err)
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
    await requestApi('/api/v1/params/set', 'POST', paramsPayload())
    if (!silent) {
      setActionResult('参数保存', '参数已持久化到服务端', [
        { label: '目标主机', value: sshForm.host },
        { label: 'WOL MAC', value: wolForm.mac }
      ])
    }
    return true
  } catch (err) {
    lastError.value = toErrorMessage(err)
    return false
  } finally {
    paramsBusy.value = false
  }
}

async function runControl(title, path, payload) {
  actionBusy.value = true
  lastError.value = ''
  try {
    const resp = await requestApi(path, 'POST', payload)
    const data = resp.data || {}
    setActionResult(title, '命令已执行', [
      { label: '主机', value: data.host ?? '-' },
      { label: '命令', value: data.command ?? '-' },
      { label: '退出码', value: data.exit_status ?? '-' },
      { label: '输出', value: trimOutput(data.output) }
    ])
  } catch (err) {
    lastError.value = toErrorMessage(err)
    setActionResult(title, '命令执行失败', [
      { label: '错误', value: lastError.value }
    ])
  } finally {
    actionBusy.value = false
  }
}

async function wakeHost() {
  const ok = await saveParams({ silent: true })
  if (!ok) return
  await runControl('开机', '/api/v1/wol/wake', {
    mac: wolForm.mac.trim(),
    broadcast: wolForm.broadcast.trim(),
    port: Number(wolForm.port)
  })
}

async function rebootHost() {
  const ok = await saveParams({ silent: true })
  if (!ok) return
  await runControl('重启', '/api/v1/host/reboot', {
    host: sshForm.host.trim(),
    port: Number(sshForm.port),
    user: sshForm.user.trim(),
    password: sshForm.password,
    timeout_ms: Number(sshForm.timeout_ms)
  })
}

async function poweroffHost() {
  const ok = await saveParams({ silent: true })
  if (!ok) return
  await runControl('关机', '/api/v1/host/poweroff', {
    host: sshForm.host.trim(),
    port: Number(sshForm.port),
    user: sshForm.user.trim(),
    password: sshForm.password,
    timeout_ms: Number(sshForm.timeout_ms)
  })
}

async function probeHost() {
  if (probeInFlight.value) {
    return
  }
  probeInFlight.value = true
  try {
    const resp = await requestApi('/api/v1/host/probe')
    setProbeData(resp.data || {})
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
  } finally {
    probeInFlight.value = false
  }
}

async function loadMonitoringState({ silent = false } = {}) {
  monitoringBusy.value = true
  try {
    const resp = await requestApi('/api/v1/monitoring/get')
    const enabled = Boolean(resp?.data?.enabled)
    monitoringEnabled.value = enabled
    if (!silent) {
      setActionResult('状态监控', enabled ? '状态监控已开启' : '状态监控已关闭', [
        { label: '监控状态', value: enabled ? '开启' : '关闭' }
      ])
    }
    return true
  } catch (err) {
    if (!silent) {
      lastError.value = toErrorMessage(err)
    }
    return false
  } finally {
    monitoringBusy.value = false
  }
}

async function setMonitoringEnabled(enabled) {
  monitoringBusy.value = true
  lastError.value = ''
  try {
    const resp = await requestApi('/api/v1/monitoring/set', 'POST', { enabled })
    monitoringEnabled.value = Boolean(resp?.data?.enabled)
    setActionResult(
      '状态监控',
      monitoringEnabled.value ? '状态监控已开启' : '状态监控已关闭',
      [{ label: '监控状态', value: monitoringEnabled.value ? '开启' : '关闭' }]
    )
    await probeHost()
  } catch (err) {
    lastError.value = toErrorMessage(err)
  } finally {
    monitoringBusy.value = false
  }
}

async function toggleMonitoring() {
  await setMonitoringEnabled(!monitoringEnabled.value)
}

onMounted(async () => {
  await loadParams({ silent: true })
  await loadMonitoringState({ silent: true })
  await probeHost()
  probeTimer = setInterval(probeHost, 1000)
})

onUnmounted(() => {
  if (probeTimer) {
    clearInterval(probeTimer)
    probeTimer = null
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
      <p class="chip">管理地址：<strong>网关（{{ gatewayHost }}:9527）</strong></p>
    </section>

    <section class="card controls">
      <h2>控制操作</h2>
      <div class="control-row">
        <button class="control-btn" @click="wakeHost" :disabled="controlsBusy">开机</button>
        <button class="control-btn" @click="rebootHost" :disabled="controlsBusy">重启</button>
        <button class="control-btn danger" @click="poweroffHost" :disabled="controlsBusy">关机</button>
      </div>
    </section>

    <section class="card status">
      <h2>
        执行结果
        <span :style="{ color: probeDotColor }">●</span>
      </h2>

      <div class="probe-line">
        <span>目标状态：<strong>{{ probeStatusText }}</strong></span>
        <span>主机：<strong>{{ probe.host || sshForm.host || '-' }}</strong></span>
        <span>消息：{{ probe.message }}</span>
        <button class="mini-btn" @click="toggleMonitoring" :disabled="monitoringBusy">
          状态监控：{{ monitoringEnabled ? '开' : '关' }}
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
                <input v-model="sshForm.password" type="password" placeholder="password" />
              </label>
              <label class="field full">
                <span>超时(ms)</span>
                <input v-model.number="sshForm.timeout_ms" type="number" min="100" />
              </label>
            </div>
          </article>
        </div>

        <div class="row params-actions">
          <button class="ghost" @click="loadParams()" :disabled="controlsBusy">刷新参数</button>
          <button @click="saveParams()" :disabled="controlsBusy">保存参数</button>
        </div>
      </div>
    </section>
  </main>
</template>
