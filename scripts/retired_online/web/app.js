const $ = (id) => document.getElementById(id);
const map = L.map('map').setView([37.55, 126.98], 10);
L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png', {
  maxZoom: 19,
  attribution: '&copy; OpenStreetMap contributors | Sentinel-2 imagery: Copernicus / ESA',
}).addTo(map);

const state = { bbox: null, rect: null, a: null, b: null, overlays: [], products: null, request: 0 };

// ---------- helpers ----------
async function getJSON(url) {
  const r = await fetch(url);
  const body = await r.json().catch(() => ({}));
  if (!r.ok) throw new Error(body.detail || `Request failed (${r.status})`);
  return body;
}
async function getImage(url) {
  const r = await fetch(url);
  if (!r.ok) throw new Error((await r.json().catch(() => ({}))).detail || `Request failed (${r.status})`);
  return URL.createObjectURL(await r.blob());
}
const fmt = (n, d = 2) => Number(n).toLocaleString(undefined, { maximumFractionDigits: d });
const isIndex = (p) => state.products.indices.some((i) => i.id === p);
const sceneDate = (s) => s.datetime.slice(0, 10);
function setInfo(id, text, isError = false) {
  $(id).textContent = text;
  $(id).classList.toggle('error', isError);
}

// ---------- 1. area ----------
let drawing = false;
let anchor = null;
$('draw').onclick = () => {
  drawing = true;
  anchor = null;
  map.dragging.disable();
  $('map').classList.add('drawing');
  $('draw').classList.add('active');
};
map.on('mousedown', (e) => {
  if (!drawing) return;
  e.originalEvent.preventDefault(); // stop the drag from selecting page content
  anchor = e.latlng;
  if (state.rect) state.rect.remove();
  state.rect = L.rectangle([anchor, anchor], { color: '#eb6834', weight: 2, fill: false, interactive: false }).addTo(map);
});
map.on('mousemove', (e) => {
  if (drawing && anchor) state.rect.setBounds(L.latLngBounds(anchor, e.latlng));
});
map.on('mouseup', () => {
  if (!drawing || !anchor) return;
  map.dragging.enable();
  $('map').classList.remove('drawing');
  $('draw').classList.remove('active');
  setTimeout(() => { drawing = false; }, 0); // let the trailing click event pass first
  anchor = null;
  const b = state.rect.getBounds();
  if (b.getWest() === b.getEast() || b.getSouth() === b.getNorth()) return;
  state.bbox = [b.getWest(), b.getSouth(), b.getEast(), b.getNorth()].map((v) => v.toFixed(6)).join(',');
  const widthKm = b.getSouthWest().distanceTo(b.getSouthEast()) / 1000;
  const heightKm = b.getSouthWest().distanceTo(b.getNorthWest()) / 1000;
  setInfo('area-info', `Area: ${fmt(widthKm, 1)} × ${fmt(heightKm, 1)} km`);
  $('search').disabled = false;
  state.a = state.b = null;
  $('scenes').replaceChildren();
  setInfo('search-info', '');
  refresh();
});

// ---------- 2. search ----------
const today = new Date();
$('end').value = today.toISOString().slice(0, 10);
$('start').value = new Date(today - 90 * 864e5).toISOString().slice(0, 10);

$('search').onclick = async () => {
  setInfo('search-info', 'Searching…');
  $('scenes').replaceChildren();
  try {
    const q = new URLSearchParams({ bbox: state.bbox, start: $('start').value, end: $('end').value, max_cloud: $('cloud').value });
    const { scenes } = await getJSON(`/api/search?${q}`);
    setInfo('search-info', scenes.length
      ? `${scenes.length} scenes. Choose A to display; add B to compare.`
      : 'No scenes found. Widen the dates or raise the cloud limit.');
    scenes.forEach(addSceneRow);
  } catch (err) {
    setInfo('search-info', err.message, true);
  }
};

function addSceneRow(scene) {
  const li = document.createElement('li');
  const when = document.createElement('span');
  when.className = 'when';
  when.textContent = sceneDate(scene);
  const meta = document.createElement('span');
  meta.className = 'meta';
  meta.textContent = `cloud ${scene.cloud_cover}%` + (scene.coverage < 0.995 ? ` · covers ${Math.round(scene.coverage * 100)}%` : '');
  li.append(when, meta);
  for (const slot of ['a', 'b']) {
    const btn = document.createElement('button');
    btn.type = 'button';
    btn.textContent = slot.toUpperCase();
    btn.title = `Use as scene ${slot.toUpperCase()}`;
    btn.dataset.slot = slot;
    btn.dataset.id = scene.id;
    btn.onclick = () => {
      state[slot] = state[slot]?.id === scene.id ? null : scene;
      document.querySelectorAll('#scenes button').forEach((b) => {
        b.classList.toggle('on', state[b.dataset.slot]?.id === b.dataset.id);
      });
      refresh();
    };
    li.append(btn);
  }
  $('scenes').append(li);
}

// ---------- 3. layer ----------
function layerQuery(item, itemB) {
  const q = new URLSearchParams({
    item, bbox: state.bbox, product: $('product').value,
    mask_clouds: $('mask').checked, k: $('k').value,
  });
  if (itemB) q.set('item_b', itemB);
  return q;
}

// Which scenes the current view needs, or a message saying what is missing.
function plan() {
  const view = $('view').value;
  const product = $('product').value;
  const { a, b } = state;
  if (!state.bbox) return { message: 'Draw an area first.' };
  if (view === 'a') return a ? { layers: [[a.id]], title: sceneDate(a) } : { message: 'Pick scene A from the list to display it.' };
  if (view === 'b') return b ? { layers: [[b.id]], title: sceneDate(b) } : { message: 'Pick scene B from the list.' };
  if (!a || !b) return { message: 'Pick both scene A and scene B to compare.' };
  if (view === 'swipe') return { layers: [[a.id], [b.id]], title: `${sceneDate(a)} | ${sceneDate(b)}`, swipe: true };
  if (!isIndex(product)) return { message: 'Change detection needs an index product (NDVI, NDWI, NDBI or NBR).' };
  return { layers: [[a.id, b.id]], title: `${sceneDate(a)} → ${sceneDate(b)}`, change: true };
}

async function refresh() {
  const token = ++state.request;
  state.overlays.forEach((o) => o.remove());
  state.overlays = [];
  map.closePopup();
  const product = $('product').value;
  $('k-row').hidden = product !== 'landcover';
  $('threshold-row').hidden = $('view').value !== 'change';
  $('swipe').hidden = true;
  $('stats-section').hidden = true;
  $('legend').replaceChildren();

  const p = plan();
  if (p.message) return setInfo('layer-info', p.message);
  setInfo('layer-info', `Showing ${p.title}`);
  $('busy').hidden = false;
  try {
    const bounds = state.rect.getBounds();
    const urls = await Promise.all(p.layers.map((ids) => getImage(`/api/render.png?${layerQuery(...ids)}`)));
    if (token !== state.request) return;
    state.overlays = urls.map((u) => L.imageOverlay(u, bounds, { opacity: $('opacity').value }).addTo(map));
    $('swipe').hidden = !p.swipe;
    updateSwipe();
    drawLegend(product, p.change);

    $('stats-section').hidden = false;
    $('export-png').href = urls[urls.length - 1];
    $('export-tif').hidden = !!p.swipe;
    $('export-tif').href = `/api/export.tif?${layerQuery(...p.layers[0])}`;
    $('stats').replaceChildren();
    if (!p.swipe && (isIndex(product) || product === 'landcover')) {
      const q = layerQuery(...p.layers[0]);
      q.set('threshold', $('threshold').value);
      const stats = await getJSON(`/api/stats?${q}`);
      if (token === state.request) drawStats(stats, product);
    }
  } catch (err) {
    if (token === state.request) setInfo('layer-info', err.message, true);
  } finally {
    if (token === state.request) $('busy').hidden = true;
  }
}

function drawLegend(product, change) {
  const meta = change ? state.products.change : state.products.indices.find((i) => i.id === product);
  if (!meta) return;
  const ends = change ? ['Decrease', 'Increase'] : ['Low', 'High'];
  $('legend').innerHTML = `
    <div class="ramp-ends"><span>${ends[0]}</span><span>${ends[1]}</span></div>
    <div class="ramp" style="background:linear-gradient(to right, ${meta.stops.join(', ')})"></div>
    <div class="ramp-ticks"><span>${meta.vmin}</span><span>${meta.mid}</span><span>${meta.vmax}</span></div>`;
}

// Swipe: the top overlay (scene B) is clipped to the right of the divider.
function updateSwipe() {
  const top = state.overlays[1]?.getElement();
  if ($('swipe').hidden || !top) return;
  const frac = Number($('swipe-range').value);
  const mapBox = $('map').getBoundingClientRect();
  const imgBox = top.getBoundingClientRect();
  const scale = imgBox.width / top.offsetWidth || 1;
  const cut = Math.min(Math.max(mapBox.left + mapBox.width * frac - imgBox.left, 0), imgBox.width) / scale;
  top.style.clipPath = `inset(0 0 0 ${cut}px)`;
  $('swipe-line').style.left = `${frac * 100}%`;
}
map.on('move zoomend resize', updateSwipe);
$('swipe-range').oninput = updateSwipe;

$('opacity').oninput = () => state.overlays.forEach((o) => o.setOpacity($('opacity').value));
for (const id of ['product', 'view', 'mask', 'k', 'threshold']) $(id).onchange = refresh;

// Click inside the area to read the value under the cursor.
map.on('click', async (e) => {
  const p = plan();
  if (drawing || p.message || p.swipe || !isIndex($('product').value)) return;
  if (!state.rect.getBounds().contains(e.latlng)) return;
  const q = layerQuery(...p.layers[0]);
  q.set('lon', e.latlng.lng);
  q.set('lat', e.latlng.lat);
  try {
    const { value } = await getJSON(`/api/point?${q}`);
    const name = $('product').value.toUpperCase() + (p.change ? ' change' : '');
    L.popup().setLatLng(e.latlng).setContent(`${name}: ${value === null ? 'no data' : value}`).openOn(map);
  } catch { /* outside the grid or archive unreachable: nothing to show */ }
});

// ---------- 4. statistics ----------
function drawStats(s, product) {
  const tiles = [`<div class="tile"><b>${fmt(s.valid_km2)}</b><span>km² usable (${s.valid_pct}%)</span></div>`];
  if (s.mean !== undefined) {
    tiles.push(`<div class="tile"><b>${fmt(s.mean, 3)}</b><span>mean</span></div>`,
      `<div class="tile"><b>${fmt(s.median, 3)}</b><span>median</span></div>`);
  }
  const rows = (s.classes || []).map((c) => `
    <tr>
      <td>${c.color ? `<span class="swatch" style="background:${c.color}"></span>` : ''}${c.label}
        <div class="share" style="width:${c.pct}%${c.color ? `;background:${c.color}` : ''}"></div></td>
      <td class="num">${fmt(c.km2)} km²</td>
      <td class="num">${c.pct}%</td>
    </tr>`).join('');
  $('stats').innerHTML = `
    <div class="tiles">${tiles.join('')}</div>
    ${s.valid_pct === 0 ? '<p class="hint">No usable pixels: the scene is cloudy here or does not cover the area.</p>' : ''}
    <table class="classes">${rows}</table>
    ${product === 'landcover' ? '<p class="hint">Classes come from unsupervised clustering; names are estimates from each class\'s average spectrum.</p>' : ''}`;
  if (s.histogram) $('stats').append(...histogram(s.histogram, s.change));
}

function histogram({ edges, km2 }, change) {
  const W = 300, H = 96, base = H - 16, n = km2.length, step = W / n;
  const peak = Math.max(...km2) || 1;
  const bars = km2.map((v, i) => {
    const h = (v / peak) * (base - 4);
    return `<rect class="hit" x="${i * step}" y="0" width="${step}" height="${base}" data-i="${i}"/>` +
      `<rect class="bar" x="${i * step + 1}" y="${base - h}" width="${step - 2}" height="${h}" rx="1.5" pointer-events="none"/>`;
  }).join('');
  const ticks = [-1, -0.5, 0, 0.5, 1].map((t) => {
    const x = ((t + 1) / 2) * W;
    const anchor = t === -1 ? 'start' : t === 1 ? 'end' : 'middle';
    return `<text x="${x}" y="${H - 3}" text-anchor="${anchor}">${t}</text>`;
  }).join('');
  const title = document.createElement('div');
  title.className = 'chart-title';
  title.textContent = change ? 'Area by amount of change (km²)' : 'Area by index value (km²)';
  const wrap = document.createElement('div');
  wrap.innerHTML = `<svg class="hist" viewBox="0 0 ${W} ${H}" role="img" aria-label="${title.textContent}">
    ${bars}<line x1="0" y1="${base}" x2="${W}" y2="${base}"/>${ticks}</svg>`;
  const tip = $('tooltip');
  wrap.querySelectorAll('.hit').forEach((hit) => {
    hit.onmousemove = (e) => {
      const i = Number(hit.dataset.i);
      tip.textContent = `${edges[i]} to ${edges[i + 1]}: ${fmt(km2[i])} km²`;
      tip.hidden = false;
      tip.style.left = `${e.clientX + 12}px`;
      tip.style.top = `${e.clientY - 28}px`;
    };
    hit.onmouseleave = () => { tip.hidden = true; };
  });
  return [title, wrap];
}

// ---------- start ----------
getJSON('/api/products').then((products) => {
  state.products = products;
  const group = (label, items) => {
    const g = document.createElement('optgroup');
    g.label = label;
    items.forEach((i) => g.append(new Option(i.label, i.id)));
    return g;
  };
  $('product').append(
    group('Imagery', products.composites),
    group('Indices', products.indices),
    group('Classification', [{ id: 'landcover', label: 'Land cover (K-means)' }]),
  );
});
