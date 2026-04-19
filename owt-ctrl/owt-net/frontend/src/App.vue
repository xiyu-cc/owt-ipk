<script setup>
import { computed, onMounted, onUnmounted, ref, watch } from 'vue'

import AgentPanel from './components/AgentPanel.vue'
import CommandPanel from './components/CommandPanel.vue'
import ParamsPanel from './components/ParamsPanel.vue'
import { useRpcSocket } from './composables/useRpcSocket'
import { RPC_EVENTS, RPC_METHODS, WS_UI_PATH } from './protocol/v4'

const rpc = useRpcSocket(WS_UI_PATH)

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
    const res = await rpc.call(RPC_METHODS.AGENT_LIST, { include_offline: true })
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
    const res = await rpc.call(RPC_METHODS.PARAMS_GET, { agent_mac: selectedAgentMac.value })
    paramsText.value = JSON.stringify(res?.resource?.params || {}, null, 2)
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
    const res = await rpc.call(RPC_METHODS.PARAMS_UPDATE, {
      agent_mac: selectedAgentMac.value,
      params: parsed
    })
    paramsText.value = JSON.stringify(res?.resource?.params || parsed, null, 2)
    const commandId = res?.resource?.command?.command_id || '-'
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
    const res = await rpc.call(RPC_METHODS.COMMAND_SUBMIT, {
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
    await rpc.call(RPC_METHODS.SESSION_SUBSCRIBE, { scope: 'all' })
  } catch (_) {
    // ignore
  }
  await refreshAgents()
}

const offOpen = rpc.on('__open__', () => {
  void bootstrap()
})

const offSnapshot = rpc.on(RPC_EVENTS.AGENT_SNAPSHOT, (params) => {
  normalizeSnapshot(params?.resource || {})
})

const offAgent = rpc.on(RPC_EVENTS.AGENT_UPDATE, (params) => {
  upsertAgent(params?.resource)
})

const offCommand = rpc.on(RPC_EVENTS.COMMAND_EVENT, (params) => {
  appendCommandEvent(params?.resource)
})

watch(
  () => selectedAgentMac.value,
  async (next) => {
    if (!next || !rpc.connected.value) return
    try {
      await rpc.call(RPC_METHODS.SESSION_SUBSCRIBE, { scope: 'agent', agent_mac: next })
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
      <h1>OWT Control v4</h1>
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
