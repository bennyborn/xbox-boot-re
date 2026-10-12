// The eleven textures of the set-up (Textures.md 1) from the core onto the
// GPU. The core writes them as they lie in the guest's memory: levels one
// after another, the six faces of a cube each rounded up to 128 bytes,
// texels reordered (Textures.md 2, 3). Here they are put back in rows.
// Only level 0 is loaded: every stage of the original reads with
// SET_TEXTURE_FILTER 0x02022000, which stays on level 0 (GPU.md 6).

// Where texel (x, y) of a w x h picture lies (Textures.md 3): from bit 0
// up one bit of x while x has bits left, then one of y while y has.
function reordered(w, h) {
    const at = new Uint32Array(w * h);
    for (let y = 0; y < h; y++)
        for (let x = 0; x < w; x++) {
            let i = 0, bit = 0;
            for (let k = 0; (1 << k) < w || (1 << k) < h; k++) {
                if ((1 << k) < w)
                    i |= (x >> k & 1) << bit++;
                if ((1 << k) < h)
                    i |= (y >> k & 1) << bit++;
            }
            at[y * w + x] = i;
        }
    return at;
}

// Level 0 of one face as rows: R G B A for the formats 6 and 7 (the word
// A << 24 | R << 16 | G << 8 | B, little-endian), one byte for format 0x19.
function rows(t, bytes, face, faceBytes) {
    const { width: w, height: h, texelBytes: n } = t, at = reordered(w, h);
    const out = new Uint8Array(w * h * n), from = face * faceBytes;
    for (let i = 0; i < w * h; i++) {
        const s = from + at[i] * n;
        if (n === 1)
            out[i] = bytes[s];
        else
            out.set([bytes[s + 2], bytes[s + 1], bytes[s], t.format === 7 ? 255 : bytes[s + 3]], 4 * i);
    }
    return out;
}

// One GPUTexture per texture of core.textures(), in its order (XB_TEX_*).
export function upload(device, textures) {
    return textures.map((t) => {
        if (![6, 7, 0x19].includes(t.format) || (t.texelBytes !== 4 && t.texelBytes !== 1))
            throw new Error(`texture format 0x${t.format.toString(16)}`);
        const texture = device.createTexture({
            size: [t.width, t.height, t.faces], format: t.texelBytes === 1 ? 'r8unorm' : 'rgba8unorm',
            usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST });
        // a face with its levels, rounded up to 128 bytes for a cube
        const faceBytes = t.size / t.faces;
        for (let face = 0; face < t.faces; face++)
            device.queue.writeTexture({ texture, origin: [0, 0, face] }, rows(t, t.bytes, face, faceBytes),
                                      { bytesPerRow: t.width * t.texelBytes }, [t.width, t.height]);
        return texture;
    });
}
