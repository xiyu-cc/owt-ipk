'use strict';

function safeField(value) {
	return String(value != null ? value : '').replace(/[;\r\n]/g, '').trim();
}

function toField(value) {
	if (value == null)
		return '';
	return safeField(String(value));
}

function formatTempC(tempMC) {
	const n = parseInt(tempMC, 10);
	if (isNaN(n))
		return '-';
	return (n / 1000).toFixed(1) + '°C';
}

function collectSourcesFromRows(rows) {
	const sources = [];

	for (let i = 0; i < rows.length; i++) {
		const row = rows[i] || {};
		const enabled = row.enabled && row.enabled.checked;
		if (!enabled)
			continue;

		sources.push({
			id: toField(row.id && row.id.value),
			type: toField(row.type && row.type.value),
			path: toField(row.path && row.path.value),
			object: toField(row.object && row.object.value),
			method: toField(row.method && row.method.value),
			key: toField(row.key && row.key.value),
			args: toField(row.args && row.args.value),
			t_start: toField(row.tStart && row.tStart.value),
			t_full: toField(row.tFull && row.tFull.value),
			t_crit: toField(row.tCrit && row.tCrit.value),
			ttl: toField(row.ttl && row.ttl.value),
			poll: toField(row.poll && row.poll.value),
			weight: toField(row.weight && row.weight.value)
		});
	}

	return sources;
}

return L.Class.extend({
	safeField: safeField,
	formatTempC: formatTempC,
	collectSourcesFromRows: collectSourcesFromRows
	});
