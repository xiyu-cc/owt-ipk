import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { RPC_EVENTS, RPC_METHODS, WS_UI_PATH } from '../src/protocol/v4.js'

assert.equal(WS_UI_PATH, '/ws/v4/ui')

assert.deepEqual(Object.values(RPC_METHODS), [
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

assert.deepEqual(Object.values(RPC_EVENTS), [
  'agent.snapshot',
  'agent.update',
  'command.event'
])

const appVue = readFileSync(new URL('../src/App.vue', import.meta.url), 'utf8')
assert.match(appVue, /useRpcSocket\(WS_UI_PATH\)/)
assert.match(appVue, /RPC_METHODS\.COMMAND_SUBMIT/)
assert.match(appVue, /RPC_EVENTS\.COMMAND_EVENT/)

const rpcSocket = readFileSync(new URL('../src/composables/useRpcSocket.js', import.meta.url), 'utf8')
assert.match(rpcSocket, /path = WS_UI_PATH/)

console.log('frontend v4 contract tests passed')
