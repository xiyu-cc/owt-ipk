'use strict';
'require fancontrol.validators as validators';

function bindButtonClick(btn, fn) {
	btn.addEventListener('click', function(ev) {
		if (ev) {
			ev.preventDefault();
			ev.stopPropagation();
		}
		return fn(ev);
	});
}

function toText(value, fallback) {
	if (value == null || value === '')
		return String(fallback != null ? fallback : '');
	return String(value);
}

function SourceTable() {
	this.rows = [];
}

SourceTable.prototype.refreshTypeFields = function(row) {
	const isSysfs = row.type.value === 'sysfs';
	const isUbus = row.type.value === 'ubus';

	row.path.disabled = !isSysfs;
	row.object.disabled = !isUbus;
	row.method.disabled = !isUbus;
	row.key.disabled = !isUbus;
	row.args.disabled = !isUbus;
};

SourceTable.prototype.createSourceRow = function(initial, tbody) {
	const enabled = E('input', { 'type': 'checkbox', 'checked': initial.enabled !== false });
	const id = E('input', {
		'class': 'cbi-input-text',
		'type': 'text',
		'value': validators.safeField(initial.id || ''),
		'style': 'min-width:7em'
	});
	const type = E('select', { 'class': 'cbi-input-select', 'style': 'min-width:6em' }, [
		E('option', { 'value': 'sysfs' }, _('sysfs')),
		E('option', { 'value': 'ubus' }, _('ubus'))
	]);
	type.value = (validators.safeField(initial.type || 'sysfs').toLowerCase() === 'ubus') ? 'ubus' : 'sysfs';
	const path = E('input', {
		'class': 'cbi-input-text',
		'type': 'text',
		'value': validators.safeField(initial.path || ''),
		'placeholder': '/sys/class/.../temp*_input',
		'style': 'min-width:16em'
	});
	const object = E('input', {
		'class': 'cbi-input-text',
		'type': 'text',
		'value': validators.safeField(initial.object || ''),
		'placeholder': 'qmodem',
		'style': 'min-width:8em'
	});
	const method = E('input', {
		'class': 'cbi-input-text',
		'type': 'text',
		'value': validators.safeField(initial.method || ''),
		'placeholder': 'get_temperature',
		'style': 'min-width:9em'
	});
	const key = E('input', {
		'class': 'cbi-input-text',
		'type': 'text',
		'value': validators.safeField(initial.key || ''),
		'placeholder': 'temp_mC',
		'style': 'min-width:7em'
	});
	const args = E('input', {
		'class': 'cbi-input-text',
		'type': 'text',
		'value': validators.safeField(initial.args || ''),
		'placeholder': '{}',
		'style': 'min-width:8em'
	});

	const tStart = E('input', { 'class': 'cbi-input-text', 'type': 'number', 'value': toText(initial.t_start, ''), 'style': 'width:7em' });
	const tFull = E('input', { 'class': 'cbi-input-text', 'type': 'number', 'value': toText(initial.t_full, ''), 'style': 'width:7em' });
	const tCrit = E('input', { 'class': 'cbi-input-text', 'type': 'number', 'value': toText(initial.t_crit, ''), 'style': 'width:7em' });
	const ttl = E('input', { 'class': 'cbi-input-text', 'type': 'number', 'value': toText(initial.ttl, ''), 'style': 'width:5em' });
	const poll = E('input', { 'class': 'cbi-input-text', 'type': 'number', 'value': toText(initial.poll, ''), 'style': 'width:5em' });
	const weight = E('input', { 'class': 'cbi-input-text', 'type': 'number', 'value': toText(initial.weight, ''), 'style': 'width:5em' });

	const removeBtn = E('button', {
		'class': 'btn cbi-button cbi-button-remove',
		'type': 'button'
	}, _('删除'));

	const statusSpan = E('span', { 'class': 'source-status' }, '-');
	const tr = E('tr', { 'class': 'tr cbi-section-table-row' }, [
		E('td', { 'class': 'td cbi-section-table-cell' }, [ enabled ]),
		E('td', { 'class': 'td cbi-section-table-cell' }, [ id ]),
		E('td', { 'class': 'td cbi-section-table-cell' }, [ statusSpan ]),
		E('td', { 'class': 'td cbi-section-table-cell' }, [ type ]),
		E('td', { 'class': 'td cbi-section-table-cell' }, [ path ]),
		E('td', { 'class': 'td cbi-section-table-cell' }, [ object ]),
		E('td', { 'class': 'td cbi-section-table-cell' }, [ method ]),
		E('td', { 'class': 'td cbi-section-table-cell' }, [ key ]),
		E('td', { 'class': 'td cbi-section-table-cell' }, [ args ]),
		E('td', { 'class': 'td cbi-section-table-cell' }, [ tStart ]),
		E('td', { 'class': 'td cbi-section-table-cell' }, [ tFull ]),
		E('td', { 'class': 'td cbi-section-table-cell' }, [ tCrit ]),
		E('td', { 'class': 'td cbi-section-table-cell' }, [ ttl ]),
		E('td', { 'class': 'td cbi-section-table-cell' }, [ poll ]),
		E('td', { 'class': 'td cbi-section-table-cell' }, [ weight ]),
		E('td', { 'class': 'td cbi-section-table-cell' }, [ removeBtn ])
	]);

	const row = {
		enabled,
		id,
		type,
		path,
		object,
		method,
		key,
		args,
		tStart,
		tFull,
		tCrit,
		ttl,
		poll,
		weight,
		status: statusSpan,
		removeBtn,
		tr
	};

	bindButtonClick(removeBtn, () => {
		tbody.removeChild(tr);
		this.rows = this.rows.filter((r) => r !== row);
	});

	type.addEventListener('change', () => {
		this.refreshTypeFields(row);
	});

	this.refreshTypeFields(row);
	this.rows.push(row);
	tbody.appendChild(tr);
};

SourceTable.prototype.fillRows = function(sourceList, tbody) {
	while (tbody.firstChild)
		tbody.removeChild(tbody.firstChild);
	this.rows = [];

	for (let i = 0; i < sourceList.length; i++)
		this.createSourceRow(sourceList[i], tbody);
};

SourceTable.prototype.ensureRows = function(refs, initialSources) {
	if (!refs || !refs.tbody)
		return;
	if (this.rows.length > 0 && refs.tbody.children.length > 0)
		return;

	const defaults = Array.isArray(initialSources) ? initialSources : [];
	this.fillRows(defaults, refs.tbody);
};

SourceTable.prototype.nextUniqueId = function(base) {
	const prefix = validators.safeField(base || 'source') || 'source';
	const used = Object.create(null);

	for (let i = 0; i < this.rows.length; i++) {
		const row = this.rows[i];
		if (!row || !row.id)
			continue;
		const id = validators.safeField(row.id.value || '');
		if (id)
			used[id] = true;
	}

	if (!used[prefix])
		return prefix;
	let n = 2;
	while (used[prefix + n])
		n++;
	return prefix + n;
};

SourceTable.prototype.collectSources = function() {
	return validators.collectSourcesFromRows(this.rows);
};

SourceTable.prototype.applySourceRuntime = function(status) {
	const statusOk = !!(status && status.ok);
	const statusRunning = (status && status.running != null) ? !!status.running : true;
	const statusStale = !!(status && status.stale);
	if (!statusOk || !statusRunning || statusStale) {
		for (let i = 0; i < this.rows.length; i++) {
			const row = this.rows[i];
			if (row && row.status)
				row.status.textContent = '-';
		}
		return;
	}

	const sourceById = Object.create(null);
	const list = (status && Array.isArray(status.sources)) ? status.sources : [];
	for (let i = 0; i < list.length; i++) {
		const s = list[i] || {};
		const sid = validators.safeField(s.id || '');
		if (sid)
			sourceById[sid] = s;
	}

	for (let i = 0; i < this.rows.length; i++) {
		const row = this.rows[i];
		if (!row || !row.status)
			continue;

		const sid = validators.safeField(row.id.value || '');
		const s = sid ? sourceById[sid] : null;
		if (!s) {
			row.status.textContent = '-';
			continue;
		}

		if (s.ok && s.temp_mC != null)
			row.status.textContent = validators.formatTempC(s.temp_mC);
		else
			row.status.textContent = '-';
	}
};

SourceTable.prototype.getAllButtons = function() {
	const buttons = [];
	for (let i = 0; i < this.rows.length; i++) {
		const row = this.rows[i];
		if (row && row.removeBtn)
			buttons.push(row.removeBtn);
	}
	return buttons;
};

return L.Class.extend({
	create: function() {
		return new SourceTable();
	}
});
