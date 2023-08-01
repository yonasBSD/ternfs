// Snapshot edges
// --------------------------------------------------------------------

export function snapshotEdges() {
    function toggleFullEdges(show) {
        const els = document.getElementsByClassName('edges-full');
        for (let i = 0; i < els.length; i++) {
            els[i].style.display = show ? '' : 'none';
        }
        if (show) {
            localStorage.setItem('edges-full', show);
        } else {
            localStorage.removeItem('edges-full');
        }
    }
    window.addEventListener('DOMContentLoaded', () => {
        const enabled = localStorage.getItem('edges-full');
        toggleFullEdges(enabled);
        const checkbox = document.getElementById('snapshot-edges-checkbox')
        checkbox.checked = enabled;
    });
}

// stats
// --------------------------------------------------------------------

export function stats() {
    function h(tag, attrs, ...children) {
        const el = document.createElement(tag);
        // set attrs
        const setAttr = (obj, k, v) => {
            if (typeof v === 'object') {
                Object.entries(v).forEach(([k1, v1]) => setAttr(obj[k], k1, v1));
            } else {
                obj[k] = v;
            }
        }
        Object.entries(attrs).forEach(([k, v]) => setAttr(el, k, v));
        // add children
        const visit = (c) => {
            if (Array.isArray(c)) {
                c.forEach(visit);
            } else if (typeof c === 'string') {
                el.appendChild(document.createTextNode(c));
            } else {
                el.appendChild(c);
            }
        };
        children.forEach(visit);
        return el;
    }

    // Function to format a Date into the 'yyyy-mm-ddThh:mm' format required by datetime-local inputs
    function formatLocalDate(date) {
        const year = date.getFullYear();
        const month = ('0' + (date.getMonth()+1)).slice(-2); // months are zero-based in JavaScript
        const day = ('0' + date.getDate()).slice(-2);
        const hours = ('0' + date.getHours()).slice(-2);
        const minutes = ('0' + date.getMinutes()).slice(-2);
        return `${year}-${month}-${day}T${hours}:${minutes}`;
    }
    
    function nsToLocalDate(ns) {
        return formatLocalDate(new Date(Number(ns/1000000n)));
    }
    
    const chartsEl = document.getElementById('charts');

    const twoDec = (x) => (Math.round(x*100) / 100).toFixed(2);
    
    function formatNs(ns) {
        let scale = 1n;
        const next = (unit, scaleFactor) => {
            if (ns < scale*scaleFactor) {
                return twoDec(Number(ns)/Number(scale)) + unit;
            }
            scale *= scaleFactor;
            return null;
        };
        let units = [
            ['ns', 1000n],
            ['us', 1000n],
            ['ms', 1000n],
            ['s',  60n],
            ['m',  60n],
            ['h',  100000000000n],
        ]
        for (const [unit, factor] of units) {
            const res = next(unit, factor);
            if (res) { return res; }
        }
        throw 'Impossible';
    }
    
    function drawChart(req, timings) {
        const chartEl = document.createElement('canvas');
        new window.Chart(chartEl, {
            type: 'line',
            data: {
                labels: timings.histogram.map(({upperBoundNs}) => `< ${formatNs(upperBoundNs)}`),
                datasets: [{ label: req, data: timings.histogram.map(({count}) => Number(count)), tension: 0.2 }],
            },
            options: { plugins: { legend: { display: false } } },
        });
        return chartEl;
    }

    function nsToRFC3339Nano(ns) {
        const msBigInt = ns / BigInt(1000000);
        const msNumber = Number(msBigInt);
        const date = new Date(msNumber);    
        const nsStr = Number(ns % BigInt(1000000)).toString().padStart(6, '0');
        const dateStr = date.toISOString().replace('Z', '');
        return `${dateStr}${nsStr}Z`;
    }

    async function getStats(startName, startTime, endTime) {
        console.log(`fetching startName=${startName} startTime=${startTime} endTime=${endTime}`)
        const response = await fetch('/api/GET_STATS', {
            method: 'POST',
            headers: { 'Accept': 'application/octet-stream' },
            body: JSON.stringify({
                StartTime: nsToRFC3339Nano(startTime),
                StartName: startName,
                EndTime: nsToRFC3339Nano(endTime),
            })
        });
        
        if (!response.ok) {
            throw new Error("HTTP error " + response.status);
        }
        
        const buffer = await response.arrayBuffer();
        return new Uint8Array(buffer);
    }

    function* parseStats(buf) {
        const decoder = new TextDecoder('ascii');
        const view = new DataView(buf.buffer);
        let cur = 0;
        const nextTime = view.getBigUint64(cur, true); cur += 8;
        const nextNameLen = buf[cur]; cur++;
        const nextName = decoder.decode(buf.subarray(cur, cur+nextNameLen)); cur += nextNameLen;
        let statsLen = view.getUint16(cur, true); cur += 2;
        for (let i = 0; i < statsLen; i++) {
            const nameLen = buf[cur]; cur++;
            const name = decoder.decode(buf.subarray(cur, cur+nameLen)); cur += nameLen;
            const time = view.getBigUint64(cur, true); cur += 8;
            const valueLen = view.getUint16(cur, true); cur += 2;
            const value = buf.subarray(cur, cur+valueLen); cur += valueLen;
            yield { name, time, value };
        }
        return { nextName, nextTime };
    }

    const chartsFigures = ['count', 'ops/s', 'mean', 'p50', 'p90', 'p99'];
    const allFigures = [...chartsFigures, 'first', 'last'];

    const urlParams = new URLSearchParams(window.location.search);

    const timeFromEl = document.getElementById('time-from');
    const timeToEl = document.getElementById('time-to');

    // set initial times (now-24hrs and now)
    const urlTimeFrom = urlParams.get('timeFrom');
    if (urlTimeFrom) {
        timeFromEl.value = nsToLocalDate(BigInt(urlTimeFrom));
    } else {
        timeFromEl.value = formatLocalDate(new Date(new Date().getTime() - 24 * 60 * 60 * 1000));
    }
    const urlTimeTo = urlParams.get('timeTo');
    if (urlTimeTo) {
        timeToEl.value = nsToLocalDate(BigInt(urlTimeTo));
    } else {
        timeToEl.value = formatLocalDate(new Date());
    }
    
    const sortSelectEl = document.getElementById('sort-select');
    const selectedSort = urlParams.get('sortBy') || 'name';
    for (const f of ['name', ...allFigures]) {
        sortSelectEl.appendChild(h('option', { selected: f === selectedSort, value: f }, f));
    }

    const chartsCheckedEl = document.getElementById('charts-check');
    chartsCheckedEl.checked = urlParams.get('showCharts') !== null;

    const statsFilterEl = document.getElementById('stats-filter');
    statsFilterEl.value = urlParams.get('filter') || '';
    
    let cachedData = { timeFrom: 0n, timeTo: 0n, stats: {} };

    async function loadData(timeFrom, timeTo) {
        if (timeFrom === cachedData.timeFrom && timeTo === cachedData.timeTo) {
            return cachedData.stats;
        }

        const data = { stats: {}, timeFrom, timeTo };

        chartsEl.innerHTML = '<em>Loading...</em>';

        // fetch histos
        let startTime = timeFrom;
        let startName = "";
        const endTime = timeTo;
        for (let i = 0; i < 10; i++) {
            let respBuf;
            try {
                respBuf = await getStats(startName, startTime, endTime);
            } catch (error) {
                chartsEl.innerHTML = `Error fetching stats: ${error}`;
                return;
            }
            const stats = parseStats(respBuf);
            for (;;) {
                const n = stats.next();
                if (n.done) {
                    startName = n.value.nextName;
                    startTime = n.value.nextTime;
                    break;
                }
                const stat = n.value;
                let name = stat.name;
                const segments = stat.name.split('.');
                if (segments[0] === 'shard') { // group all shards into one bunch
                    name = segments[0] + '.' + segments.slice(2).join('.');
                }
                data.stats[name] = data.stats[name] || {
                    histogram: [],
                    elapsedNs: BigInt(0),
                    firstStat: 1n<<64n,
                    lastStat: 0n,
                };
                if (stat.time > data.stats[name].lastStat) {
                    data.stats[name].lastStat = stat.time;
                }
                if (stat.time < data.stats[name].firstStat) {
                    data.stats[name].firstStat = stat.time;
                }
                // parse histo
                const valueView = new DataView(stat.value.buffer, stat.value.byteOffset, stat.value.byteLength);
                if (stat.value.length < 8 || (stat.value.length-8)%16 !== 0) {
                    throw `Unexpected histo length ${stat.value.length}`;
                }
                data.stats[name].elapsedNs += valueView.getBigUint64(0, true);
                const histogram = data.stats[name].histogram;
                for (let i = 8, bin = 0; i < stat.value.length; i += 8+8, bin++) {
                    const upperBoundNs = valueView.getBigUint64(i, true);
                    const count = valueView.getBigUint64(i+8, true);
                    histogram[bin] = histogram[bin] || { upperBoundNs, count: BigInt(0) };
                    if (histogram[bin].upperBoundNs !== upperBoundNs) {
                        throw `Differing upper bounds for ${stat.name} at ${bin}, ${histogram[bin].upperBoundNs} != ${upperBoundNs}`;
                    }
                    histogram[bin].count += count;
                }
            }
            if (startName === "") { break; }
        }
    
        // trim histos, replace undefineds with zero
        for (const reqData of Object.values(data.stats)) {
            let skipStart = 0;
            for (; skipStart < reqData.histogram.length && !reqData.histogram[skipStart].count; skipStart++) {}
            skipStart = skipStart == 0 ? 0 : skipStart-1;
            let skipEnd = 0;
            for (; skipEnd < reqData.histogram.length && !reqData.histogram[reqData.histogram.length-skipEnd-1].count; skipEnd++) {}
            skipEnd = skipEnd == 0 ? 0 : skipEnd-1;
            reqData.histogram = reqData.histogram.slice(skipStart, reqData.histogram.length-skipEnd);
        }

        // compute figures
        for (const stat of Object.values(data.stats)) {
            stat.figures = {};

            stat.figures.first = { sort: stat.firstStat, text: nsToLocalDate(stat.firstStat) };
            stat.figures.last = { sort: stat.lastStat, text: nsToLocalDate(stat.lastStat) };

            let totalCount = 0n; stat.histogram.forEach(({count}) => { totalCount += count });
            stat.figures.count = { sort: totalCount, text: `${totalCount.toLocaleString()}` };

            const zeroIfNoCount = (k, z, f) => {
                if (totalCount > 0) {
                    stat.figures[k] = f();
                } else {
                    stat.figures[k] = { sort: z, text: '0' };
                }
            };
            const bigZeroIfNoCount = (k, f) => zeroIfNoCount(k, 0n, f);
            const floatZeroIfNoCount = (k, f) => zeroIfNoCount(k, 0, f);

            floatZeroIfNoCount('ops/s', () => {
                const reqPerS = Number((totalCount * 1000000000000n) / stat.elapsedNs) / 1000.0;
                return { sort: reqPerS, text: twoDec(reqPerS) };
            });

            bigZeroIfNoCount('mean', () => {
                let mean = 0n;
                for (const { count, upperBoundNs } of stat.histogram) {
                    mean += count * upperBoundNs;
                }
                mean /= totalCount;
                return { sort: mean, text: formatNs(mean) };
            });

            const percentile = (name, p) => {
                bigZeroIfNoCount(name, () => {
                    let countSoFar = 0n;
                    let u = 0n;
                    const toReach = (totalCount*p)/100n;
                    for (const { count, upperBoundNs } of stat.histogram) {
                        countSoFar += count;
                        u = upperBoundNs;
                        if (countSoFar >= toReach) {
                            break;
                        }
                    }
                    return { sort: u, text: formatNs(u) };
                })    
            }
            percentile('p50', 50n);
            percentile('p90', 90n);
            percentile('p99', 99n);
        }

        cachedData = data;
        return data.stats;
    }

    async function reload() {
        // get drawing params
        const timeFrom = BigInt(new Date(timeFromEl.value)) * BigInt(1000000);
        const timeTo = BigInt(new Date(timeToEl.value)) * BigInt(1000000);
        const sortByFigure = sortSelectEl.options[sortSelectEl.selectedIndex].value;
        const sortBy = ([n1, d1], [n2, d2]) => {
            if (sortByFigure === 'name') {
                return n1.localeCompare(n2);
            } else {
                const d = d2.figures[sortByFigure].sort - d1.figures[sortByFigure].sort;
                return (d < 0) ? -1 : (d > 0) ? 1 : 0; // avoid bigint -> float
            }
        };
        const showCharts = chartsCheckedEl.checked;
        const statsFilterStr = statsFilterEl.value;
        const statsFilter = new RegExp(statsFilterStr);

        // bake them into url
        const urlParams = new URLSearchParams();
        urlParams.set('timeFrom', timeFrom.toString());
        urlParams.set('timeTo', timeTo.toString());
        urlParams.set('sortBy', sortByFigure);
        showCharts && urlParams.set('showCharts', '');
        urlParams.set('filter', statsFilterStr);
        history.replaceState({}, '', window.location.pathname + '?' + urlParams.toString());

        // Load data
        const data = await loadData(timeFrom, timeTo);

        // now draw the divs
        chartsEl.innerHTML = '';
        const sortedData = Object.entries(data).filter(([k, _]) => k.match(k.match(statsFilter))).sort(sortBy);
        if (showCharts) {
            chartsEl.appendChild(h(
                'div', 
                { style: { display: 'grid', gridTemplateColumns: '50% 50%' } },
                sortedData.map(([stat, statData]) => h(
                    'div',
                    { style: { display: 'grid', gridTemplateRows: 'auto auto auto', width: '100%' }},
                    h('h5', { className: 'font-monospace mb-1' }, stat),
                    h(
                        'table',
                        { className: 'table mb-1', style: { justifySelf: 'start', width: 'auto' } },
                        h('tr', {}, chartsFigures.map(figure => h('th', { className: 'px-1' }, figure))),
                        h('tr', {}, chartsFigures.map(figure => h('td', { className: 'px-1' }, statData.figures[figure].text))),
                    ),
                    drawChart(stat, statData)
                )),
            ));    
        } else {
            chartsEl.appendChild(h(
                'table', 
                { className: 'table' },
                h('thead', {}, h('tr', {}, h('th', {}, '#'), allFigures.map(figure => h('th', {}, figure)))),
                h('tbody', {},
                    sortedData.map(([stat, statData]) => h(
                        'tr', {},
                        h('th', {}, h('h5', { className: 'font-monospace' }, stat)),
                        allFigures.map(figure => h('td', {}, statData.figures[figure].text)),
                    ),
                ))
            ));
        }

        console.log('done drawing');
    }

    // quick range change
    const timeRangeChange = (id, msDiff) => {
        const el = document.getElementById(id);
        el.addEventListener('click', () => {
            timeFromEl.value = formatLocalDate(new Date(new Date().getTime() - msDiff));
            timeToEl.value = formatLocalDate(new Date());
        });
    }
    timeRangeChange('load-3h',  1000 * 60 * 60 * 3);
    timeRangeChange('load-6h',  1000 * 60 * 60 * 6);
    timeRangeChange('load-12h', 1000 * 60 * 60 * 12);
    timeRangeChange('load-1d',  1000 * 60 * 60 * 24);
    timeRangeChange('load-7d',  1000 * 60 * 60 * 24 * 7);

    // redraw on submit
    const formEl = document.getElementById('time-selection');
    formEl.addEventListener('submit', (el) => {
        el.preventDefault();
        reload();
    })
    for (const el of [statsFilterEl, sortSelectEl, chartsCheckedEl]) {
        el.addEventListener('input', reload);
    }

    reload();
}