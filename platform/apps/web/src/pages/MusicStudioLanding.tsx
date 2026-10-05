import { Link } from '../router.tsx';
import { price, type Wallet } from './music-studio/types.ts';
export const MUSIC_IDEAS = [
  [
    'Moss & morning light',
    'A warm woodland piece with flute, harp and gentle pizzicato. Let the melody wander and return; combat adds quiet drums and a bassoon counterline.',
  ],
  [
    'Little clockwork garden',
    'Soft glass bells and rounded synth pads, curious and unhurried. A playful answering melody, with a deeper pulse as the colony grows.',
  ],
  [
    'A waltz for the rain',
    'A cozy folk waltz with recorder and kalimba. Sparse, breathing phrases for calm, a lilting rhythm for building, and warm low strings for combat.',
  ],
] as const;
export function MusicStudioLanding({
  wallet,
  busy,
  buy,
  threads,
  draft,
  setDraft,
}: {
  wallet: Wallet;
  busy: boolean;
  buy: (id: string) => void;
  threads: { id: string; title: string }[];
  draft: string;
  setDraft: (s: string) => void;
}) {
  return (
    <section className="mu-landing">
      <div className="mu-hero">
        <span className="ms-eyebrow">A SOUNDTRACK OF YOUR OWN</span>
        <h2>
          Small creatures.
          <br />A whole world of sound.
        </h2>
        <p>
          Describe a feeling. Shape a melody. Hear one piece grow from a quiet colony into a
          bustling world.
        </p>
        <div className="mu-score-art" aria-hidden="true">
          {Array.from({ length: 38 }, (_, i) => (
            <i
              key={i}
              style={{
                height: `${16 + (Math.sin(i * 0.7) + 1) * 33}%`,
                animationDelay: `${i * 0.05}s`,
              }}
            />
          ))}
        </div>
      </div>
      <div className="mu-idea-grid">
        {MUSIC_IDEAS.map(([name, prompt]) => (
          <button key={name} onClick={() => setDraft(prompt)}>
            <strong>{name}</strong>
            <span>{prompt}</span>
          </button>
        ))}
      </div>
      <label className="mu-draft">
        Your musical idea
        <textarea
          rows={4}
          maxLength={8000}
          value={draft}
          onChange={(e) => setDraft(e.target.value)}
          placeholder="Warm woodwinds, a curious melody, a little more movement as the colony grows…"
        />
      </label>
      <div className="mu-landing-bottom">
        <div>
          <h3>Compose. Listen. Make it yours.</h3>
          <p>
            Chat with your composer, follow each render and check, then refine any version. Every
            set includes calm, building and combat on the same timeline.
          </p>
          <p>Keep your music private or publish it to the community library.</p>
        </div>
        <div>
          <h3>One delivered set. One credit.</h3>
          <p>Includes up to two automatic repairs. Failed generations return your credit.</p>
          {wallet.packs.map((p) => (
            <button
              className="primary"
              key={p.id}
              disabled={busy || !wallet.enabled}
              onClick={() => buy(p.id)}
            >
              Buy {p.credits} credits · {price(p)}
            </button>
          ))}
          {!wallet.packs.length && <p>Sales are not available yet. Your idea stays saved here.</p>}
        </div>
      </div>
      {!!threads.length && (
        <nav aria-label="Saved compositions">
          <h3>Your compositions</h3>
          {threads.map((t) => (
            <Link key={t.id} to={`/music-studio/${t.id}`}>
              {t.title}
            </Link>
          ))}
        </nav>
      )}
    </section>
  );
}
