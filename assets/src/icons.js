// Iconos genéricos de la barra recreada (formas planas, sin logotipos de nadie).
// Uso: <div class="bar glass" data-icons></div>
const ICONS = [
  ['#5fd4ff', 'grid'], ['#f6c343', 'folder'], ['#ff7a59', 'circle'], ['#7c8cff', 'chat'],
  ['#3ddc97', 'square'], ['#ff5fa2', 'play'], ['#c9d3e3', 'term'],
];
const glyph = (c, k) => ({
  grid: `<div style="display:grid;grid-template-columns:repeat(2,9px);gap:3px">${`<i style="width:9px;height:9px;background:${c}"></i>`.repeat(4)}</div>`,
  folder: `<i style="width:24px;height:18px;background:${c};border-radius:3px 3px 4px 4px"></i>`,
  circle: `<i style="width:22px;height:22px;border-radius:50%;background:conic-gradient(${c},#ffd166,${c})"></i>`,
  chat: `<i style="width:24px;height:18px;border-radius:9px;background:${c}"></i>`,
  square: `<i style="width:20px;height:20px;border-radius:6px;background:${c}"></i>`,
  play: `<i style="width:0;height:0;border-radius:0;border-left:18px solid ${c};border-top:11px solid transparent;border-bottom:11px solid transparent"></i>`,
  term: `<i style="width:24px;height:20px;border-radius:4px;background:#1b2233;border:2px solid ${c}"></i>`,
}[k]);
document.querySelectorAll('[data-icons]').forEach((bar) => {
  bar.innerHTML = ICONS.map(([c, k], i) => `<div class="ico${i < 3 ? ' on' : ''}">${glyph(c, k)}</div>`).join('') +
    '<div class="tray"><s></s><s></s><span>12:00<br>01/01/2026</span></div>';
});
