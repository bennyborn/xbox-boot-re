// WebGPU: one offscreen picture, shown on the canvas and read back for
// pictures on demand. It is 640 x 480 like the original's (GPU.md 7); the
// workbench may make it as large as the canvas is on the screen (resize,
// docs/web.md 11). In W0 the picture is a test pattern of three bars; the
// renderer of the animation replaces drawPattern from W1 on.

export const WIDTH = 640, HEIGHT = 480;
const FORMAT = 'rgba8unorm';

// Rows of the bars and their colours; hosts/web/test_w0.mjs measures them.
export const BARS = [
    { name: 't', y0: 40, y1: 80, rgb: [255, 255, 255] },
    { name: 'draws', y0: 120, y1: 160, rgb: [0, 255, 0] },
    { name: 'params', y0: 200, y1: 240, rgb: [0, 0, 255] },
];
export const BACKGROUND = [16, 16, 16];

const TRIANGLE = `
@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
    var p = array<vec2f, 3>(vec2f(-1, -1), vec2f(3, -1), vec2f(-1, 3));
    return vec4f(p[i], 0, 1);
}`;

const rgb = (c) => `vec3f(${c.map((v) => `${v}.0 / 255.0`).join(', ')})`;

const PATTERN = TRIANGLE + `
@group(0) @binding(0) var<uniform> u: vec4f;
@fragment fn fs(@builtin(position) p: vec4f) -> @location(0) vec4f {
    let x = p.x / ${WIDTH}.0;
    var c = ${rgb(BACKGROUND)};
${BARS.map((b, i) => `    if (p.y >= ${b.y0}.0 && p.y < ${b.y1}.0 && x < u[${i}]) { c = ${rgb(b.rgb)}; }`).join('\n')}
    return vec4f(c, 1);
}`;

const BLIT = TRIANGLE + `
@group(0) @binding(0) var picture: texture_2d<f32>;
@fragment fn fs(@builtin(position) p: vec4f) -> @location(0) vec4f {
    return textureLoad(picture, vec2u(p.xy), 0);
}`;

export class Gpu {
    static async create(canvas) {
        if (!navigator.gpu)
            throw new Error('this browser has no WebGPU');
        const adapter = await navigator.gpu.requestAdapter();
        if (!adapter)
            throw new Error('no WebGPU adapter');
        // a picture as wide as a wide screen needs a surface twice as wide
        // (docs/web.md 11.7): as large as the adapter allows, not 8192
        const maxTextureDimension2D = adapter.limits.maxTextureDimension2D;
        return new Gpu(await adapter.requestDevice({ requiredLimits: { maxTextureDimension2D } }), adapter.info, canvas);
    }

    constructor(device, info, canvas) {
        this.device = device;
        this.info = { vendor: info.vendor, architecture: info.architecture };
        // what WebGPU rejects (a shader, a pipeline) arrives here, not as an exception
        this.errors = [];
        device.onuncapturederror = (e) => this.errors.push(e.error.message);
        this.canvas = canvas;
        this.context = canvas.getContext('webgpu');
        const shown = navigator.gpu.getPreferredCanvasFormat();
        this.context.configure({ device, format: shown });

        const pattern = device.createShaderModule({ code: PATTERN });
        this.pattern = device.createRenderPipeline({
            layout: 'auto', vertex: { module: pattern }, fragment: { module: pattern, targets: [{ format: FORMAT }] },
        });
        this.values = device.createBuffer({ size: 16, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
        this.patternBind = device.createBindGroup({
            layout: this.pattern.getBindGroupLayout(0), entries: [{ binding: 0, resource: { buffer: this.values } }],
        });

        const blit = device.createShaderModule({ code: BLIT });
        this.blit = device.createRenderPipeline({
            layout: 'auto', vertex: { module: blit }, fragment: { module: blit, targets: [{ format: shown }] },
        });
        this.resize(WIDTH, HEIGHT);
    }

    // The picture and the canvas at width x height; true when that changed them.
    resize(width, height) {
        if (this.picture && this.width === width && this.height === height)
            return false;
        this.picture?.destroy();
        this.width = this.canvas.width = width;
        this.height = this.canvas.height = height;
        this.picture = this.device.createTexture({
            size: [width, height], format: FORMAT,
            usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC | GPUTextureUsage.TEXTURE_BINDING,
        });
        this.pictureView = this.picture.createView();
        this.blitBind = this.device.createBindGroup({
            layout: this.blit.getBindGroupLayout(0), entries: [{ binding: 0, resource: this.pictureView }],
        });
        return true;
    }

    // Draws the three bars; each value in 0 ... 1 lights that share of its row.
    drawPattern(values) {
        this.device.queue.writeBuffer(this.values, 0, new Float32Array([values[0], values[1], values[2], 0]));
        const enc = this.device.createCommandEncoder();
        const pass = enc.beginRenderPass({
            colorAttachments: [{ view: this.pictureView, loadOp: 'clear', storeOp: 'store', clearValue: [0, 0, 0, 1] }],
        });
        pass.setPipeline(this.pattern);
        pass.setBindGroup(0, this.patternBind);
        pass.draw(3);
        pass.end();
        this.device.queue.submit([enc.finish()]);
    }

    // Copies the picture onto the canvas.
    present() {
        const enc = this.device.createCommandEncoder();
        const pass = enc.beginRenderPass({
            colorAttachments: [{ view: this.context.getCurrentTexture().createView(), loadOp: 'clear', storeOp: 'store' }],
        });
        pass.setPipeline(this.blit);
        pass.setBindGroup(0, this.blitBind);
        pass.draw(3);
        pass.end();
        this.device.queue.submit([enc.finish()]);
    }

    // The picture as the program drew it: width * height * 4 bytes, R G B A,
    // rows packed. WebGPU copies rows in steps of 256 bytes.
    async read() {
        const { width, height } = this, row = width * 4, stride = Math.ceil(row / 256) * 256;
        const buffer = this.device.createBuffer({ size: stride * height, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
        const enc = this.device.createCommandEncoder();
        enc.copyTextureToBuffer({ texture: this.picture }, { buffer, bytesPerRow: stride }, [width, height]);
        this.device.queue.submit([enc.finish()]);
        await buffer.mapAsync(GPUMapMode.READ);
        const mapped = new Uint8Array(buffer.getMappedRange()), pixels = new Uint8Array(row * height);
        for (let y = 0; y < height; y++)
            pixels.set(mapped.subarray(y * stride, y * stride + row), y * row);
        buffer.unmap();
        buffer.destroy();
        return pixels;
    }
}
