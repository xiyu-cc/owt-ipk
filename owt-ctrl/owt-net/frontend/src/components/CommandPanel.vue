<script setup>
import { reactive } from 'vue'

const props = defineProps({
  busy: { type: Boolean, default: false }
})

const emit = defineEmits(['run'])

const wol = reactive({
  mac: 'AA:BB:CC:DD:EE:FF',
  broadcast: '192.168.1.255',
  port: 9
})

const ssh = reactive({
  host: '192.168.1.10',
  port: 22,
  user: 'root',
  password: '',
  timeout_ms: 5000
})

function submit(commandType, payload, title) {
  emit('run', { commandType, payload, title })
}

function wolPayload() {
  return {
    mac: wol.mac,
    broadcast: wol.broadcast,
    port: wol.port
  }
}

function sshPayload() {
  return {
    host: ssh.host,
    port: ssh.port,
    user: ssh.user,
    password: ssh.password,
    timeout_ms: ssh.timeout_ms
  }
}
</script>

<template>
  <section class="card">
    <h2>Commands</h2>
    <div class="grid">
      <button class="btn" type="button" :disabled="props.busy" @click="submit('host_probe_get', {}, '状态探测')">探测</button>
      <button class="btn" type="button" :disabled="props.busy" @click="submit('monitoring_set', { enabled: true }, '开启采集')">采集开</button>
      <button class="btn" type="button" :disabled="props.busy" @click="submit('monitoring_set', { enabled: false }, '关闭采集')">采集关</button>
      <button class="btn" type="button" :disabled="props.busy" @click="submit('host_reboot', sshPayload(), '重启')">重启</button>
      <button class="btn" type="button" :disabled="props.busy" @click="submit('host_poweroff', sshPayload(), '关机')">关机</button>
      <button class="btn" type="button" :disabled="props.busy" @click="submit('wol_wake', wolPayload(), '开机')">开机</button>
    </div>

    <div class="form-grid">
      <label>
        WOL MAC
        <input v-model="wol.mac" :disabled="props.busy" />
      </label>
      <label>
        Broadcast
        <input v-model="wol.broadcast" :disabled="props.busy" />
      </label>
      <label>
        WOL Port
        <input v-model.number="wol.port" type="number" :disabled="props.busy" />
      </label>
      <label>
        SSH Host
        <input v-model="ssh.host" :disabled="props.busy" />
      </label>
      <label>
        SSH Port
        <input v-model.number="ssh.port" type="number" :disabled="props.busy" />
      </label>
      <label>
        SSH User
        <input v-model="ssh.user" :disabled="props.busy" />
      </label>
      <label>
        SSH Password
        <input v-model="ssh.password" type="password" :disabled="props.busy" />
      </label>
      <label>
        SSH Timeout(ms)
        <input v-model.number="ssh.timeout_ms" type="number" :disabled="props.busy" />
      </label>
    </div>
  </section>
</template>
