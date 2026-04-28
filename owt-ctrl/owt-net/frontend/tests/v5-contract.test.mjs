import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import {
  COMMAND_BUS_ACTIONS,
  COMMAND_BUS_EVENTS,
  COMMAND_BUS_ERROR_CODES,
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

const contractHeader = readFileSync(
  new URL('../../../owt-protocol/include/owt/protocol/v5/contract.h', import.meta.url),
  'utf8'
)

function extractNamespaceBody(text, namespaceName) {
  const startToken = `namespace ${namespaceName} {`
  const endToken = `} // namespace ${namespaceName}`
  const start = text.indexOf(startToken)
  assert.notEqual(start, -1, `missing namespace ${namespaceName} in contract.h`)
  const end = text.indexOf(endToken, start)
  assert.notEqual(end, -1, `missing namespace end marker for ${namespaceName} in contract.h`)
  return text.slice(start, end)
}

function extractStringConstants(body, keyPrefix) {
  const regex = new RegExp(`inline constexpr std::string_view ${keyPrefix}\\w+ = \"([^\"]+)\";`, 'g')
  const values = []
  for (const match of body.matchAll(regex)) {
    values.push(match[1])
  }
  return values
}

const uiNamespaceBody = extractNamespaceBody(contractHeader, 'ui')
const errorCodeNamespaceBody = extractNamespaceBody(contractHeader, 'error_code')
const backendUiActions = extractStringConstants(uiNamespaceBody, 'kAction')
const backendUiEvents = extractStringConstants(uiNamespaceBody, 'kEvent')
const backendErrorCodes = extractStringConstants(errorCodeNamespaceBody, 'k')
const sortStrings = (values) => [...values].sort()

assert.deepEqual(
  sortStrings(Object.values(COMMAND_BUS_ACTIONS)),
  sortStrings(backendUiActions),
  'frontend/backend UI action constants must match'
)
assert.deepEqual(
  sortStrings(Object.values(COMMAND_BUS_EVENTS)),
  sortStrings(backendUiEvents),
  'frontend/backend UI event constants must match'
)
assert.deepEqual(
  sortStrings(Object.values(COMMAND_BUS_ERROR_CODES)),
  sortStrings(backendErrorCodes),
  'frontend/backend error code constants must match'
)

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
