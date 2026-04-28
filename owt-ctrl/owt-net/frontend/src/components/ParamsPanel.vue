<script setup>
const props = defineProps({
  wol: {
    type: Object,
    default: () => ({
      mac: 'AA:BB:CC:DD:EE:FF',
      broadcast: '192.168.1.255',
      port: 9
    })
  },
  ssh: {
    type: Object,
    default: () => ({
      host: '192.168.1.10',
      port: 22,
      user: 'root',
      password: '',
      timeout_ms: 5000
    })
  },
  busy: { type: Boolean, default: false }
})

const emit = defineEmits(['update:wol', 'update:ssh', 'load', 'save'])

function parseIntOrFallback(value, fallback) {
  const parsed = Number.parseInt(String(value), 10)
  return Number.isFinite(parsed) ? parsed : fallback
}

function updateWolField(key, value) {
  emit('update:wol', { ...props.wol, [key]: value })
}

function updateSshField(key, value) {
  emit('update:ssh', { ...props.ssh, [key]: value })
}
</script>

<template>
  <div class="params-grid">
    <article class="params-group">
      <h3>Wake-on-LAN</h3>
      <div class="fields">
        <label class="field full">
          <span>MAC</span>
          <input
            :value="props.wol.mac"
            placeholder="AA:BB:CC:DD:EE:FF"
            :disabled="props.busy"
            @input="updateWolField('mac', $event.target.value)"
          />
        </label>
        <label class="field">
          <span>广播地址</span>
          <input
            :value="props.wol.broadcast"
            placeholder="192.168.1.255"
            :disabled="props.busy"
            @input="updateWolField('broadcast', $event.target.value)"
          />
        </label>
        <label class="field">
          <span>端口</span>
          <input
            :value="props.wol.port"
            type="number"
            min="1"
            max="65535"
            :disabled="props.busy"
            @input="updateWolField('port', parseIntOrFallback($event.target.value, props.wol.port))"
          />
        </label>
      </div>
    </article>

    <article class="params-group">
      <h3>远程控制（SSH）</h3>
      <div class="fields">
        <label class="field full">
          <span>主机 IP</span>
          <input
            :value="props.ssh.host"
            placeholder="192.168.1.10"
            :disabled="props.busy"
            @input="updateSshField('host', $event.target.value)"
          />
        </label>
        <label class="field">
          <span>SSH 端口</span>
          <input
            :value="props.ssh.port"
            type="number"
            min="1"
            max="65535"
            :disabled="props.busy"
            @input="updateSshField('port', parseIntOrFallback($event.target.value, props.ssh.port))"
          />
        </label>
        <label class="field">
          <span>用户</span>
          <input
            :value="props.ssh.user"
            placeholder="root"
            :disabled="props.busy"
            @input="updateSshField('user', $event.target.value)"
          />
        </label>
        <label class="field full">
          <span>密码</span>
          <input
            :value="props.ssh.password"
            type="password"
            placeholder="可留空"
            :disabled="props.busy"
            @input="updateSshField('password', $event.target.value)"
          />
        </label>
        <label class="field full">
          <span>超时(ms)</span>
          <input
            :value="props.ssh.timeout_ms"
            type="number"
            min="100"
            :disabled="props.busy"
            @input="updateSshField('timeout_ms', parseIntOrFallback($event.target.value, props.ssh.timeout_ms))"
          />
        </label>
      </div>
    </article>

    <div class="row params-actions">
      <button class="ghost" type="button" :disabled="props.busy" @click="emit('load')">读取</button>
      <button type="button" :disabled="props.busy" @click="emit('save')">保存</button>
    </div>
  </div>
</template>
