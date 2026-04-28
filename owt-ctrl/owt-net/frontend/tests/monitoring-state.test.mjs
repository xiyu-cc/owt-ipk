import assert from 'node:assert/strict'

import {
  monitoringEnabledFromStats,
  monitoringOverrideFromCommandResource,
  shouldClearMonitoringOverride
} from '../src/monitoring-state.js'

assert.equal(monitoringEnabledFromStats(null), null, 'null stats should be unknown')
assert.equal(monitoringEnabledFromStats({}), null, 'missing field should be unknown')
assert.equal(
  monitoringEnabledFromStats({ monitoring_enabled: true }),
  true,
  'true should be parsed'
)
assert.equal(
  monitoringEnabledFromStats({ monitoring_enabled: false }),
  false,
  'false should be parsed'
)

const override = monitoringOverrideFromCommandResource({
  command: {
    command_type: 'monitoring_set',
    agent_mac: 'AA:00:00:00:90:01'
  },
  event: {
    status: 'succeeded',
    detail: {
      result: {
        enabled: false
      }
    }
  }
})
assert.deepEqual(
  override,
  { agentMac: 'AA:00:00:00:90:01', enabled: false },
  'monitoring_set succeeded should produce override'
)

assert.equal(
  monitoringOverrideFromCommandResource({
    command: { command_type: 'host_reboot', agent_mac: 'AA:00:00:00:90:01' },
    event: { status: 'succeeded', detail: { result: { enabled: true } } }
  }),
  null,
  'non-monitoring command should not produce override'
)

assert.equal(
  shouldClearMonitoringOverride(
    {
      reason: 'agent_heartbeat',
      resource: {
        agent_mac: 'AA:00:00:00:90:01',
        stats: { monitoring_enabled: true }
      }
    },
    'AA:00:00:00:90:01'
  ),
  true,
  'heartbeat update should clear override for same agent'
)

assert.equal(
  shouldClearMonitoringOverride(
    {
      reason: 'command_result',
      resource: {
        agent_mac: 'AA:00:00:00:90:01',
        stats: { monitoring_enabled: true }
      }
    },
    'AA:00:00:00:90:01'
  ),
  false,
  'non-heartbeat update should not clear override'
)

console.log('monitoring-state.test passed')
