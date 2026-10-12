// The sound on the page: an AudioContext at the rate of the sound hardware
// and the worklet that plays the core's output.
import { AUDIO_RATE } from './core.js';

const MODULE = new URL('./sound-worklet.js', import.meta.url);

function node(context, assets, more) {
    return new AudioWorkletNode(context, 'xboot-sound', {
        numberOfInputs: 0, outputChannelCount: [2],
        processorOptions: { wasm: assets.wasm, pack: assets.pack, layout: assets.layout, ...more },
    });
}

export class Sound {
    // Must be called from a click: browsers start no sound on their own.
    static async create(assets, frame) {
        const context = new AudioContext({ sampleRate: AUDIO_RATE });
        await context.audioWorklet.addModule(MODULE);
        const n = node(context, assets, { frame });
        await new Promise((resolve) => { n.port.onmessage = (e) => e.data.ready && resolve(); });
        // W3b: a volume and a tap for the scope behind the core's output
        const gain = context.createGain(), analyser = context.createAnalyser();
        analyser.fftSize = 2048;
        n.connect(gain).connect(analyser).connect(context.destination);
        return new Sound(context, n, gain, analyser);
    }

    constructor(context, n, gain, analyser) {
        this.context = context;
        this.node = n;
        this.gain = gain;
        this.analyser = analyser;
    }

    // Like a tape: the sound at `rate` times its speed, faster is higher.
    rate(rate) {
        this.node.port.postMessage({ cmd: 'rate', rate });
    }

    volume(v) {
        this.gain.gain.value = v;
    }

    play() {
        this.context.resume();
        this.node.port.postMessage({ cmd: 'play' });
    }

    pause() {
        this.node.port.postMessage({ cmd: 'pause' });
    }

    // Channels left out of the mix, bit c for channel c (xb_audio_mute).
    mute(mask) {
        this.node.port.postMessage({ cmd: 'mute', mask });
    }

    // The place in the sound that belongs to a tick: 48 frames per ms.
    seek(tick) {
        this.node.port.postMessage({ cmd: 'seek', frame: tick * (AUDIO_RATE / 1000) });
    }
}

// `frames` stereo frames from frame `from` on through the same worklet,
// without a sound device: interleaved 16-bit values as the core wrote them.
export async function renderOffline(assets, frames, from = 0) {
    const context = new OfflineAudioContext(2, frames, AUDIO_RATE);
    await context.audioWorklet.addModule(MODULE);
    const n = node(context, assets, { autoplay: true, frame: from });
    await new Promise((resolve) => { n.port.onmessage = (e) => e.data.ready && resolve(); });
    n.connect(context.destination);
    const buffer = await context.startRendering();
    const left = buffer.getChannelData(0), right = buffer.getChannelData(1);
    const out = new Int16Array(2 * frames);
    for (let i = 0; i < frames; i++) {
        out[2 * i] = Math.round(left[i] * 32768);
        out[2 * i + 1] = Math.round(right[i] * 32768);
    }
    return out;
}
