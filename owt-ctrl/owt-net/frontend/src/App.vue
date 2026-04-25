<script setup>
import { computed, onMounted, onUnmounted, ref, watch } from 'vue'

import AgentPanel from './components/AgentPanel.vue'
import CommandPanel from './components/CommandPanel.vue'
import ParamsPanel from './components/ParamsPanel.vue'
import { useCommandBusSocket } from './composables/useCommandBusSocket'
import { COMMAND_BUS_EVENTS, COMMAND_BUS_ACTIONS, WS_UI_PATH } from './protocol/v5'

const bus = useCommandBusSocket(WS_UI_PATH)

const agents = ref([])
const selectedAgentMac = ref('')
const paramsText = ref('{}')
const loadingAgents = ref(false)
const busy = ref(false)
const lastError = ref('')
const lastAction = ref('')
const commandEvents = ref([])

const selectedAgent = computed(() => {
  return agents.value.find((item) => item.agent_mac === selectedAgentMac.value) || null
})

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

function appendCommandEvent(resource) {
  if (!resource || typeof resource !== 'object') return
  commandEvents.value.unshift(resource)
  if (commandEvents.value.length > 80) {
    commandEvents.value.length = 80
  }
}

function commandTypeLabel(type) {
  const map = {
    wol_wake: '开机',
    host_reboot: '重启',
    host_poweroff: '关机',
    host_probe_get: '状态探测',
    monitoring_set: '采集开关',
    params_get: '参数读取',
    params_set: '参数下发'
  }
  return map[type] || type || '-'
}

function summarizeEvent(item) {
  const command = item?.command || {}
  const event = item?.event || {}
  const detail = event?.detail || {}
  const status = event?.status || '-'
  const commandType = commandTypeLabel(command?.command_type)

  if (status === 'succeeded') {
    return `${commandType}执行成功`
  }
  if (status === 'timed_out') {
    return `${commandType}执行超时`
  }
  if (status === 'failed') {
    const reason =
      detail?.result?.error ||
      detail?.error ||
      command?.result?.message ||
      command?.result?.error ||
      command?.result?.error_code ||
      '执行失败'
    return `${commandType}${reason}`
  }
  if (status === 'running') {
    return `${commandType}执行中`
  }
  if (status === 'acked' || status === 'dispatched') {
    return `${commandType}已受理`
  }
  return `${commandType}${status}`
}

async function refreshAgents() {
  loadingAgents.value = true
  try {
    const res = await bus.call(COMMAND_BUS_ACTIONS.AGENT_LIST, { include_offline: true })
    normalizeSnapshot(res || {})
    lastError.value = ''
  } catch (err) {
    lastError.value = err instanceof Error ? err.message : String(err)
  } finally {
    loadingAgents.value = false
  }
}

async function loadParams() {
  if (!selectedAgentMac.value) return
  busy.value = true
  try {
    const res = await bus.call(COMMAND_BUS_ACTIONS.PARAMS_GET, { agent_mac: selectedAgentMac.value })
    paramsText.value = JSON.stringify(res?.params || {}, null, 2)
    lastAction.value = 'params.get completed'
    lastError.value = ''
  } catch (err) {
    lastError.value = err instanceof Error ? err.message : String(err)
  } finally {
    busy.value = false
  }
}

async function saveParams() {
  if (!selectedAgentMac.value) return
  let parsed = null
  try {
    parsed = JSON.parse(paramsText.value || '{}')
  } catch (_) {
    lastError.value = 'params JSON 解析失败'
    return
  }

  busy.value = true
  try {
    const res = await bus.call(COMMAND_BUS_ACTIONS.PARAMS_UPDATE, {
      agent_mac: selectedAgentMac.value,
      params: parsed
    })
    paramsText.value = JSON.stringify(res?.params || parsed, null, 2)
    const commandId = res?.command?.command_id || '-'
    lastAction.value = `params.update accepted: ${commandId}`
    lastError.value = ''
  } catch (err) {
    lastError.value = err instanceof Error ? err.message : String(err)
  } finally {
    busy.value = false
  }
}

async function runCommand({ commandType, payload, title }) {
  if (!selectedAgentMac.value) {
    lastError.value = '请先选择 agent'
    return
  }
  busy.value = true
  try {
    const res = await bus.call(COMMAND_BUS_ACTIONS.COMMAND_SUBMIT, {
      agent_mac: selectedAgentMac.value,
      agent_id: selectedAgent.value?.agent_id || selectedAgentMac.value,
      command_type: commandType,
      payload,
      timeout_ms: 5000,
      max_retry: 1
    })
    const commandId = res?.command_id || '-'
    lastAction.value = `${title} accepted: ${commandId}`
    lastError.value = ''
  } catch (err) {
    lastError.value = err instanceof Error ? err.message : String(err)
  } finally {
    busy.value = false
  }
}

async function bootstrap() {
  try {
    await bus.call(COMMAND_BUS_ACTIONS.SESSION_SUBSCRIBE, { scope: 'all' })
  } catch (_) {
    // ignore
  }
  await refreshAgents()
}

const offOpen = bus.on('__open__', () => {
  void bootstrap()
})

const offSnapshot = bus.on(COMMAND_BUS_EVENTS.AGENT_SNAPSHOT, (params) => {
  normalizeSnapshot(params?.resource || {})
})

const offAgent = bus.on(COMMAND_BUS_EVENTS.AGENT_UPDATE, (params) => {
  upsertAgent(params?.resource)
})

const offCommand = bus.on(COMMAND_BUS_EVENTS.COMMAND_EVENT, (params) => {
  const resource = params?.resource
  const agentMac = resource?.command?.agent_mac
  if (selectedAgentMac.value && agentMac && selectedAgentMac.value !== agentMac) {
    return
  }
  appendCommandEvent(resource)
})

watch(
  () => selectedAgentMac.value,
  async (next) => {
    if (!next || !bus.connected.value) return
    commandEvents.value = []
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
  offOpen()
  offSnapshot()
  offAgent()
  offCommand()
  bus.close()
})
</script>

<template>
  <main class="page">
    <header class="card">
      <h1>OWT Control v5</h1>
      <p class="hint">WS-only / Drogon command bus v5 / agent+ui channels</p>
      <p v-if="lastError" class="error">{{ lastError }}</p>
      <p v-if="lastAction" class="hint">{{ lastAction }}</p>
    </header>

    <AgentPanel
      v-model:selectedAgentMac="selectedAgentMac"
      :agents="agents"
      :connected="bus.connected"
      :loading="loadingAgents || busy"
      @refresh="refreshAgents"
    />

    <CommandPanel :busy="busy" @run="runCommand" />

    <ParamsPanel v-model="paramsText" :busy="busy" @load="loadParams" @save="saveParams" />

    <section class="card">
      <h2>Command Events</h2>
      <div v-if="!commandEvents.length" class="hint">暂无事件</div>
      <div v-else class="events">
        <article v-for="(item, idx) in commandEvents" :key="idx" class="event-item">
          <div class="event-head">
            <strong>{{ commandTypeLabel(item?.command?.command_type) }}</strong>
            <span>{{ item?.event?.event_type || '-' }}</span>
            <span>{{ item?.event?.status || '-' }}</span>
          </div>
          <p class="hint">{{ summarizeEvent(item) }}</p>
          <details>
            <summary class="hint">查看原始详情</summary>
            <pre class="mono">{{ JSON.stringify(item, null, 2) }}</pre>
          </details>
        </article>
      </div>
    </section>
  </main>
</template>
