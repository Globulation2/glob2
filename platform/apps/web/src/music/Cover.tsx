import type { MusicRelease } from '@glob2/protocol';
const PALETTES = [
  ['#29203f', '#425653', '#62816b', '#b5bd79'],
  ['#242b46', '#3b5867', '#6e9493', '#c5ba84'],
  ['#30213f', '#67435a', '#987166', '#d4ae7a'],
];
export function Cover({ release }: { release: MusicRelease }) {
  if (release.coverUrl)
    return (
      <img className="music-cover" src={release.coverUrl} alt={`${release.metadata.title} cover`} />
    );
  const seed = [...release.id].reduce(
    (hash, c) => (Math.imul(hash, 31) + c.charCodeAt(0)) >>> 0,
    0,
  );
  const palette = PALETTES[seed % PALETTES.length] ?? ['#29203f', '#425653', '#62816b', '#b5bd79'];
  const shift = seed % 35;
  return (
    <svg
      className="music-cover music-cover-terrain"
      viewBox="0 0 240 240"
      role="img"
      aria-label={`${release.metadata.title} terrain illustration`}
    >
      <rect width="240" height="240" fill={palette[0]} />
      <circle cx={170 + shift / 3} cy="54" r="25" fill="#ecd08a" opacity="0.85" />
      {[0, 1, 2].map((layer) => (
        <path
          key={layer}
          d={`M-20 ${95 + layer * 45} Q${45 + shift} ${35 + layer * 44} 125 ${105 + layer * 37} T260 ${80 + layer * 52} V260 H-20Z`}
          fill={palette[layer + 1]}
        />
      ))}
      {[0, 1, 2, 3].map((line) => (
        <path
          key={line}
          d={`M-10 ${155 + line * 19} Q70 ${110 + line * 20} 130 ${170 + line * 12} T255 ${150 + line * 19}`}
          fill="none"
          stroke="#f8e6ac"
          strokeWidth="1"
          opacity="0.22"
        />
      ))}
      <g
        transform={`translate(${70 + shift}, 137)`}
        fill="#f3dfa5"
        stroke={palette[0]}
        strokeWidth="2"
      >
        <path d="M-16 9V-10L0-23l16 13V9Z" />
        <path d="M-8 9V-4h7V9" fill={palette[0]} />
        <path d="M22 18V2L33-7 44 2v16Z" />
        <circle cx="-28" cy="17" r="5" fill="#ddb869" />
        <circle cx="-39" cy="25" r="3" fill="#ddb869" />
        <circle cx="52" cy="25" r="4" fill="#ddb869" />
      </g>
      <g fill="#f8e6ac" opacity="0.65">
        <circle cx="35" cy="39" r="1.5" />
        <circle cx="84" cy="25" r="1" />
        <circle cx="115" cy="58" r="1.5" />
      </g>
    </svg>
  );
}
