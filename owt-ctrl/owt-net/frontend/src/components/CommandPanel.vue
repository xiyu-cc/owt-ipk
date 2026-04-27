<script setup>
import { computed } from 'vue'

const props = defineProps({
  busy: { type: Boolean, default: false },
  canRun: { type: Boolean, default: false },
  monitoringEnabled: { type: Boolean, default: false }
})

const emit = defineEmits(['wake', 'reboot', 'toggleMonitoring', 'poweroff'])

const monitoringClass = computed(() => {
  return props.monitoringEnabled ? 'monitoring-on' : 'monitoring-off'
})

const blocked = computed(() => props.busy || !props.canRun)
</script>

<template>
  <div class="command-panel">
    <div class="control-row">
      <button class="control-btn" type="button" :disabled="blocked" @click="emit('wake')">开机</button>
      <button class="control-btn" type="button" :disabled="blocked" @click="emit('reboot')">重启</button>
      <button class="control-btn" :class="monitoringClass" type="button" :disabled="blocked" @click="emit('toggleMonitoring')">采集</button>
      <button class="control-btn danger" type="button" :disabled="blocked" @click="emit('poweroff')">关机</button>
    </div>
  </div>
</template>
