<script setup>
const props = defineProps({
  agents: { type: Array, required: true },
  selectedAgentMac: { type: String, default: '' },
  connected: { type: Boolean, default: false },
  loading: { type: Boolean, default: false }
})

const emit = defineEmits(['update:selectedAgentMac', 'refresh'])

function onSelect(event) {
  emit('update:selectedAgentMac', event.target.value)
}
</script>

<template>
  <section class="card">
    <h2>Agent</h2>
    <div class="row">
      <label for="agent">目标 Agent</label>
      <select id="agent" :value="props.selectedAgentMac" @change="onSelect" :disabled="props.loading">
        <option v-if="!props.agents.length" value="">暂无 agent</option>
        <option v-for="item in props.agents" :key="item.agent_mac" :value="item.agent_mac">
          {{ item.agent_id || item.agent_mac }} ({{ item.online ? 'online' : 'offline' }})
        </option>
      </select>
      <button class="btn" type="button" @click="emit('refresh')" :disabled="props.loading">刷新</button>
    </div>
    <p class="hint">ws: {{ props.connected ? 'connected' : 'disconnected' }}</p>
  </section>
</template>
