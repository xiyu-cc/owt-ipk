import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import {
  COMMAND_BUS_ACTIONS,
  COMMAND_BUS_EVENTS,
  WS_UI_PATH
} from '../src/protocol/v5.js'

assert.equal(WS_UI_PATH, '/ws/v5/ui')

assert.deepEqual(Object.values(COMMAND_BUS_ACTIONS), [
  'session.subscribe',
  'params.get',
  'params.update',
  'command.submit'
])

assert.deepEqual(Object.values(COMMAND_BUS_EVENTS), [
  'agent.snapshot',
  'agent.update',
  'command.event'
])

const appVue = readFileSync(new URL('../src/App.vue', import.meta.url), 'utf8')
assert.match(appVue, /useCommandBusSocket\(WS_UI_PATH\)/)
assert.match(appVue, /COMMAND_BUS_ACTIONS\.COMMAND_SUBMIT/)
assert.match(appVue, /COMMAND_BUS_EVENTS\.COMMAND_EVENT/)
assert.match(appVue, /scope:\s*'agent'/)
assert.doesNotMatch(appVue, /scope:\s*'all'/)
assert.doesNotMatch(appVue, /Command Bus v5 控制台，使用稳定版视觉与布局。/)
assert.doesNotMatch(appVue, /ws:\s*\{\{\s*bus\.connected\s*\?\s*'connected'\s*:\s*'disconnected'\s*\}\}/)
assert.doesNotMatch(appVue, /@refresh=/)
assert.doesNotMatch(appVue, /refreshAgents\(/)

const agentPanelVue = readFileSync(new URL('../src/components/AgentPanel.vue', import.meta.url), 'utf8')
assert.doesNotMatch(agentPanelVue, /刷新/)
assert.doesNotMatch(agentPanelVue, /ws:\s*\{\{/)
assert.doesNotMatch(agentPanelVue, /defineEmits\(\['update:selectedAgentMac',\s*'refresh'\]\)/)

const busSocket = readFileSync(new URL('../src/composables/useCommandBusSocket.js', import.meta.url), 'utf8')
assert.match(busSocket, /path = WS_UI_PATH/)
assert.match(busSocket, /kind: BUS_KINDS\.ACTION/)
assert.match(busSocket, /msg\.kind === BUS_KINDS\.EVENT/)

console.log('frontend v5 contract tests passed')
