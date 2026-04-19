<script setup>
import { computed, onMounted, onUnmounted, ref, watch } from 'vue'

import AgentPanel from './components/AgentPanel.vue'
import CommandPanel from './components/CommandPanel.vue'
import ParamsPanel from './components/ParamsPanel.vue'
import { useRpcSocket } from './composables/useRpcSocket'

const rpc = useRpcSocket('/ws/v3/ui')

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

async function refreshAgents() {
  loadingAgents.value = true
  try {
    const res = await rpc.call('agent_list', { include_offline: true })
    normalizeSnapshot(res?.resource || {})
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
    const res = await rpc.call('params_get', { agent_mac: selectedAgentMac.value })
    paramsText.value = JSON.stringify(res?.resource?.params || {}, null, 2)
    lastAction.value = 'params_get completed'
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
    const res = await rpc.call('params_put', {
      agent_mac: selectedAgentMac.value,
      params: parsed
    })
    paramsText.value = JSON.stringify(res?.resource?.params || parsed, null, 2)
    const commandId = res?.resource?.command?.command_id || '-'
    lastAction.value = `params_put accepted: ${commandId}`
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
    const res = await rpc.call('command_submit', {
      agent_mac: selectedAgentMac.value,
      agent_id: selectedAgent.value?.agent_id || selectedAgentMac.value,
      command_type: commandType,
      payload,
      timeout_ms: 5000,
      max_retry: 1
    })
    const commandId = res?.resource?.command_id || '-'
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
    await rpc.call('subscribe', { scope: 'all' })
  } catch (_) {
    // ignore
  }
  await refreshAgents()
}

const offOpen = rpc.on('__open__', () => {
  void bootstrap()
})

const offSnapshot = rpc.on('agent_snapshot', (params) => {
  normalizeSnapshot(params?.resource || {})
})

const offAgent = rpc.on('agent_status_update', (params) => {
  upsertAgent(params?.resource)
})

const offCommand = rpc.on('command_event', (params) => {
  appendCommandEvent(params?.resource)
})

watch(
  () => selectedAgentMac.value,
  async (next) => {
    if (!next || !rpc.connected.value) return
    try {
      await rpc.call('subscribe', { scope: 'agent', agent_mac: next })
    } catch (_) {
      // ignore
    }
    void loadParams()
  }
)

onMounted(() => {
  rpc.connect()
})

onUnmounted(() => {
  offOpen()
  offSnapshot()
  offAgent()
  offCommand()
  rpc.close()
})
</script>

<template>
  <main class="page">
    <header class="card">
      <h1>OWT Control v3</h1>
      <p class="hint">WS-only / JSON-RPC 2.0 / agent+ui channels</p>
      <p v-if="lastError" class="error">{{ lastError }}</p>
      <p v-if="lastAction" class="hint">{{ lastAction }}</p>
    </header>

    <AgentPanel
      v-model:selectedAgentMac="selectedAgentMac"
      :agents="agents"
      :connected="rpc.connected"
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
            <strong>{{ item?.command?.command_type || '-' }}</strong>
            <span>{{ item?.event?.event_type || '-' }}</span>
            <span>{{ item?.event?.status || '-' }}</span>
          </div>
          <pre class="mono">{{ JSON.stringify(item, null, 2) }}</pre>
        </article>
      </div>
    </section>
  </main>
</template>
