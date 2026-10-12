// The sound tab of the workbench (docs/web.md 11): what each channel of the
// sequencer does over the run, read from the core's own blocks
// (xb_audio_block, Audio.md 3 to 5), and a view of it with a header per
// channel and a piano roll. Nothing here is computed twice: notes, samples,
// levels and calls are those of the core, only gathered and drawn.

// The samples the tracks set, named as in Audio.md 3.5
export const SAMPLES = {
    1: 'sawtooth', 3: 'sawtooth', 5: 'noise', 6: '8-bit 0x8006C008',
    7: '8-bit 0x8006CEC8 (loop)', 9: '8-bit 0x80066AC0',
};
export const SAMPLE_COLOUR = { 1: '#f0a040', 3: '#f6d04a', 5: '#9aa4b4', 6: '#5fd0f0', 7: '#c08cff', 9: '#7ee06a' };
// XB_CALL_* of xboot.h, named as in Audio.md 5
export const CALLS = ['Filter', 'Release', 'Sample', 'Level', 'Note', 'Pitch', 'B2'];
const CALL_COLOUR = ['#5aa9ff', '#ff6a5a', '#ffffff', '#ffb347', null, null, '#888'];
const NOTE = 4, PITCH = 5, STOP = 1, SAMPLE = 2;

export const CHANNELS = 12;          // the twelve tracks of event 1 (Audio.md 3.5)
const BLOCK_MS = 32 / 48;            // XB_AUDIO_BLOCK frames at 48 kHz
const BUCKET = 9;                    // blocks per tick of the sequencer (Audio.md 1)
export const BUCKET_MS = BLOCK_MS * BUCKET;

// The semitone of a pitch for the library (Audio.md 5.3: 341 per semitone,
// 60 at 0): the pitch byte of the track plus its fraction.
export const semitone = (lib) => 60 + lib / 341;

// Runs the sound of a fresh core from the start for `ms` and gathers per
// channel its notes, its calls and per tick its gain and loudness. Yields to
// the page now and then; `progress` gets 0 ... 1.
export async function analyse(core, ms, progress) {
    core.init(0, 0);
    core.initSound(4800);
    const blocks = Math.ceil(ms / BLOCK_MS), buckets = Math.ceil(blocks / BUCKET);
    const ch = Array.from({ length: CHANNELS }, (_, c) => ({
        channel: c, notes: [], calls: [], gain: new Float32Array(buckets), level: new Float32Array(buckets),
        sample: null, open: null, released: null,
    }));
    for (let b = 0; b < blocks; b++) {
        const blk = core.audioBlock(), at = b * BLOCK_MS, k = Math.floor(b / BUCKET);
        for (const call of blk.calls) {
            const c = ch[call.channel];
            if (!c)
                continue;
            c.calls.push({ ms: at, fn: call.fn, a0: call.a0, a1: call.a1, effect: call.effect });
            if (call.fn === SAMPLE)
                c.sample = call.a0;
            if (call.fn === NOTE || call.fn === PITCH) {
                if (c.open)
                    c.open.end = at;
                c.open = { start: at, end: null, lib: call.a0, pitch: semitone(call.a0), sample: c.sample,
                           struck: call.fn === NOTE && !!call.effect, retune: call.fn === PITCH || !call.effect };
                c.notes.push(c.open);
                c.released = null;
            }
            if (call.fn === STOP && c.open && c.released === null) {
                c.open.release = at;
                c.released = at;
            }
        }
        for (const c of ch) {
            let g = 0, e = 0;
            for (let i = 0; i < 3; i++)
                g = Math.max(g, blk.gain[3 * c.channel + i]);
            for (let i = 0; i < 32; i++)
                e += blk.voice[32 * c.channel + i] ** 2;
            const lv = g * Math.sqrt(e / 32);
            c.gain[k] = Math.max(c.gain[k], g);
            c.level[k] = Math.max(c.level[k], lv);
            // a released note ends where its voice has fallen silent
            if (c.open && c.released !== null && g === 0 && at - c.released > BUCKET_MS) {
                c.open.end = at;
                c.open = null;
            }
        }
        if (b % 600 === 599) {
            progress?.(b / blocks);
            await new Promise((ok) => setTimeout(ok, 0));
        }
    }
    let gmax = 0, lmax = 0;
    for (const c of ch) {
        if (c.open)
            c.open.end = ms;
        c.gmax = Math.max(...c.gain);
        gmax = Math.max(gmax, c.gmax);
        lmax = Math.max(lmax, ...c.level);
        const p = c.notes.map((n) => n.pitch);
        c.lo = p.length ? Math.min(...p) : 0;
        c.hi = p.length ? Math.max(...p) : 0;
        c.samples = [...new Set(c.calls.filter((x) => x.fn === SAMPLE).map((x) => x.a0))];
        delete c.open;
        delete c.released;
    }
    progress?.(1);
    return { ms, channels: ch, gmax, lmax };
}

const esc = (s) => String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;');
const fmt = (ms) => (ms / 1000).toFixed(3) + ' s';

// The view: headers on the left (mute, solo, name, sample, current note,
// level), the piano roll on the right. `onSeek(ms)` for a click or drag,
// `onMute()` when mute or solo change.
export class TrackView {
    constructor(root, { onSeek, onMute }) {
        this.root = root;
        this.data = null;
        this.mute = new Set();
        this.solo = new Set();
        this.layers = { notes: true, volume: true, events: true, grid: true };
        this.span = 8000;
        this.t = 0;
        this.onSeek = onSeek;
        this.onMute = onMute;
        root.innerHTML = '<div class="tv-head"></div><div class="tv-roll"><canvas></canvas><div class="tv-tip" hidden></div></div>';
        this.head = root.querySelector('.tv-head');
        this.canvas = root.querySelector('canvas');
        this.tip = root.querySelector('.tv-tip');
        let drag = false;
        const seek = (e) => {
            const r = this.canvas.getBoundingClientRect();
            this.onSeek(Math.max(0, Math.min(1, (e.clientX - r.left) / r.width)) * this.span);
        };
        this.canvas.onpointerdown = (e) => { drag = true; this.canvas.setPointerCapture?.(e.pointerId); seek(e); };
        this.canvas.onpointermove = (e) => { if (drag) seek(e); else this.hover(e); };
        this.canvas.onpointerup = () => { drag = false; };
        this.canvas.onpointerleave = () => { this.tip.hidden = true; };
        this.head.onclick = (e) => {
            const { mute, solo } = e.target.dataset;
            const set = mute !== undefined ? this.mute : solo !== undefined ? this.solo : null;
            if (!set)
                return;
            const c = Number(mute ?? solo);
            set.has(c) ? set.delete(c) : set.add(c);
            this.onMute();
            this.headers();
        };
        new ResizeObserver(() => this.draw()).observe(root);
    }

    // Bit c set: channel c left out of the mix (xb_audio_mute).
    get mask() {
        let m = 0;
        for (let c = 0; c < 16; c++)
            if (this.mute.has(c) || (this.solo.size && !this.solo.has(c)))
                m |= 1 << c;
        return m;
    }

    set(data) {
        this.data = data;
        this.headers();
        this.draw();
    }

    headers() {
        if (!this.data)
            return;
        this.head.innerHTML = this.data.channels.map((c) => `
            <div class="tv-row" data-ch="${c.channel}">
              <button class="tv-btn${this.mute.has(c.channel) ? ' on' : ''}" data-mute="${c.channel}" title="mute">M</button>
              <button class="tv-btn solo${this.solo.has(c.channel) ? ' on' : ''}" data-solo="${c.channel}" title="solo">S</button>
              <div class="tv-name"><b>Channel ${c.channel}</b><span>${c.samples.length ? c.samples.map((s) => `${s} ${esc(SAMPLES[s] || '')}`).join(', ') : c.notes.length ? '' : 'no note'}</span></div>
              <div class="tv-note">–</div>
              <div class="tv-meter"><i></i></div>
            </div>`).join('');
        this.update(this.t);
    }

    // The current note, sample and level of every channel at `ms`.
    update(ms) {
        this.t = ms;
        if (!this.data)
            return;
        const k = Math.min(Math.floor(ms / BUCKET_MS), this.data.channels[0].level.length - 1);
        const silent = this.mask;
        for (const row of this.head.querySelectorAll('.tv-row')) {
            const c = this.data.channels[row.dataset.ch];
            const n = c.notes.find((x) => x.start <= ms && ms < x.end);
            row.querySelector('.tv-note').textContent = n ? `${n.pitch.toFixed(2)}${n.sample != null ? ' · S' + n.sample : ''}` : '–';
            const lv = silent & (1 << c.channel) ? 0 : Math.sqrt(c.level[Math.max(k, 0)] / (this.data.lmax || 1));
            row.querySelector('.tv-meter i').style.width = `${Math.min(lv, 1) * 100}%`;
            row.classList.toggle('muted', !!(silent & (1 << c.channel)));
        }
        this.draw();
    }

    rowHeight() {
        const first = this.head.querySelector('.tv-row');
        return first ? first.getBoundingClientRect().height : 30;
    }

    draw() {
        const cv = this.canvas, d = this.data;
        const w = cv.clientWidth, rh = this.rowHeight(), h = d ? rh * d.channels.length : 60;
        const dpr = window.devicePixelRatio || 1;
        cv.style.height = h + 'px';
        if (cv.width !== Math.round(w * dpr) || cv.height !== Math.round(h * dpr)) {
            cv.width = Math.round(w * dpr);
            cv.height = Math.round(h * dpr);
        }
        const g = cv.getContext('2d');
        g.setTransform(dpr, 0, 0, dpr, 0, 0);
        g.clearRect(0, 0, w, h);
        if (!d)
            return;
        const X = (ms) => ms / this.span * w, css = getComputedStyle(this.root);
        const line = css.getPropertyValue('--line').trim() || '#333', accent = css.getPropertyValue('--accent').trim() || '#8f8';
        if (this.layers.grid) {
            g.fillStyle = line;
            for (let s = 0; s <= this.span; s += 500)
                g.fillRect(Math.round(X(s)), 0, s % 1000 ? 0.5 : 1, h);
        }
        d.channels.forEach((c, i) => {
            const y0 = i * rh, muted = this.mask & (1 << c.channel);
            g.globalAlpha = muted ? 0.3 : 1;
            g.fillStyle = line;
            g.fillRect(0, y0 + rh - 1, w, 1);
            if (this.layers.volume && c.gmax > 0) {
                g.beginPath();
                g.moveTo(0, y0 + rh - 2);
                for (let k = 0; k < c.gain.length; k++)
                    g.lineTo(X(k * BUCKET_MS), y0 + rh - 2 - (c.gain[k] / d.gmax) * (rh - 6));
                g.lineTo(X(c.gain.length * BUCKET_MS), y0 + rh - 2);
                g.fillStyle = 'rgba(140, 200, 255, 0.13)';
                g.fill();
                g.strokeStyle = 'rgba(140, 200, 255, 0.55)';
                g.lineWidth = 1;
                g.stroke();
            }
            if (this.layers.notes) {
                const span = Math.max(c.hi - c.lo, 1);
                for (const n of c.notes) {
                    const y = c.hi > c.lo ? y0 + 4 + (1 - (n.pitch - c.lo) / span) * (rh - 12) : y0 + rh / 2 - 2;
                    g.fillStyle = SAMPLE_COLOUR[n.sample] || '#ddd';
                    g.globalAlpha = (muted ? 0.3 : 1) * (n.retune ? 0.6 : 1);
                    g.fillRect(X(n.start), y, Math.max(X(n.end) - X(n.start), 1.5), 4);
                    if (n.struck)
                        g.fillRect(X(n.start), y - 2, 1.5, 8);
                    if (n.release != null) {
                        g.globalAlpha *= 0.35;
                        g.fillRect(X(n.release), y + 1, Math.max(X(n.end) - X(n.release), 0), 2);
                    }
                }
                g.globalAlpha = muted ? 0.3 : 1;
            }
            if (this.layers.events)
                for (const e of c.calls) {
                    const col = CALL_COLOUR[e.fn];
                    if (!col)
                        continue;
                    g.fillStyle = col;
                    const x = X(e.ms);
                    if (e.fn === SAMPLE) {
                        g.beginPath();
                        g.moveTo(x, y0 + rh - 9);
                        g.lineTo(x + 4, y0 + rh - 2);
                        g.lineTo(x - 4, y0 + rh - 2);
                        g.fill();
                    } else {
                        g.fillRect(x, y0 + rh - (e.fn === STOP ? 10 : 5), 1, e.fn === STOP ? 9 : 4);
                    }
                }
            g.globalAlpha = 1;
        });
        g.fillStyle = accent;
        g.fillRect(X(this.t) - 1, 0, 2, h);
    }

    // What lies under the pointer: a note, else the nearest call.
    hover(e) {
        const d = this.data;
        if (!d)
            return;
        const r = this.canvas.getBoundingClientRect(), rh = this.rowHeight();
        const ms = (e.clientX - r.left) / r.width * this.span, i = Math.floor((e.clientY - r.top) / rh), c = d.channels[i];
        if (!c) {
            this.tip.hidden = true;
            return;
        }
        const slack = this.span / r.width * 4;
        const n = c.notes.find((x) => x.start - slack <= ms && ms < x.end);
        const calls = c.calls.filter((x) => Math.abs(x.ms - ms) < slack && CALL_COLOUR[x.fn]);
        const lines = [`<b>Channel ${c.channel}</b> · ${fmt(ms)}`];
        if (n)
            lines.push(`${n.struck ? 'Note struck' : n.retune ? 'Pitch changed' : 'Note, voice still running'}: ${n.pitch.toFixed(2)} (lib ${n.lib})`,
                       `Sample ${n.sample ?? '–'} ${esc(SAMPLES[n.sample] || '')}`,
                       `${fmt(n.start)} to ${fmt(n.end)}${n.release != null ? ', released ' + fmt(n.release) : ''}`);
        for (const x of calls.slice(0, 4))
            lines.push(`${CALLS[x.fn]} at ${fmt(x.ms)}: ${x.fn === 0 ? `F ${x.a0}, Q ${x.a1}` : x.fn === 3 ? `Level ${x.a0}` : x.fn === 2 ? `Sample ${x.a0}` : ''}`);
        this.tip.innerHTML = lines.join('<br>');
        this.tip.hidden = false;
        const x = e.clientX - r.left, flip = x > r.width - 230;
        this.tip.style.left = `${flip ? x - 230 : x + 12}px`;
        this.tip.style.top = `${Math.min(e.clientY - r.top + 12, r.height - 70)}px`;
    }
}
