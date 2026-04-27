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
  <div>
    <div class="agent-row">
      <label for="agent-select">目标 Agent</label>
      <select
        id="agent-select"
        :value="props.selectedAgentMac"
        @change="onSelect"
        :disabled="props.loading"
      >
        <option value="" disabled>{{ props.agents.length ? '请选择 Agent' : '暂无 agent' }}</option>
        <option v-for="item in props.agents" :key="item.agent_mac" :value="item.agent_mac">
          {{ item.agent_id || item.agent_mac }} · {{ item.online ? '在线' : '离线' }}
        </option>
      </select>
      <button class="ghost agent-refresh" type="button" @click="emit('refresh')" :disabled="props.loading">刷新</button>
    </div>

    <p v-if="!props.agents.length" class="hint">暂无 Agent，可先启动 `owt-agent` 并完成 REGISTER。</p>
    <p v-else class="hint">ws: {{ props.connected ? 'connected' : 'disconnected' }}</p>
  </div>
</template>
