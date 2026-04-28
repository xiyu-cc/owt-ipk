function isPlainObject(value) {
  return Boolean(value) && typeof value === 'object' && !Array.isArray(value)
}

export function monitoringEnabledFromStats(stats) {
  if (!isPlainObject(stats)) {
    return null
  }
  if (stats.monitoring_enabled === true) {
    return true
  }
  if (stats.monitoring_enabled === false) {
    return false
  }
  return null
}

export function monitoringOverrideFromCommandResource(resource) {
  if (!isPlainObject(resource)) {
    return null
  }
  const command = isPlainObject(resource.command) ? resource.command : null
  const event = isPlainObject(resource.event) ? resource.event : null
  if (!command || !event) {
    return null
  }
  if (command.command_type !== 'monitoring_set') {
    return null
  }
  if (String(event.status || '').toLowerCase() !== 'succeeded') {
    return null
  }

  const agentMac = typeof command.agent_mac === 'string' ? command.agent_mac : ''
  const enabled = event?.detail?.result?.enabled
  if (!agentMac || typeof enabled !== 'boolean') {
    return null
  }
  return { agentMac, enabled }
}

export function shouldClearMonitoringOverride(updatePayload, overrideAgentMac) {
  if (!overrideAgentMac || !isPlainObject(updatePayload)) {
    return false
  }

  if (updatePayload.reason !== 'agent_heartbeat') {
    return false
  }

  const resource = isPlainObject(updatePayload.resource) ? updatePayload.resource : null
  if (!resource || resource.agent_mac !== overrideAgentMac) {
    return false
  }

  return typeof monitoringEnabledFromStats(resource.stats) === 'boolean'
}
