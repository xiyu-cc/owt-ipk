import assert from 'node:assert/strict'

import { useCommandBusSocket } from '../src/composables/useCommandBusSocket.js'
import {
  BUS_KINDS,
  BUS_VERSION,
  COMMAND_BUS_ACTIONS,
  WS_UI_PATH
} from '../src/protocol/v5.js'

class FakeWebSocket {
  static CONNECTING = 0
  static OPEN = 1
  static CLOSING = 2
  static CLOSED = 3
  static instances = []

  constructor(url) {
    this.url = url
    this.readyState = FakeWebSocket.CONNECTING
    this.sent = []
    this.onopen = null
    this.onmessage = null
    this.onclose = null
    this.onerror = null
    FakeWebSocket.instances.push(this)
  }

  send(data) {
    this.sent.push(data)
  }

  close() {
    this.readyState = FakeWebSocket.CLOSED
    if (typeof this.onclose === 'function') {
      this.onclose()
    }
  }

  emitOpen() {
    this.readyState = FakeWebSocket.OPEN
    if (typeof this.onopen === 'function') {
      this.onopen()
    }
  }

  emitMessage(data) {
    if (typeof this.onmessage === 'function') {
      this.onmessage({ data })
    }
  }
}

globalThis.window = {
  location: { href: 'http://127.0.0.1:3000/dashboard' }
}
globalThis.WebSocket = FakeWebSocket

const bus = useCommandBusSocket(WS_UI_PATH)
bus.connect()
assert.equal(FakeWebSocket.instances.length, 1, 'socket should be created')

const ws = FakeWebSocket.instances[0]
ws.emitOpen()

const pending = bus.call(COMMAND_BUS_ACTIONS.COMMAND_SUBMIT, {
  agent_mac: 'AA:00:00:00:90:01',
  command_type: 'monitoring_set',
  payload: { enabled: true },
  timeout_ms: 5000,
  max_retry: 1
})

assert.equal(ws.sent.length, 1, 'single command submit should send one ws message')
const outbound = JSON.parse(ws.sent[0])
assert.equal(outbound.v, BUS_VERSION, 'outbound protocol version mismatch')
assert.equal(outbound.kind, BUS_KINDS.ACTION, 'outbound kind mismatch')
assert.equal(outbound.name, COMMAND_BUS_ACTIONS.COMMAND_SUBMIT, 'outbound name mismatch')

ws.emitMessage(
  JSON.stringify({
    v: BUS_VERSION,
    kind: BUS_KINDS.RESULT,
    name: COMMAND_BUS_ACTIONS.COMMAND_SUBMIT,
    id: outbound.id,
    ts_ms: Date.now(),
    payload: { accepted: true }
  })
)

const result = await pending
assert.equal(result.accepted, true, 'submit should resolve by matching result id')
assert.equal(ws.sent.length, 1, 'submit flow should not emit duplicate action messages')

bus.close()
console.log('command-submit-single-send.test passed')
