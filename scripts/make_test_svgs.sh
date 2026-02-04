#!/usr/bin/env bash
set -euo pipefail

OUTDIR="assets/svg"
mkdir -p "$OUTDIR"

OUTDIR="$OUTDIR" python3 - <<'PY'
from pathlib import Path
import os
import textwrap

outdir = Path(os.environ.get("OUTDIR", "assets/svg"))
outdir.mkdir(parents=True, exist_ok=True)

def write(name: str, content: str) -> None:
    path = outdir / name
    path.write_text(textwrap.dedent(content).strip() + "\n", encoding="utf-8")

write("basic_shapes.svg", r"""
<svg xmlns="http://www.w3.org/2000/svg" width="320" height="200" viewBox="0 0 320 200">
  <defs>
    <linearGradient id="grad1" x1="0%" y1="0%" x2="100%" y2="0%">
      <stop offset="0%" stop-color="#ff6b6b"/>
      <stop offset="100%" stop-color="#4d96ff"/>
    </linearGradient>
    <radialGradient id="grad2" cx="50%" cy="50%" r="50%">
      <stop offset="0%" stop-color="#fff3b0"/>
      <stop offset="100%" stop-color="#f4a261"/>
    </radialGradient>
    <pattern id="pat" width="12" height="12" patternUnits="userSpaceOnUse" patternTransform="rotate(15)">
      <rect x="0" y="0" width="12" height="12" fill="#e0fbfc"/>
      <path d="M0 0 L12 12 M12 0 L0 12" stroke="#98c1d9" stroke-width="2"/>
    </pattern>
    <style>
      .outline { fill: none; stroke: #222; stroke-width: 3; }
      .soft { opacity: 0.9; }
    </style>
  </defs>

  <rect x="10" y="10" width="120" height="60" rx="10" ry="10"
        fill="url(#grad1)" stroke="#1b1b1b" stroke-width="4"/>
  <circle cx="200" cy="40" r="28" fill="url(#grad2)" stroke="#333" stroke-width="3"/>
  <ellipse cx="280" cy="45" rx="25" ry="15" fill="url(#pat)" stroke="#333" stroke-width="2"/>
  <line x1="10" y1="90" x2="140" y2="90" stroke="#2b2d42" stroke-width="6" stroke-linecap="round"/>
  <polyline points="170,90 200,110 230,90 260,110 290,90"
            fill="none" stroke="#ef476f" stroke-width="4" stroke-linejoin="round" stroke-dasharray="6 4"/>
  <polygon points="30,120 90,120 110,170 60,190 10,170"
           fill="#a8dadc" stroke="#1d3557" stroke-width="3" stroke-linejoin="bevel"/>
  <path d="M150 150 A30 20 30 0 1 210 170 A30 20 30 0 1 270 150"
        class="outline soft"/>
  <path d="M150 150 A30 20 30 0 1 210 170 A30 20 30 0 1 270 150"
        stroke="#8d99ae" stroke-width="4" fill="none" stroke-linecap="square"/>
</svg>
""")

write("text.svg", r"""
<svg xmlns="http://www.w3.org/2000/svg" width="320" height="200" viewBox="0 0 320 200">
  <defs>
    <path id="curve" d="M20 150 C80 110 140 190 300 150"/>
  </defs>
  <text x="12" y="30" font-size="18" fill="#1d3557">
    Hello <tspan fill="#e63946">SVG</tspan>
  </text>
  <text x="12" y="60" font-size="12" letter-spacing="2" word-spacing="4" fill="#457b9d">
    Spaced text test
  </text>
  <text x="12" y="90" font-size="14" fill="#2a9d8f">
    <tspan x="12" dy="0">Line one</tspan>
    <tspan x="12" dy="16" fill="#e76f51">Line two</tspan>
  </text>
  <text font-size="14" fill="#264653">
    <textPath href="#curve" startOffset="0%">Text on a path</textPath>
  </text>
</svg>
""")

write("clip_mask.svg", r"""
<svg xmlns="http://www.w3.org/2000/svg" width="320" height="200" viewBox="0 0 320 200">
  <defs>
    <clipPath id="clip1">
      <circle cx="80" cy="70" r="45"/>
    </clipPath>
    <mask id="mask1">
      <rect x="0" y="0" width="320" height="200" fill="white"/>
      <circle cx="240" cy="70" r="35" fill="black"/>
    </mask>
    <linearGradient id="grad3" x1="0%" y1="0%" x2="0%" y2="100%">
      <stop offset="0%" stop-color="#ffadad"/>
      <stop offset="100%" stop-color="#ffd6a5"/>
    </linearGradient>
  </defs>
  <rect x="20" y="20" width="120" height="100" fill="url(#grad3)" clip-path="url(#clip1)"/>
  <rect x="180" y="20" width="120" height="100" fill="#90be6d" mask="url(#mask1)"/>
  <rect x="20" y="140" width="280" height="40" fill="#577590"/>
</svg>
""")

write("hsl_use_masktype.svg", r"""
<svg xmlns="http://www.w3.org/2000/svg" width="320" height="200" viewBox="0 0 320 200">
  <defs>
    <path id="star" d="M20 0 L24 14 L38 14 L27 22 L31 36 L20 27 L9 36 L13 22 L2 14 L16 14 Z"/>
    <mask id="alphaMask" maskUnits="userSpaceOnUse" mask-type="alpha">
      <rect x="0" y="0" width="320" height="200" fill="black" fill-opacity="0"/>
      <rect x="180" y="20" width="120" height="80" fill="white" fill-opacity="0.65"/>
      <circle cx="240" cy="60" r="18" fill="white" fill-opacity="1"/>
    </mask>
  </defs>

  <rect x="20" y="20" width="120" height="60" fill="hsl(32, 100%, 55%)"/>
  <circle cx="170" cy="50" r="28" fill="hsla(210, 95%, 45%, 0.75)"/>

  <g transform="translate(0 85)">
    <use href="#star" x="20" y="0" fill="#e63946" stroke="#1d3557" stroke-width="1.5"/>
    <use href="#star" x="70" y="0" fill="#f4a261" stroke="#1d3557" stroke-width="1.5"/>
    <use href="#star" x="120" y="0" fill="#2a9d8f" stroke="#1d3557" stroke-width="1.5"/>
  </g>

  <rect x="180" y="20" width="120" height="80" fill="#fb8500" mask="url(#alphaMask)"/>
  <rect x="180" y="120" width="120" height="60" fill="#264653"/>
</svg>
""")

write("use_visibility.svg", r"""
<svg xmlns="http://www.w3.org/2000/svg" width="320" height="220" viewBox="0 0 320 220">
  <defs>
    <rect id="tileRect" x="0" y="0" width="36" height="24" rx="4" ry="4"
          fill="#8ecae6" stroke="#1d3557" stroke-width="2"/>
    <circle id="tileCircle" cx="18" cy="12" r="9" fill="#ffb703" stroke="#fb8500" stroke-width="2"/>
    <polygon id="tilePoly" points="2,22 18,2 34,22" fill="#90be6d" stroke="#2d6a4f" stroke-width="2"/>
  </defs>

  <rect x="0" y="0" width="320" height="220" fill="#f1faee"/>

  <use href="#tileRect" x="20" y="20"/>
  <use href="#tileCircle" x="70" y="20"/>
  <use href="#tilePoly" x="120" y="20"/>

  <use href="#tileRect" x="20" y="60" fill="#ffafcc" stroke="#6d597a"/>
  <use href="#tileCircle" x="70" y="60" fill="#ffd166" stroke="#9c6644"/>
  <use href="#tilePoly" x="120" y="60" fill="#2a9d8f" stroke="#264653"/>

  <g visibility="hidden">
    <use href="#tileRect" x="20" y="110"/>
    <use href="#tileCircle" x="70" y="110"/>
    <use href="#tilePoly" x="120" y="110"/>
  </g>

  <g display="none">
    <rect x="180" y="20" width="110" height="70" fill="#e63946"/>
  </g>

  <g visibility="hidden">
    <g visibility="visible">
      <rect x="180" y="110" width="110" height="70" fill="#457b9d"/>
      <text x="188" y="150" font-size="12" fill="#ffffff">visible child</text>
    </g>
  </g>
</svg>
""")

write("use_opacity.svg", r"""
<svg xmlns="http://www.w3.org/2000/svg" width="320" height="200" viewBox="0 0 320 200">
  <defs>
    <rect id="box" x="0" y="0" width="64" height="44" rx="6" ry="6"
          fill="#457b9d" stroke="#1d3557" stroke-width="4"/>
    <linearGradient id="g" x1="0%" y1="0%" x2="100%" y2="0%">
      <stop offset="0%" stop-color="#ff006e" stop-opacity="20%"/>
      <stop offset="100%" stop-color="#3a86ff" stop-opacity="80%"/>
    </linearGradient>
    <filter id="flood" x="-20%" y="-20%" width="140%" height="140%">
      <feFlood flood-color="#fb8500" flood-opacity="35%" result="f"/>
      <feComposite in="f" in2="SourceGraphic" operator="atop"/>
    </filter>
  </defs>

  <rect x="0" y="0" width="320" height="200" fill="#ffffff"/>

  <use href="#box" x="20" y="24"/>
  <use href="#box" x="100" y="24" fill="#e63946" stroke="#6a040f"/>
  <use href="#box" x="180" y="24" style="fill:#2a9d8f;stroke:#264653"/>

  <rect x="20" y="96" width="90" height="64" fill="url(#g)" stroke="#111" stroke-width="2"/>
  <rect x="130" y="96" width="90" height="64" fill="#8338ec" fill-opacity="40%" stroke="#111" stroke-width="2"/>
  <rect x="240" y="96" width="60" height="64" fill="#ffbe0b" opacity="55%" filter="url(#flood)"/>
</svg>
""")

write("symbol_marker_css.svg", r"""
<svg xmlns="http://www.w3.org/2000/svg" width="360" height="220" viewBox="0 0 360 220">
  <defs>
    <symbol id="badge" viewBox="0 0 40 40">
      <rect x="2" y="2" width="36" height="36" rx="8" ry="8" fill="#8ecae6" stroke="#1d3557" stroke-width="3"/>
      <circle cx="20" cy="20" r="9" fill="#ffb703"/>
    </symbol>
    <marker id="arrow" refX="10" refY="5" markerWidth="10" markerHeight="10" orient="auto" markerUnits="strokeWidth" viewBox="0 0 10 10">
      <path d="M0 0 L10 5 L0 10 z" fill="#264653"/>
    </marker>
    <style>
      g rect { stroke: #1d3557; stroke-width: 2; }
      g > rect { fill: #ffd166; }
      .nested rect { fill: #ef476f; }
      .nested > rect { fill: #06d6a0; }
    </style>
  </defs>

  <rect x="0" y="0" width="360" height="220" fill="#f8f9fa"/>

  <use href="#badge" x="20" y="16" width="48" height="48"/>
  <use href="#badge" x="90" y="16" width="64" height="64"/>
  <use href="#badge" x="180" y="16" width="40" height="40"/>

  <path d="M30 120 L160 120 L220 170" fill="none" stroke="#457b9d" stroke-width="4"
        marker-start="url(#arrow)" marker-mid="url(#arrow)" marker-end="url(#arrow)"/>

  <g transform="translate(240 96)">
    <rect x="0" y="0" width="90" height="40"/>
    <g class="nested" transform="translate(0 50)">
      <rect x="0" y="0" width="90" height="34"/>
      <g transform="translate(8 6)">
        <rect x="0" y="0" width="28" height="20"/>
      </g>
    </g>
  </g>
</svg>
""")

write("text_marker_orient.svg", r"""
<svg xmlns="http://www.w3.org/2000/svg" width="360" height="220" viewBox="0 0 360 220">
  <defs>
    <marker id="arrowRev" refX="10" refY="5" markerWidth="10" markerHeight="10" orient="auto-start-reverse" viewBox="0 0 10 10">
      <path d="M0 0 L10 5 L0 10 z" fill="#1d3557"/>
    </marker>
  </defs>

  <rect x="0" y="0" width="360" height="220" fill="#ffffff"/>
  <line x1="30" y1="40" x2="330" y2="40" stroke="#457b9d" stroke-width="5"
        marker-start="url(#arrowRev)" marker-end="url(#arrowRev)"/>

  <line x1="30" y1="84" x2="330" y2="84" stroke="#2a9d8f" stroke-width="4"
        marker-start="url(#arrowRev)" marker-mid="url(#arrowRev)" marker-end="url(#arrowRev)"/>

  <text x="24" y="140" font-size="20" fill="#111827">alphabetic baseline</text>
  <text x="24" y="170" font-size="20" dominant-baseline="middle" fill="#111827">middle baseline</text>
  <text x="24" y="200" font-size="20" dominant-baseline="hanging" fill="#111827">hanging baseline</text>

  <line x1="20" y1="140" x2="340" y2="140" stroke="#e63946" stroke-width="1"/>
  <line x1="20" y1="170" x2="340" y2="170" stroke="#e63946" stroke-width="1"/>
  <line x1="20" y1="200" x2="340" y2="200" stroke="#e63946" stroke-width="1"/>
</svg>
""")

write("symbol_use_par.svg", r"""
<svg xmlns="http://www.w3.org/2000/svg" width="360" height="200" viewBox="0 0 360 200">
  <defs>
    <symbol id="s1" viewBox="0 0 100 50" preserveAspectRatio="xMidYMid meet">
      <rect x="0" y="0" width="100" height="50" fill="#8ecae6"/>
      <circle cx="50" cy="25" r="18" fill="#ffb703"/>
    </symbol>
    <symbol id="s2" viewBox="0 0 100 50" preserveAspectRatio="none">
      <rect x="0" y="0" width="100" height="50" fill="#bde0fe"/>
      <circle cx="50" cy="25" r="18" fill="#e63946"/>
    </symbol>
  </defs>
  <rect x="0" y="0" width="360" height="200" fill="#ffffff"/>
  <use href="#s1" x="20" y="20" width="140" height="80"/>
  <use href="#s1" x="190" y="20" width="140" height="40"/>
  <use href="#s2" x="20" y="120" width="140" height="40"/>
  <use href="#s2" x="190" y="120" width="140" height="80"/>
</svg>
""")

write("css_attr_selectors.svg", r"""
<svg xmlns="http://www.w3.org/2000/svg" width="320" height="200" viewBox="0 0 320 200">
  <defs>
    <style>
      [data-role] { opacity: 0.85; }
      [data-role="target"] { fill: #06d6a0; }
      g[data-group] rect { stroke: #1d3557; stroke-width: 3; }
      g[data-group] > rect { fill: #ffd166; }
      g[data-group] [data-role="target"] { fill: #ef476f; }
    </style>
  </defs>
  <rect x="0" y="0" width="320" height="200" fill="#f8f9fa"/>
  <g data-group="alpha" transform="translate(20 20)">
    <rect x="0" y="0" width="120" height="50" data-role="plain"/>
    <rect x="0" y="70" width="120" height="50" data-role="target"/>
  </g>
  <g transform="translate(180 20)">
    <rect x="0" y="0" width="120" height="50" data-role="target"/>
    <rect x="0" y="70" width="120" height="50"/>
  </g>
</svg>
""")

write("css_sibling_bullet_blend.svg", r"""
<svg xmlns="http://www.w3.org/2000/svg" width="360" height="220" viewBox="0 0 360 220">
  <defs>
    <style>
      .a + .b { fill: #06d6a0; }
      .a ~ .c { fill: #ef476f; }
    </style>
    <filter id="blendOverlay" x="-10%" y="-10%" width="120%" height="120%">
      <feFlood flood-color="#118ab2" flood-opacity="70%" result="f"/>
      <feBlend mode="overlay" in="SourceGraphic" in2="f"/>
    </filter>
  </defs>

  <rect x="0" y="0" width="360" height="220" fill="#ffffff"/>
  <rect class="a" x="20" y="20" width="70" height="36" fill="#ffd166"/>
  <rect class="b" x="100" y="20" width="70" height="36" fill="#cccccc"/>
  <rect class="c" x="180" y="20" width="70" height="36" fill="#cccccc"/>
  <rect class="d" x="260" y="20" width="70" height="36" fill="#cccccc"/>

  <text x="20" y="110" font-size="20" fill="#111827">• bullet one</text>
  <text x="20" y="142" font-size="20" fill="#111827">• bullet two</text>

  <rect x="20" y="170" width="120" height="34" fill="#ffd166" filter="url(#blendOverlay)"/>
</svg>
""")

write("filters_basic.svg", r"""
<svg xmlns="http://www.w3.org/2000/svg" width="320" height="200" viewBox="0 0 320 200">
  <defs>
    <filter id="f1" x="-20%" y="-20%" width="140%" height="140%">
      <feGaussianBlur in="SourceGraphic" stdDeviation="2" result="blur"/>
      <feOffset in="blur" dx="4" dy="4" result="off"/>
      <feColorMatrix in="off" type="saturate" values="0.2" result="desat"/>
      <feComponentTransfer in="desat" result="ct">
        <feFuncR type="gamma" amplitude="1" exponent="0.8" offset="0"/>
        <feFuncG type="linear" slope="0.9" intercept="0.05"/>
        <feFuncB type="linear" slope="0.9" intercept="0.05"/>
        <feFuncA type="linear" slope="1" intercept="0"/>
      </feComponentTransfer>
      <feMorphology in="ct" operator="dilate" radius="1" result="morph"/>
      <feMerge>
        <feMergeNode in="morph"/>
        <feMergeNode in="SourceGraphic"/>
      </feMerge>
    </filter>
  </defs>
  <rect x="40" y="30" width="120" height="80" rx="10" ry="10"
        fill="#3a86ff" stroke="#222" stroke-width="4" filter="url(#f1)"/>
  <circle cx="230" cy="70" r="35" fill="#ffbe0b" stroke="#333" stroke-width="3"/>
</svg>
""")

write("filters_advanced.svg", r"""
<svg xmlns="http://www.w3.org/2000/svg" width="320" height="220" viewBox="0 0 320 220">
  <defs>
    <filter id="conv" x="-10%" y="-10%" width="120%" height="120%">
      <feConvolveMatrix order="3" kernelMatrix="0 -1 0 -1 5 -1 0 -1 0" edgeMode="duplicate"/>
    </filter>
    <filter id="disp" x="-10%" y="-10%" width="120%" height="120%">
      <feTurbulence type="turbulence" baseFrequency="0.03" numOctaves="2" seed="3" result="noise"/>
      <feDisplacementMap in="SourceGraphic" in2="noise" scale="12" xChannelSelector="R" yChannelSelector="G"/>
    </filter>
    <filter id="light" x="-20%" y="-20%" width="140%" height="140%">
      <feDiffuseLighting in="SourceAlpha" surfaceScale="4" diffuseConstant="1.2" lighting-color="#ffd6a5">
        <feDistantLight azimuth="45" elevation="60"/>
      </feDiffuseLighting>
    </filter>
    <filter id="spec" x="-20%" y="-20%" width="140%" height="140%">
      <feSpecularLighting in="SourceAlpha" surfaceScale="6" specularConstant="1.0" specularExponent="12" lighting-color="#ffffff">
        <feDistantLight azimuth="30" elevation="50"/>
      </feSpecularLighting>
    </filter>
    <filter id="tile" x="0" y="0" width="100%" height="100%">
      <feTile in="SourceGraphic"/>
    </filter>
    <filter id="img" x="0" y="0" width="100%" height="100%">
      <feImage href="assets/basic.png" x="0" y="0" width="120" height="80" result="img"/>
      <feComposite in="img" in2="SourceGraphic" operator="over"/>
    </filter>
  </defs>

  <rect x="20" y="20" width="120" height="60" fill="#8ecae6" filter="url(#conv)"/>
  <rect x="170" y="20" width="120" height="60" fill="#ffafcc" filter="url(#disp)"/>
  <rect x="20" y="100" width="120" height="60" fill="#a7c957" filter="url(#light)"/>
  <rect x="170" y="100" width="120" height="60" fill="#f77f00" filter="url(#spec)"/>
  <rect x="20" y="180" width="120" height="30" fill="#e9c46a" filter="url(#tile)"/>
  <rect x="170" y="170" width="120" height="40" fill="#f4a261" filter="url(#img)"/>
</svg>
""")

write("filter_inputs.svg", r"""
<svg xmlns="http://www.w3.org/2000/svg" width="320" height="200" viewBox="0 0 320 200">
  <defs>
    <linearGradient id="bg" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" stop-color="#219ebc"/>
      <stop offset="100%" stop-color="#023047"/>
    </linearGradient>
    <filter id="paintmix" x="-10%" y="-10%" width="120%" height="120%">
      <feComposite in="FillPaint" in2="SourceGraphic" operator="in" result="filled"/>
      <feComposite in="StrokePaint" in2="SourceGraphic" operator="in" result="stroked"/>
      <feBlend in="filled" in2="stroked" mode="multiply" result="mix"/>
      <feComposite in="mix" in2="BackgroundImage" operator="over" result="out"/>
      <feComposite in="out" in2="BackgroundAlpha" operator="atop"/>
    </filter>
  </defs>
  <rect x="0" y="0" width="320" height="200" fill="url(#bg)"/>
  <rect x="40" y="40" width="240" height="120" rx="16" ry="16"
        fill="#ffb703" stroke="#fb8500" stroke-width="10" filter="url(#paintmix)"/>
</svg>
""")

print(f"Wrote SVG test files to {outdir}")
PY
