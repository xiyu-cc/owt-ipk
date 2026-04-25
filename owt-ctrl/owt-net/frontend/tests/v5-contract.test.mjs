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
  'session.unsubscribe',
  'agent.list',
  'agent.get',
  'params.get',
  'params.update',
  'command.submit',
  'command.get',
  'command.list',
  'audit.list'
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

const busSocket = readFileSync(new URL('../src/composables/useCommandBusSocket.js', import.meta.url), 'utf8')
assert.match(busSocket, /path = WS_UI_PATH/)
assert.match(busSocket, /kind: BUS_KINDS\.ACTION/)
assert.match(busSocket, /msg\.kind === BUS_KINDS\.EVENT/)

console.log('frontend v5 contract tests passed')
