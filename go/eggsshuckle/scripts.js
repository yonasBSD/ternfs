import * as p from '/static/preact-10.18.1.module.js';
import { useEffect, useState, useCallback } from '/static/preact-hooks-10.18.1.module.js';

// API to shuckle itself
// --------------------------------------------------------------------

async function shuckleReq(reqKind, req) {
    const response = await fetch(`/api/shuckle/${reqKind}`, {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        },
        body: JSON.stringify(req),
    });

    if (!response.ok) {
        throw new Error(`Bad HTTP status ${response.status}`);
    }

    const r = await response.json();
    return r.resp;
}

function idToShard(id) {
    return Number.parseInt(id.slice(-2), 16);
}

const NULL_INODE_ID = '0x0000000000000000';

function idToType(id) {
    const idN = BigInt(id);
    const typ = (idN >> 61n) & 3n;
    switch (typ) {
    case 1n:
        return 'DIRECTORY';
    case 2n:
        return 'FILE';
    case 3n:
        return 'SYMLINK';
    default:
        throw new Error(`Bad inode id ${id}`);
    }
}

async function shardReq(reqKind, shardId, req) {
    const response = await fetch(`/api/shard/${shardId}/${reqKind}`, {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        },
        body: JSON.stringify(req),
    });

    if (!response.ok) {
        throw new Error(`Bad HTTP status ${response.status}`);
    }

    const r = await response.json();
    return r.resp;
}

// Table viewer
// --------------------------------------------------------------------

function rowCells(row) {
    if (Array.isArray(row)) { return row; }
    return row.cells;
}

function rowProps(row) {
    if (Array.isArray(row)) { return {}; }
    return row.props || {};
}

function TableInner(props) {
    const { cols, rows, sortByIx: initialSortByIx, elementsPerPage: initialElementsPerPage } = props
    const paginateExtra = props.paginateExtra || [];
    const disableSearch = !!props.disableSearch;
    const filterFromSearchParams = !!props.filterFromSearchParams;

    function computePages(sortByIx, colFilters, elementsPerPage) {
        const colRegexps = colFilters.map(s => new RegExp(s));
        const filteredRows = rows.filter(row => rowCells(row).every((cell, ix) => {
            let s;
            let col = cols[ix];
            if (col.string) {
                s = col.string(cell);
            } else if (typeof cell !== 'string') {
                s = `${cell}`;
            } else {
                s = cell;
            }
            return s.match(colRegexps[ix]);
        }));
        const sortedRows = filteredRows.sort((row1, row2) => {
            for (const [ix, ascending] of sortByIx) {
                if (rowCells(row1)[ix] < rowCells(row2)[ix]) {
                    return ascending ? -1 : 1;
                } else if (rowCells(row1)[ix] > rowCells(row2)[ix]) {
                    return ascending ? 1 : -1;
                }
            }
            return 0;
        });
        const pages = [];
        for (let i = 0; i < sortedRows.length; i += elementsPerPage) {
            pages.push(sortedRows.slice(i, i + elementsPerPage));
        }
        if (pages.length === 0) {
            pages.push([]);
        }
        return { pages, numRows: sortedRows.length };
    }

    const initialColFilters = Array.from({length: cols.length}, () => '');
    if (filterFromSearchParams) {
        cols.forEach(({name}, ix) => {
            const v = new URLSearchParams(window.location.search).get(name);
            if (v !== null) {
                initialColFilters[ix] = v;
            }
        });
    }

    const [{pages, numRows, currentPage, sortByIx, colFilters, elementsPerPage}, setState] = useState({
        pages: [[]],
        numRows: 0,
        currentPage: 0,
        sortByIx: initialSortByIx,
        colFilters: initialColFilters,
        elementsPerPage: initialElementsPerPage,
    });

    // first filtering
    useEffect(() => {
        const {pages, numRows} = computePages(initialSortByIx, initialColFilters, initialElementsPerPage);
        setState(prev => ({...prev, pages, numRows}));
    }, [])

    const pageRows = pages[currentPage];
    const project = (fld) => (set) => setState(prev => {
        const next = {...prev};
        next[fld] = set(prev[fld]);
        if (fld === 'sortByIx' || fld === 'colFilters' || fld === 'elementsPerPage') {
            if (prev[fld] !== next[fld]) { // deep equal would be nicer
                const {pages, numRows} = computePages(next.sortByIx, next.colFilters, next.elementsPerPage);
                next.pages = pages;
                next.numRows = numRows;
                next.currentPage = 0;
            }
        }
        return next;
    });
    const setSortByIx = project('sortByIx');
    const setColFilters = project('colFilters');
    const setElementsPerPage = project('elementsPerPage');

    if (props.identifier) {
        useEffect(() => {
            localStorage.setItem(`table-${props.identifier}-elements-per-page`, JSON.stringify(elementsPerPage));
            localStorage.setItem(`table-${props.identifier}-sort-by-ix`, JSON.stringify(sortByIx));
        })
    }

    const setIx = (ix) => (ev) => {
        ev.preventDefault();
        setSortByIx(prev => {
            const next = [...prev];
            for (let i = 0; i < next.length; i++) {
                const [otherIx, ascending] = prev[i];
                if (ix === otherIx) {
                    next[i] = [otherIx, !ascending];
                    return next;
                }
            }
            next.push([ix, true]);
            return next;
        });
    };
    const removeIx = (ix) => (ev) => {
        ev.preventDefault();
        setSortByIx(prev => [...prev].filter(([otherIx, _]) => otherIx !== ix));
    };
    const setColFilter = (ix) => (ev) => {
        const v = ev.target.value;
        setColFilters(prev => {
            const next = [...prev];
            next[ix] = v;
            return next;
        });
    };

    const switchPage = (direction) => () => setState(prev => {
        const next = {...prev};
        next.currentPage += direction;
        next.currentPage = Math.max(0, Math.min(next.pages.length-1, next.currentPage));
        return next;
    });

    function Paginate() {
        return p.h('tr', {}, p.h('th', { className: 'input-group-sm py-1', colspan: `${cols.length}` },
            p.h('button', {type: 'button', onClick: switchPage(-1), className: 'btn btn-primary py-1', disabled: currentPage === 0}, '🡐'),
            ' ',
            p.h('button', {type: 'button', onClick: switchPage(+1), className: 'btn btn-primary py-1 me-2', disabled: currentPage >= pages.length-1}, '🡒'),
            ` ${currentPage+1} / ${pages.length} (${numRows} rows)`,
            p.h('input', {
                type: 'number', min: '1', step: '1', className: 'ms-2 form-control', style: 'width: 5rem; display: inline;', value: elementsPerPage,
                onChange: (ev) => {
                    const x = parseInt(ev.target.value);
                    setElementsPerPage(_ => x);
                },
            }),
            ' per page',
            ...paginateExtra,
        ));
    };

    const dummyRows = [];
    if (currentPage > 0) {
        for (let i = pageRows.length; i < elementsPerPage; i++) {
            dummyRows.push(p.h('tr', {}, cols.map(_ => p.h('td', {}, '\u00A0'))));
        }    
    }

    return p.h(
        'table', {className: 'table'},
        p.h('thead', {},
            p.h(Paginate),
            p.h('tr', {className: 'text-nowrap'}, cols.map(({name: c}, ix) => {
                const sortIx = sortByIx.findIndex(([otherIx, _]) => ix == otherIx);
                let sortButton;
                if (sortIx < 0) {
                    sortButton = p.h(
                        'a',
                        {
                            className: 'text-muted',
                            href: '#',
                            style: 'text-decoration: none;',
                            onClick: setIx(ix),
                        },
                        '▲'
                    )
                } else {
                    const [_, ascending] = sortByIx[sortIx];
                    sortButton = [
                        p.h('a', { href: '#', style: 'text-decoration: none;', onClick: setIx(ix)}, `${ascending ? "▲" : "▼"}`),
                        p.h('sup', {}, `${sortIx+1}`), ' ',
                        p.h('a', {href: '#', style: 'text-decoration: none;', onClick: removeIx(ix)}, '×')];
                }             
                return p.h('th', {}, c, ' ', p.h('small', {}, sortButton))
            })),
            disableSearch ? null :
                p.h('tr', {className: 'text-nowrap'}, cols.map((_, ix) => {
                    return p.h('th', {className: 'py-1 input-group-sm'}, p.h('input', { type: 'text', className: 'form-control', onChange: setColFilter(ix), value: colFilters[ix] }));
                })),
        ),
        p.h('tbody', {},
            pageRows.map(row => 
                p.h('tr', {className: 'text-nowrap', ...rowProps(row)}, rowCells(row).map((r, i) => {
                    let col = cols[i];
                    let content = r;
                    if (col.string) {
                        content = col.string(content);
                    }
                    if (col.render) {
                        content = col.render(content, row);
                    }
                    return p.h('td', {}, content);
                })),
            ),
            dummyRows,
        ),
        pages.length > 1 ? p.h('thead', {}, p.h(Paginate)) : null,
    );
}

function Table(props) {
    const elementsPerPageStorage = localStorage.getItem(`table-${props.identifier}-elements-per-page`);
    const elementsPerPage = elementsPerPageStorage ? parseInt(elementsPerPageStorage) : (props.elementsPerPage || 20);
    const sortByIxStorage = localStorage.getItem(`table-${props.identifier}-sort-by-ix`);
    const sortByIx = sortByIxStorage ? JSON.parse(sortByIxStorage) : [];
    return TableInner({...props, elementsPerPage, sortByIx});
}

// index
// --------------------------------------------------------------------

function stringifyShortFlags(f) {
    if (f == 0) {
        return '0';
    }
    const flags = [[0x1, 'S'], [0x2, 'NR'], [0x4, 'NW'], [0x8, 'D']];
    return flags.filter(([otherF, _]) => otherF&f).map(([_, s]) => s).join('|');
}

function stringifyStorageClass(x) {
    switch (x) {
    case 2:
        return 'HDD';
    case 3:
        return 'FLASH';
    }
    throw Error(`Bad storage ${x}`);
}

function stringifySize(x) {
    let amount = x;
    let unit = 'byte';
    if (x > 1e15) {
        amount = x/1e15;
        unit = 'PB';
    } else if (x > 1e12) {
        amount = x/1e12;
        unit = 'TB';
    } else if (x > 1e9) {
        amount = x/1e9;
        unit = 'GB';
    } else if (x > 1e6) {
        amount = x/1e6;
        unit = 'MB';
    } else if (x > 1e3) {
        amount = x/1e3;
        unit = 'KB';
    }
    return amount.toFixed(2) + unit;
}

function stringifyAgo(then, now) {
    const diffInSeconds = Math.floor((now - then) / 1000);

    let timeAgo;
    if (diffInSeconds < 60) {
        timeAgo = `${diffInSeconds}s ago`;
    } else if (diffInSeconds < 3600) {
        timeAgo = `${Math.floor(diffInSeconds / 60)}m ago`;
    } else if (diffInSeconds < 86400) {
        timeAgo = `${Math.floor(diffInSeconds / 3600)}h ago`;
    } else {
        timeAgo = `${Math.floor(diffInSeconds / 86400)}d ago`;
    }
    
    return timeAgo;
}

function stringifyAddress(ip, port) {
    return `${ip[0]}.${ip[1]}.${ip[2]}.${ip[3]}:${port}`;
}

export function renderIndex() {
    function BlockServices() {
        const [blockServices, setBlockServices] = useState(null);
        useEffect(async () => {
            const resp = await shuckleReq('ALL_BLOCK_SERVICES', {});
            setBlockServices(resp);
        }, []);
    
        if (blockServices === null) {
            return p.h('em', null, 'Loading...');
        }
 
        const now = new Date();
        const rows = [];
        let failureDomains = new Set();
        let capacity = 0;
        let available = 0;
        let blocks = 0n;
        for (const bs of blockServices.BlockServices) {
            failureDomains.add(bs.Info.FailureDomain);
            capacity += bs.Info.CapacityBytes;
            available += bs.Info.AvailableBytes;
            blocks += BigInt(bs.Info.Blocks);
            rows.push([
                bs.Info.FailureDomain,
                stringifyAddress(bs.Info.Ip1, bs.Info.Port1),
                stringifyAddress(bs.Info.Ip2, bs.Info.Port2),
                bs.Info.Path,
                bs.Info.Id,
                bs.Info.Flags,
                bs.Info.StorageClass,
                bs.Info.Blocks,
                bs.Info.CapacityBytes,
                bs.Info.AvailableBytes,
                bs.Info.LastSeen,
                bs.HasFiles,
            ]);
        }
        const cols = [
            {name: 'FailureDomain'},
            {name: 'Address 1'},
            {name: 'Address 2'}, 
            {name: 'Path', render: t => p.h('code', {}, t)},
            {name: 'Id', render: t => p.h('code', {}, t)},
            {name: 'Flags', string: stringifyShortFlags, render: t => p.h('code', {}, t)},
            {name: 'StorageClass', string: stringifyStorageClass},
            {name: 'Blocks'},
            {name: 'Capacity', string: stringifySize},
            {name: 'Available', string: stringifySize},
            {name: 'LastSeen', render: ls => stringifyAgo(new Date(ls), now)},
            {name: 'HasFiles', string: x => x === true ? "yes" : "no"},
        ];

        return p.h('div', {},
            p.h('p', {}, p.h('ul', {},
                p.h('li', {}, `${rows.length} block services on ${failureDomains.size} failure domains`),
                p.h('li', {}, `Total capacity: ${stringifySize(capacity)}`),
                p.h('li', {}, `${stringifySize(capacity-available)} used over ${blocks} blocks (${(100*(capacity-available)/available).toFixed(2)}%)`),
            )),
            p.h(Table, { identifier: 'block-services', cols, rows, initialSorting: [[0, true], [3, true]] })
        )
    }
    p.render(p.h(BlockServices), document.getElementById('block-services-table'));

    function Shards() {
        const [shards, setShards] = useState(null);
        useEffect(async () => {
            const resp = await shuckleReq('SHARDS', {});
            setShards(resp);
        }, []);
    
        if (shards === null) {
            return p.h('em', null, 'Loading...');
        }

        const now = new Date();
        const rows = [];
        shards.Shards.forEach((shard, ix) => {
            rows.push([
                ix,
                stringifyAddress(shard.Ip1, shard.Port1),
                stringifyAddress(shard.Ip2, shard.Port2),
                shard.LastSeen,
            ])
        });
        const cols = [
            {name: 'Id', string: i => i.toString().padStart(3, '0'), render: t => p.h('code', {}, t)},
            {name: 'Address 1', render: t => p.h('code', {}, t)},
            {name: 'Address 2', render: t => p.h('code', {}, t)},
            {name: 'LastSeen', render: ls => stringifyAgo(new Date(ls), now)},
        ];

        return p.h(Table, { identifier: 'shards', cols, rows })
    }
    p.render(p.h(Shards), document.getElementById('shards-table'));
}

// browse
// --------------------------------------------------------------------

export function renderDirectoryEdges(id, path) {
    function Edges({id, path, showSnapshotEdges: initialShowSnapshotEdges}) {
        const [{showSnapshotEdges, edges, loadedEdges}, setState] = useState({
            showSnapshotEdges: initialShowSnapshotEdges,
            edges: null,
            loadedEdges: 0,
        });

        useEffect(async () => {
            const results = [];
            const req = {DirId: id};
            while (true) {
                const resp = await shardReq('FULL_READ_DIR', idToShard(id), req);
                results.push(...resp.Results);
                if (resp.Next.StartName === '') {
                    break;
                }
                req.Flags = resp.Next.Current ? 1 : 0;
                req.StartName = resp.Next.StartName;
                req.StartTime = resp.Next.StartTime;
                setState(prev => ({...prev, loadedEdges: results.length}))
            }
            setState(prev => ({...prev, edges: results}));
        }, [])

        useEffect(() => {
            if (showSnapshotEdges) {
                localStorage.setItem('edges-full', showSnapshotEdges);
            } else {
                localStorage.removeItem('edges-full');
            }                
        }, [showSnapshotEdges])

        const renderBool = s => s === 'yes' ? p.h('strong', {}, s) : s;
    
        let edgesTable = p.h('em', {}, `Loading (${loadedEdges}/?)...`);
        if (edges !== null) {
            const rows = [];
            for (const edge of edges) {
                if (!showSnapshotEdges && !edge.Current) {
                    continue;
                }
                rows.push({
                    props: {...(edge.Current ? {} : {className: 'table-active'})},
                    cells: [
                        edge.NameHash,
                        edge.Name,
                        edge.TargetId.Id === NULL_INODE_ID ? '' : idToType(edge.TargetId.Id),
                        edge.TargetId.Id,
                        edge.CreationTime,
                        ...(showSnapshotEdges ? [edge.Current ? 'yes' : 'no'] : []),
                        ...(showSnapshotEdges ? [edge.Current ? 'yes' : (edge.TargetId.Extra ? 'yes' : 'no')] : []),
                        edge.Current ? (edge.TargetId.Extra ? 'yes' : 'no') : '',
                    ],
                })
            }
            const cols = [
                {name: 'NameHash', render: t => p.h('code', {}, t)},
                {
                    name: 'Name',
                    render: (t, row) => {
                        const id = rowCells(row)[3];
                        if (id === NULL_INODE_ID) {
                            return p.h('code', {}, t);
                        } else {
                            const name = rowCells(row)[1];
                            const typ = rowCells(row)[2];
                            if (path && (!showSnapshotEdges || rowCells(row)[5] === 'yes')) { // current
                                return p.h('a', {href: `/browse${path}${name}`}, p.h('code', {}, t + (typ === 'DIRECTORY' ? '/' : '')));
                            } else {
                                return p.h('a', {href: `/browse?id=${id}&name=${t}`}, p.h('code', {}, t + (typ === 'DIRECTORY' ? '/' : '')));
                            }
                        }
                    },
                },
                {name: 'Type', render: t => p.h('code', {}, t)},
                {name: 'Target', render: t => p.h('code', {}, t)},
                {name: 'CreationTime', render: t => p.h('code', {}, t)},
                ...(showSnapshotEdges ? [{name: 'Current', render: renderBool}]: []),
                ...(showSnapshotEdges ? [{name: 'Owned', render: renderBool}] : []),
                {name: 'Locked', render: renderBool},
            ];
            edgesTable = p.h(Table, {
                identifier: 'edges',
                rows,
                cols,
                elementsPerPage: 100,
                filterFromSearchParams: true,
                // the key forces rerender when the number of cols changes
                key: `edges-${showSnapshotEdges}`,
                paginateExtra: [p.h('div', {className: 'ms-2 form-check form-check-inline'},
                    p.h(
                        'input',
                        {
                            type: 'checkbox',
                            className: 'form-check-input',
                            checked: !!showSnapshotEdges,
                            onChange: (ev) => {
                                ev.preventDefault();
                                setState(prev => ({...prev, showSnapshotEdges: !prev.showSnapshotEdges}));
                            },
                        }
                    ),
                    p.h('label', {className: 'form-check-label'}, 'Show snapshot edges'),
                )],
            });
        }

        return edgesTable
    }

    const showSnapshotEdges = localStorage.getItem('edges-full');
    p.render(p.h(Edges, {id, path, showSnapshotEdges}), document.getElementById('edges-table'));
}

// transient files
// --------------------------------------------------------------------

export function renderTransientFiles() {
    function TransientFiles() {
        const [{shard, transientFiles, loadedTransientFiles}, setState] = useState({
            shard: 0,
            transientFiles: null,
            loadedTransientFiles: 0,
        });

        useEffect(async () => {
            const results = [];
            const req = {};
            while (true) {
                const resp = await shardReq('VISIT_TRANSIENT_FILES', shard, req);
                results.push(...resp.Files);
                if (resp.NextId === NULL_INODE_ID) { break; }
                req.BeginId = resp.NextId;
                setState(prev => ({...prev, loadedTransientFiles: results.length}));
            }
            setState(prev => ({...prev, transientFiles: results}));
        }, [shard]);

        const rows = [];
        if (transientFiles !== null) {
            for (const {Id, Cookie, DeadlineTime} of transientFiles) {
                rows.push([Id, Cookie, DeadlineTime]);
            }    
        }
        const cols = [
            {name: 'Id', render: t => p.h('a', {href: `/browse?id=${t}`}, p.h('code', {}, t))},
            {name: 'Cookie', render: t => p.h('code', {}, t)},
            {name: 'Deadline', render: t => p.h('code', {}, t)},
        ]

        return [
            p.h('div', {className: 'input-group', style: 'width: 10rem;'},
                p.h('div', {className: 'input-group-text'}, 'Shard'),
                p.h('input', {
                    type: 'number', min: '0', max: '255', step: '1', className: 'form-control', value: shard.toString(),
                    onChange: (ev) => {
                        const shard = ev.target.value;
                        setState(prev => ({...prev, shard: parseInt(shard), transientFiles: null}));
                    },
                }),
            ),
            // key for redrawing...
            transientFiles === null ? p.h('em', {}, `Loading (${loadedTransientFiles}/?)...`) : p.h(Table, {key: shard, rows, cols, identifier: 'transient'}),
        ]
    }
    p.render(p.h(TransientFiles), document.getElementById('transient-files-table'));
}

// stats
// --------------------------------------------------------------------

export function renderStats() {
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
    
    function drawLatencyChart(req, timings) {
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
        const response = await fetch('/api/shuckle/GET_STATS', {
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
                const segments = stat.name.split('.');
                const whichStat = segments[segments.length-1];
                let name;
                if (segments[0] === 'shard') { // group all shards stats in one bunch
                    name = segments[0] + '.' + segments.slice(2, segments.length-1).join('.');
                } else {
                    name = segments.slice(0, segments.length-1).join('.');
                }
                data.stats[name] = data.stats[name] || {
                    histogram: [],
                    elapsedNs: BigInt(0),
                    firstStat: 1n<<64n,
                    lastStat: 0n,
                    errors: {},
                };
                if (whichStat === 'latency') { // latency histo
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
                } else if (whichStat === 'errors') { // errors stats
                    // not parsed yet
                } else {
                    throw `Bad stat ${whichStat}`;
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

        // now draw the latency divs
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
                    drawLatencyChart(stat, statData)
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
        el.addEventListener('change', reload);
    }

    reload();
}
