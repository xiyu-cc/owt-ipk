<script setup>
const props = defineProps({
  agents: { type: Array, required: true },
  selectedAgentMac: { type: String, default: '' },
  loading: { type: Boolean, default: false }
})

const emit = defineEmits(['update:selectedAgentMac'])

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
    </div>

    <p v-if="!props.agents.length" class="hint">暂无 Agent，可先启动 `owt-agent` 并完成 REGISTER。</p>
  </div>
</template>
