'use strict';

const DEFAULTS = {
	CONFIG_PATH: '/etc/fancontrol.conf',
	MODEM_SECTION: '2_1',
	PWM_PATH: '/sys/class/hwmon/hwmon2/pwm1',
	PWM_ENABLE_PATH: '/sys/class/hwmon/hwmon2/pwm1_enable',
	NVME_TEMP_PATH: '/sys/class/nvme/nvme0/hwmon1/temp1_input',
	THERMAL_MODE_PATH: '/sys/class/thermal/thermal_zone0/mode'
};

function intInRange(value, fallback, minv, maxv) {
	let n = parseInt(value, 10);

	if (isNaN(n))
		n = fallback;
	if (n < minv)
		n = minv;
	if (n > maxv)
		n = maxv;

	return n;
}

function safeField(value) {
	return String(value != null ? value : '').replace(/[;\r\n]/g, '').trim();
}

function relToAbsPath(path) {
	const p = safeField(path);

	if (!p)
		return '';
	if (p.charAt(0) === '/')
		return p;
	if (p.indexOf('hwmon') === 0)
		return '/sys/class/hwmon/' + p;

	return p;
}

function formatTempC(tempMC) {
	const n = parseInt(tempMC, 10);
	if (isNaN(n))
		return '-';
	return (n / 1000).toFixed(1) + '°C';
}

function defaultPwmPaths(channels) {
	let fallbackPath = '';

	if (Array.isArray(channels)) {
		for (let i = 0; i < channels.length; i++) {
			const c = channels[i] || {};
			const rel = safeField(c.pwm_rel || '');
			if (!rel)
				continue;

			const abs = '/sys/class/hwmon/' + rel;
			if (!fallbackPath)
				fallbackPath = abs;
			if (rel.indexOf('hwmon2/') === 0) {
				fallbackPath = abs;
				break;
			}
		}
	}

	const pwmPath = fallbackPath || DEFAULTS.PWM_PATH;
	return {
		pwmPath: pwmPath,
		pwmEnablePath: pwmPath + '_enable'
	};
}

function defaultSources(channels, hasQmodem) {
	const out = [];
	let nvmePath = '';

	for (let i = 0; i < channels.length; i++) {
		const c = channels[i] || {};
		const dev = String(c.devname || '').toLowerCase();
		if (!nvmePath && dev.indexOf('nvme') >= 0)
			nvmePath = relToAbsPath(c.temp_rel || '');
	}

	if (!nvmePath)
		nvmePath = DEFAULTS.NVME_TEMP_PATH;

	out.push({
		enabled: true,
		id: 'soc',
		type: 'sysfs',
		path: '/sys/class/thermal/thermal_zone0/temp',
		object: '',
		method: '',
		key: '',
		args: '{}',
		t_start: 60000,
		t_full: 82000,
		t_crit: 90000,
		ttl: 6,
		poll: 1,
		weight: 100
	});

	out.push({
		enabled: true,
		id: 'nvme',
		type: 'sysfs',
		path: nvmePath,
		object: '',
		method: '',
		key: '',
		args: '{}',
		t_start: 50000,
		t_full: 70000,
		t_crit: 80000,
		ttl: 6,
		poll: 1,
		weight: 120
	});

	out.push({
		enabled: hasQmodem !== false,
		id: 'rm500q-gl',
		type: 'ubus',
		path: '',
		object: 'qmodem',
		method: 'get_temperature',
		key: 'temp_mC',
		args: '{"config_section":"' + DEFAULTS.MODEM_SECTION + '"}',
		t_start: 58000,
		t_full: 76000,
		t_crit: 85000,
		ttl: 20,
		poll: 10,
		weight: 130
	});

	return out;
}

function collectEntriesFromRows(rows) {
	const lines = [];
	let active = 0;

	for (let i = 0; i < rows.length; i++) {
		const row = rows[i] || {};
		const enabled = row.enabled && row.enabled.checked ? '1' : '0';
		const sid = safeField(row.id && row.id.value) || ('source' + (i + 1));
		const type = safeField(row.type && row.type.value ? row.type.value : 'sysfs').toLowerCase();
		const sourcePath = safeField(row.path && row.path.value);
		const object = safeField(row.object && row.object.value);
		const method = safeField(row.method && row.method.value);
		const key = safeField(row.key && row.key.value);
		const args = safeField(row.args && row.args.value ? row.args.value : '{}') || '{}';

		const tStart = intInRange(row.tStart && row.tStart.value, 60000, 10000, 200000);
		const tFull = intInRange(row.tFull && row.tFull.value, 80000, 10000, 220000);
		const tCrit = intInRange(row.tCrit && row.tCrit.value, 90000, 10000, 250000);
		const ttl = intInRange(row.ttl && row.ttl.value, 10, 1, 3600);
		const poll = intInRange(row.poll && row.poll.value, 2, 1, 3600);
		const weight = intInRange(row.weight && row.weight.value, 100, 1, 200);

		if (enabled === '1')
			active++;

		lines.push([
			enabled,
			sid,
			type,
			sourcePath,
			object,
			method,
			key,
			args,
			String(tStart),
			String(tFull),
			String(tCrit),
			String(ttl),
			String(poll),
			String(weight)
		].join(';'));
	}

	return {
		lines: lines,
		active: active
	};
}

return L.Class.extend({
	DEFAULTS: DEFAULTS,
	intInRange: intInRange,
	safeField: safeField,
	relToAbsPath: relToAbsPath,
	formatTempC: formatTempC,
	defaultPwmPaths: defaultPwmPaths,
	defaultSources: defaultSources,
	collectEntriesFromRows: collectEntriesFromRows
});
