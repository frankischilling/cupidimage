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
