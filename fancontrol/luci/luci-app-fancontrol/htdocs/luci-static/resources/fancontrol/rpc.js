'use strict';
'require rpc';

const callScan = rpc.declare({
	object: 'luci.fancontrol',
	method: 'scan'
});

const callLoadBoardConfig = rpc.declare({
	object: 'luci.fancontrol',
	method: 'loadBoardConfig'
});

const callLoadBoardDefaults = rpc.declare({
	object: 'luci.fancontrol',
	method: 'loadBoardDefaults'
});

const callApplyBoardConfig = rpc.declare({
	object: 'luci.fancontrol',
	method: 'applyBoardConfig',
	params: [
		'interval', 'control_mode', 'pwm_path', 'pwm_enable_path', 'thermal_mode_path',
		'pwm_min', 'pwm_max', 'pwm_inverted', 'ramp_up', 'ramp_down',
		'hysteresis_mC', 'policy', 'failsafe_pwm', 'entries'
	]
});

const callServiceAction = rpc.declare({
	object: 'luci.fancontrol',
	method: 'serviceAction',
	params: [ 'action' ]
});


const callRuntimeStatus = rpc.declare({
	object: 'luci.fancontrol',
	method: 'runtimeStatus'
});

function probeQmodem() {
	return rpc.list('qmodem').then((signatures) => {
		return !!(signatures && signatures.qmodem && signatures.qmodem.get_temperature);
	}).catch(() => false);
}

function mapSettledLoad(results) {
	const data = {
		scan: {},
		loadedBoard: {},
		boardDefaults: {},
		runtimeState: {},
		hasQmodem: false,
		failures: []
	};

	const fields = [ 'scan', 'loadedBoard', 'boardDefaults', 'runtimeState', 'hasQmodem' ];
	for (let i = 0; i < fields.length; i++) {
		const key = fields[i];
		const item = results[i];
		if (item && item.status === 'fulfilled')
			data[key] = item.value;
		else
			data.failures.push(key);
	}

	if (data.hasQmodem !== true)
		data.hasQmodem = false;

	return data;
}

function loadInitial() {
	return Promise.allSettled([
		callScan(),
		callLoadBoardConfig(),
		callLoadBoardDefaults(),
		callRuntimeStatus(),
		probeQmodem()
	]).then(mapSettledLoad);
}

function applyBoardConfig(payload) {
	const p = payload || {};
	return callApplyBoardConfig(
		p.interval,
		p.control_mode,
		p.pwm_path,
		p.pwm_enable_path,
		p.thermal_mode_path,
		p.pwm_min,
		p.pwm_max,
		p.pwm_inverted,
		p.ramp_up,
		p.ramp_down,
		p.hysteresis_mC,
		p.policy,
		p.failsafe_pwm,
		p.entries
	);
}

return L.Class.extend({
	mapSettledLoad: mapSettledLoad,
	loadInitial: loadInitial,
	loadBoardConfig: callLoadBoardConfig,
	loadBoardDefaults: callLoadBoardDefaults,
	runtimeStatus: callRuntimeStatus,
	serviceAction: callServiceAction,
	applyBoardConfig: applyBoardConfig
});
