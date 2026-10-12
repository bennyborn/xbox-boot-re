// The sound in the audio thread: a wasm core of its own that only plays.
// The sound has its own clock and does not depend on xb_advance
// (docs/core.md 2, "Ton"), so this instance never advances the picture.
//
// Messages: { cmd: 'play' }, { cmd: 'pause' }, { cmd: 'seek', frame },
// { cmd: 'mute', mask } (xb_audio_mute, for the workbench),
// { cmd: 'rate', rate } (W3b: played like a tape at that speed, faster is
// higher; docs/web.md 11).
// Seeking starts the sound again and plays up to `frame` unheard; the core
// has no other way to reach a place in the sound.
import { Core } from './core.js';

const CHUNK = 4800;

class XbootSound extends AudioWorkletProcessor {
    constructor(options) {
        super();
        const o = options.processorOptions;
        this.playing = !!o.autoplay;
        this.core = null;
        this.frame = 0;
        this.mask = 0;
        this.rate = 1;
        // at another rate: the core's samples ahead of the read position
        this.buf = new Float32Array(0);
        this.at = 0;
        Core.load(o.wasm, o.pack, { xb_config: o.layout.xb_config }).then((core) => {
            this.core = core;
            this.seek(o.frame || 0);
            this.port.postMessage({ ready: true });
        });
        this.port.onmessage = (e) => {
            const m = e.data;
            if (m.cmd === 'play')
                this.playing = true;
            else if (m.cmd === 'pause')
                this.playing = false;
            else if (m.cmd === 'seek' && this.core)
                this.seek(m.frame);
            else if (m.cmd === 'rate')
                this.rate = m.rate;
            else if (m.cmd === 'mute') {
                this.mask = m.mask;
                this.core?.call('xb_audio_mute', this.core.state, this.mask);
            }
        };
    }

    seek(frame) {
        this.core.init(0, 0);
        this.core.initSound(CHUNK);
        this.core.call('xb_audio_mute', this.core.state, this.mask);
        for (let left = frame; left > 0; left -= CHUNK)
            this.core.audio(Math.min(left, CHUNK));
        this.frame = frame;
        this.buf = new Float32Array(0);
        this.at = 0;
    }

    // n more stereo frames of the core behind those not yet read.
    more(n) {
        const s = this.core.audio(n), keep = Math.floor(this.at);
        const next = new Float32Array(this.buf.length - 2 * keep + 2 * n);
        next.set(this.buf.subarray(2 * keep));
        for (let i = 0; i < 2 * n; i++)
            next[this.buf.length - 2 * keep + i] = s[i] / 32768;
        this.buf = next;
        this.at -= keep;
    }

    process(inputs, outputs) {
        const out = outputs[0];
        if (!this.core || !this.playing)
            return true;
        const n = out[0].length;
        if (this.rate === 1 && !this.buf.length) {
            const s = this.core.audio(n);
            for (let i = 0; i < n; i++) {
                out[0][i] = s[2 * i] / 32768;
                if (out.length > 1)
                    out[1][i] = s[2 * i + 1] / 32768;
            }
            this.frame += n;
            return true;
        }
        // read the core's output at `rate` frames per frame, between two frames linearly
        const need = Math.ceil(this.at + n * this.rate) + 2 - this.buf.length / 2;
        if (need > 0)
            this.more(Math.min(need, CHUNK));
        for (let i = 0; i < n; i++) {
            const k = Math.floor(this.at), f = this.at - k, b = this.buf;
            if (2 * k + 3 >= b.length)
                break;
            out[0][i] = b[2 * k] + (b[2 * k + 2] - b[2 * k]) * f;
            if (out.length > 1)
                out[1][i] = b[2 * k + 1] + (b[2 * k + 3] - b[2 * k + 1]) * f;
            this.at += this.rate;
        }
        this.frame += n * this.rate;
        return true;
    }
}

registerProcessor('xboot-sound', XbootSound);
