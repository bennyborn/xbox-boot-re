// The workbench of W3 (docs/web.md 10): what each draw of xb_frame belongs
// to, where its specification stands, the events of the time line, and a
// small reader for the narratives in docs/. Used by the page and, for the
// test of the links, by hosts/web/test_w3.mjs in node.

// XB_STEP_* and XB_DRAW_* of xboot.h
const U1 = 0x0B01, U2 = 0x0B02, U3 = 0x0B03, U4 = 0x0B04, D5 = 0x0D05;
const CHAMBER = 1, BLOB_GLOW = 2, BLOB_MESH = 3, BLOB_DROP = 4, LOGO_A = 5, LOGO_Q1 = 9, LOGO_Q2 = 10,
      LOGO_E = 11, LOGO_IMAGE = 12, VEIL = 14, GLOW = 15, PRESENT = 0;
const MESH_CHAMBER = 1;

// The parts of the picture a person can switch, as keys of keyOf. Each
// names its narrative section and the rows of docs/decomp/README.md.
export const PARTS = {
    chamber0: { name: 'Chamber: spheres', group: 'Chamber', colour: [1, 0.5, 0.2], file: 'Chamber.md', sec: '7', rows: [0x8005F897, 0x8005EE08, 0x8005C9DC, 0x80076AC0] },
    chamber1: { name: 'Chamber: cylinders', group: 'Chamber', colour: [0.9, 0.9, 0.2], file: 'Chamber.md', sec: '7', rows: [0x8005F897, 0x8005EE08, 0x8005CBC8, 0x80076C88] },
    chamber2: { name: 'Chamber: boxes', group: 'Chamber', colour: [0.3, 0.9, 0.3], file: 'Chamber.md', sec: '7', rows: [0x8005F897, 0x8005EE08, 0x8005D398, 0x80077040] },
    chamber3: { name: 'Chamber: rings', group: 'Chamber', colour: [0.3, 0.8, 1], file: 'Chamber.md', sec: '7', rows: [0x8005F897, 0x8005EE08, 0x8005D74F, 0x80077700] },
    chamber4: { name: 'Chamber: frustums', group: 'Chamber', colour: [0.6, 0.4, 1], file: 'Chamber.md', sec: '7', rows: [0x8005F897, 0x8005EE08, 0x8005CF51, 0x80077C78] },
    chamber5: { name: 'Chamber: lathes', group: 'Chamber', colour: [1, 0.4, 0.7], file: 'Chamber.md', sec: '7', rows: [0x8005F897, 0x8005EE08, 0x8005E210, 0x80077E40] },
    blob_glow: { name: 'Blob: glow surface', group: 'Blob', colour: [1, 1, 0.6], file: 'Blob.md', sec: '7.1', rows: [0x800583BF] },
    blob_mesh: { name: 'Blob: mesh', group: 'Blob', colour: [0.6, 1, 0.6], file: 'Blob.md', sec: '7.2', rows: [0x800583BF, 0x80058B93, 0x800662E8] },
    blob_drop: { name: 'Blob: drop', group: 'Blob', colour: [0.4, 1, 1], file: 'Blob.md', sec: '7.3', rows: [0x800583BF, 0x800590A6, 0x80066470] },
    logo_a: { name: 'Logo: mesh A', group: 'Logo', colour: [1, 0.3, 0.3], file: 'Logo.md', sec: '5.1', rows: [0x8005BC63, 0x80072AA0] },
    logo_b: { name: 'Logo: mesh B', group: 'Logo', colour: [0.3, 1, 0.3], file: 'Logo.md', sec: '5.1', rows: [0x8005BC63, 0x800735F0] },
    logo_c: { name: 'Logo: mesh C', group: 'Logo', colour: [0.3, 0.5, 1], file: 'Logo.md', sec: '5.1', rows: [0x8005BC63, 0x80073428] },
    logo_d: { name: 'Logo: mesh D', group: 'Logo', colour: [1, 1, 0.3], file: 'Logo.md', sec: '5.1', rows: [0x8005BC63, 0x80071008, 0x80066868] },
    logo_tm: { name: 'Logo: TM', group: 'Logo', colour: [1, 0.5, 1], file: 'Logo.md', sec: '5.3', rows: [0x8005BC63, 0x800739DC, 0x80073A0C, 0x80073FD0] },
    logo_text: { name: 'Logo: lettering', group: 'Logo', colour: [0.5, 1, 1], file: 'Logo.md', sec: '5.3', rows: [0x8005BC63, 0x80073A40, 0x80073FB8] },
    logo_image: { name: 'Logo: as image', group: 'Logo', colour: [1, 0.8, 0.5], file: 'Logo.md', sec: '5.2', rows: [0x8005BC63, 0x8005C366] },
    veil: { name: 'Veil', group: 'Veil', colour: [0.4, 1, 0.4], file: 'Effects.md', sec: '4', rows: [0x80059BCD, 0x80066690, 0x80066598] },
    glow: { name: 'Glow', group: 'Veil', colour: [1, 1, 1], file: 'Effects.md', sec: '5', rows: [0x80059BCD] },
};

// Steps that draw into targets the picture reads, not onto the screen.
export const TARGETS = {
    light: { name: 'Light views (U1)', file: 'Chamber.md', sec: '8', rows: [0x800600F5, 0x8005FDF9] },
    fxtarget: { name: 'Target of the veil (U2, U3)', file: 'Effects.md', sec: '3', rows: [0x8005A194, 0x8005FDD6, 0x80065D28] },
    logotex: { name: 'Texture of the logo (U4)', file: 'Logo.md', sec: '6', rows: [0x8005C366] },
    present: { name: 'Finish the frame (D5)', file: 'GPU.md', sec: '7', rows: [0x8003E550] },
};

// Subsystems without a draw of their own, reached from the panels.
export const OTHER = {
    camera: { name: 'Camera', file: 'Camera.md', sec: '5.5', rows: [0x8005AC32, 0x8005A6CB, 0x80070DB8] },
    timing: { name: 'Time', file: 'Timing.md', sec: '4', rows: [0x800571AD, 0x80057949, 0x80057011] },
    textures: { name: 'Textures', file: 'Textures.md', sec: '1', rows: [0x8005768C] },
    sound: { name: 'Sound', file: 'Audio.md', sec: '1', rows: [0x8006166D, 0x80060252] },
    frame: { name: 'Order', file: 'FrameOrder.md', sec: '3', rows: [0x80056D0B, 0x800571AD] },
};

// The narrative of a program, by its address (GPU.md 3), or of the fixed
// function and the combiners from the stages (GPU.md 2.1, 5).
export const PROGRAMS = { 0x80065B60: '3.1', 0x80065998: '3.2', 0x80065D28: '3.3', 0x800662E8: '3.4',
                          0x80066470: '3.5', 0x80066690: '3.6', 0x80066868: '3.7' };

// The key of a draw of xb_frame.
export function keyOf(d) {
    if (d.step === U1)
        return 'light';
    if (d.step === U2 || d.step === U3)
        return 'fxtarget';
    if (d.step === U4)
        return 'logotex';
    if (d.step === D5 || d.what === PRESENT)
        return 'present';
    switch (d.what) {
    case CHAMBER: return 'chamber' + (d.mesh - MESH_CHAMBER);
    case BLOB_GLOW: return 'blob_glow';
    case BLOB_MESH: return 'blob_mesh';
    case BLOB_DROP: return 'blob_drop';
    case LOGO_Q1: case LOGO_Q2: return 'logo_tm';
    case LOGO_E: return 'logo_text';
    case LOGO_IMAGE: return 'logo_image';
    case VEIL: return 'veil';
    case GLOW: return 'glow';
    }
    if (d.what >= LOGO_A && d.what < LOGO_A + 4)
        return 'logo_' + 'abcd'[d.what - LOGO_A];
    return 'other';
}

export const describe = (key) => PARTS[key] || TARGETS[key] || OTHER[key];

// What decides whether a part is seen where it is drawn, for picking: some
// draws cover the whole screen and add only a little light. 0 everywhere,
// 1 by alpha, 2 by alpha times brightness, 3 by brightness (shaders.js, fin).
// The limit is about 8 of 255 steps of light added or of opacity.
export const SEEN = { blob_glow: [2, 0.03], veil: [3, 0.03], glow: [2, 0.03], logo_tm: [1, 0.03], logo_image: [1, 0.03] };

// Events of the time line in seconds of t, each with where it stands. The
// times that depend on the run (pulses, the camera's switches) come from
// the core in events() below.
const FIXED = [
    { t: 0, what: 'Start', file: 'Timing.md', sec: '3' },
    { t: 0.12, what: 'Glow at its brightest', file: 'Effects.md', sec: '5' },
    { t: 0.6, what: 'Blob: drawn and deformed', file: 'Blob.md', sec: '1' },
    { t: 0.85, what: 'Chamber tracks begin', file: 'Chamber.md', sec: '6' },
    { t: 1.1, what: 'Ramp x1c begins', file: 'Timing.md', sec: '4' },
    { t: 1.171, what: 'last parts with NaN', file: 'Chamber.md', sec: '6' },
    { t: 4.7, what: 'Glow rises again', file: 'Effects.md', sec: '5' },
    { t: 5.0, what: 'Flash: K1 rises until 5.25', file: 'Effects.md', sec: '4' },
    { t: 5.2, what: 'Chamber: detail level 1', file: 'Chamber.md', sec: '6' },
    { t: 5.35, what: 'Logo: constants jump', file: 'Logo.md', sec: '7', path0: true },
    { t: 6.0, what: 'X into the texture, TM and lettering', file: 'Logo.md', sec: '6', path0: true },
    { t: 6.25, what: 'Logo: only the picture now', file: 'Logo.md', sec: '6', path0: true },
    { t: 8.0, what: 'End', file: 'Timing.md', sec: '3' },
];

// All events for this run, sorted: the fixed ones, the twelve pulses
// (Timing.md 4) and where the camera switches this+0xE and this+0xD
// (Camera.md 5.5), found by stepping t in 1 ms.
// Times marked path0 stand in the narratives for path 0 only (Logo.md 7);
// with another path (xb_select_path) they are left out.
export function events(core, path = 0) {
    const out = FIXED.filter((e) => !e.path0 || path === 0).map((e) => ({ ...e }));
    core.pulses().forEach(([t0, w, a], i) =>
        out.push({ t: t0, what: `Pulse ${i + 1}: width ${w.toFixed(3)}, height ${a.toFixed(3)}`, file: 'Timing.md', sec: '4', pulse: true }));
    let prev = core.camera(0);
    for (let ms = 1; ms <= 8000; ms++) {
        const c = core.camera(ms / 1000);
        if (c.logoOn !== prev.logoOn)
            out.push({ t: ms / 1000, what: `Logo ${c.logoOn ? 'on' : 'off'} (this+0xE)`, file: 'Camera.md', sec: '5.5' });
        if (c.chamberOn !== prev.chamberOn)
            out.push({ t: ms / 1000, what: `Chamber and blob ${c.chamberOn ? 'on' : 'off'} (this+0xD)`, file: 'Camera.md', sec: '5.5' });
        prev = c;
    }
    return out.sort((a, b) => a.t - b.t);
}

// What the picture shows at t, for the status line: from the camera's
// switches this+0xD and this+0xE (Camera.md 5.5), the logo's phase
// (Logo.md 6) and the times of Blob.md 1 and Effects.md 4.
export function phase(t, cam, logo, done) {
    if (done)
        return 'End';
    if (t < 0.6)
        return 'Glow';
    if (t >= 5.0 && t < 5.25)
        return 'Flash';
    if (cam.chamberOn && !cam.logoOn)
        return 'Blob and chamber';
    if (cam.chamberOn)
        return 'Logo arriving, chamber still there';
    return logo.phase ? 'Final picture' : 'Logo';
}

// ---- the narratives -----------------------------------------------------------

// The row of docs/decomp/README.md for an address, as its cells, or null.
export function readmeRow(readme, addr) {
    const at = '0x' + addr.toString(16).toUpperCase().padStart(8, '0');
    const line = readme.split('\n').find((l) => l.startsWith(`| ${at} |`));
    return line ? line.split('|').slice(1, -1).map((c) => c.trim()) : null;
}

// The section `sec` ("7", "7.2") of a narrative: from its heading to the
// next heading of the same or a higher level; null if there is none.
export function section(text, sec) {
    const lines = text.split('\n');
    const start = lines.findIndex((l) => /^#+ /.test(l) &&
        (l.replace(/^#+ /, '').startsWith(sec + '. ') || l.replace(/^#+ /, '').startsWith(sec + ' ')));
    if (start < 0)
        return null;
    const level = lines[start].indexOf(' ');
    let end = start + 1;
    while (end < lines.length && !(/^#+ /.test(lines[end]) && lines[end].indexOf(' ') <= level))
        end++;
    return lines.slice(start, end).join('\n');
}

const esc = (s) => s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
const inline = (s) => esc(s).replace(/`([^`]+)`/g, '<code>$1</code>').replace(/\*\*([^*]+)\*\*/g, '<b>$1</b>')
                            .replace(/\*([^*]+)\*/g, '<i>$1</i>');

// Enough Markdown for the narratives: headings, paragraphs, lists, tables
// and code blocks.
export function markdown(text) {
    const out = [], lines = text.replace(/\r/g, '').split('\n');
    for (let i = 0; i < lines.length;) {
        const l = lines[i];
        if (l.startsWith('```')) {
            const body = [];
            for (i++; i < lines.length && !lines[i].startsWith('```'); i++)
                body.push(lines[i]);
            i++;
            out.push(`<pre>${esc(body.join('\n'))}</pre>`);
        } else if (/^#+ /.test(l)) {
            const n = Math.min(l.indexOf(' ') + 1, 6);
            out.push(`<h${n}>${inline(l.slice(l.indexOf(' ') + 1))}</h${n}>`);
            i++;
        } else if (l.startsWith('|')) {
            const rows = [];
            for (; i < lines.length && lines[i].startsWith('|'); i++)
                if (!/^\|[-| :]+\|$/.test(lines[i]))
                    rows.push(lines[i].split('|').slice(1, -1).map((c) => `<td>${inline(c.trim())}</td>`).join(''));
            out.push(`<table>${rows.map((r) => `<tr>${r}</tr>`).join('')}</table>`);
        } else if (/^\s*- /.test(l)) {
            const items = [];
            for (; i < lines.length && (/^\s*- /.test(lines[i]) || /^\s{2,}\S/.test(lines[i])); i++)
                if (/^\s*- /.test(lines[i]))
                    items.push(lines[i].replace(/^\s*- /, ''));
                else
                    items[items.length - 1] += ' ' + lines[i].trim();
            out.push(`<ul>${items.map((x) => `<li>${inline(x)}</li>`).join('')}</ul>`);
        } else if (l.trim()) {
            const para = [];
            for (; i < lines.length && lines[i].trim() && !/^(#+ |\||```|\s*- )/.test(lines[i]); i++)
                para.push(lines[i]);
            out.push(`<p>${inline(para.join(' '))}</p>`);
        } else {
            i++;
        }
    }
    return out.join('\n');
}
