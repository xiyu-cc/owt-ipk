<script setup>
const props = defineProps({
  modelValue: { type: String, default: '{}' },
  busy: { type: Boolean, default: false }
})

const emit = defineEmits(['update:modelValue', 'load', 'save'])

function onInput(event) {
  emit('update:modelValue', event.target.value)
}
</script>

<template>
  <section class="card">
    <h2>Params</h2>
    <div class="actions">
      <button class="btn" type="button" :disabled="props.busy" @click="emit('load')">读取</button>
      <button class="btn" type="button" :disabled="props.busy" @click="emit('save')">保存并下发</button>
    </div>
    <textarea
      class="mono"
      rows="14"
      :value="props.modelValue"
      @input="onInput"
      :disabled="props.busy"
    />
  </section>
</template>
