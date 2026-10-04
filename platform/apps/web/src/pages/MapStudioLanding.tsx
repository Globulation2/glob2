import { useState } from 'react';
import { Link } from '../router.tsx';
import islands from '../art/studio-islands.webp';
import hills from '../art/studio-hills.webp';
import river from '../art/studio-river.webp';
import demoReference from '../art/studio-demo-reference.png';
import demoLayout from '../art/studio-demo-layout.png';
import demoCrop from '../art/studio-demo-crop.png';
import demoReady from '../art/studio-demo-ready.png';
import { price, type Wallet } from './studio/types.ts';
const examples = [
  {
    name: 'Island dreams',
    image: islands,
    description: 'Sheltered colonies, scattered islets, and open water.',
  },
  {
    name: 'Room to grow',
    image: hills,
    description: 'Rolling terrain, generous homes, and routes to explore.',
  },
  {
    name: 'Follow the river',
    image: river,
    description: 'A winding waterway changes how you discover the world.',
  },
];
const demoImages = [demoReference, demoLayout, demoCrop, demoReady];
const steps = [
  {
    title: 'Describe your world',
    text: 'Start with a landscape, a mood, or the way you want your next match to feel.',
  },
  {
    title: 'Refine the details',
    text: 'Talk through the terrain with your map designer. Choose your dimensions and player count.',
  },
  {
    title: 'Watch it take shape',
    text: 'Explore each completed stage, from generated layout to playable terrain.',
  },
  {
    title: 'Check, then play',
    text: 'Follow the playability checks. Download your map, host a match, or refine it again.',
  },
];
export function MapStudioLanding({
  wallet,
  busy,
  buy,
  threads,
  draft,
  setDraft,
}: {
  draft: string;
  setDraft: (value: string) => void;
  wallet: Wallet;
  busy: boolean;
  buy: (id: string) => void;
  threads: { id: string; title: string }[];
}) {
  const [example, setExample] = useState(0);
  const [step, setStep] = useState(0);
  const selected = examples[example] ?? examples[0];
  if (!selected) return null;
  return (
    <div className="ms-landing">
      <section className="ms-landing-hero">
        <div className="ms-landing-copy">
          <span className="ms-eyebrow">FROM YOUR IMAGINATION. INTO THE GAME.</span>
          <h2>
            Imagine your next
            <br />
            <em>battlefield.</em>
          </h2>
          <p>
            Quiet islands. Wild rivers. A world made for your next great match. Describe your idea,
            shape it together, and watch it come to life.
          </p>
          <div className="ms-landing-prompt">
            <label htmlFor="studio-first-idea">Your first idea</label>
            <textarea
              id="studio-first-idea"
              rows={2}
              maxLength={8000}
              value={draft}
              onChange={(event) => setDraft(event.target.value)}
              placeholder="An island world with a hidden lagoon…"
            />
            <span>Your idea is saved here until you’re ready to create.</span>
          </div>
          <a className="btn primary" href="#studio-packs">
            Start creating <span aria-hidden="true">↗</span>
          </a>
          <span className="ms-landing-small">
            Your maps. Your ideas. A whole new place to play.
          </span>
        </div>
        <div className="ms-hero-world">
          <div className="ms-world-halo" />
          <img
            key={selected.image}
            src={selected.image}
            alt={`${selected.name}, an example of Globulation 2 terrain`}
          />
          <div className="ms-world-label">
            <span aria-hidden="true">✧</span>
            <div>
              <strong>{selected.name}</strong>
              <span>Game terrain inspiration · illustrative example</span>
            </div>
          </div>
        </div>
      </section>
      <section className="ms-inspiration">
        <div className="ms-section-heading">
          <span className="ms-eyebrow">WHAT KIND OF WORLD WILL YOU MAKE?</span>
          <h2>A little inspiration. Endless possibilities.</h2>
          <p>
            Explore real Globulation terrain. These examples illustrate map styles, not promised AI
            output.
          </p>
        </div>
        <div className="ms-example-grid">
          {examples.map((e, i) => (
            <button key={e.name} aria-pressed={example === i} onClick={() => setExample(i)}>
              <div>
                <img src={e.image} alt={e.name} loading="lazy" />
              </div>
              <span>
                <strong>{e.name}</strong>
                <span>{e.description}</span>
              </span>
              <span className="ms-example-arrow" aria-hidden="true">
                ↗
              </span>
            </button>
          ))}
        </div>
      </section>
      <section className="ms-demo">
        <div>
          <span className="ms-eyebrow">THE JOURNEY IS PART OF THE MAGIC</span>
          <h2>
            A front-row seat
            <br />
            to creation.
          </h2>
          <p>
            No disappearing progress bar. See the images, explore the stages, and follow the checks
            as your map takes shape.
          </p>
          <span className="ms-landing-small">Recorded workflow example</span>
        </div>
        <div className="ms-demo-flow">
          <div className="ms-demo-tabs" role="group" aria-label="Explore the creation workflow">
            {steps.map((s, i) => (
              <button key={s.title} aria-pressed={step === i} onClick={() => setStep(i)}>
                <span>{i < step ? '✓' : `0${i + 1}`}</span>
                {s.title}
              </button>
            ))}
          </div>
          <div className="ms-demo-detail" key={step}>
            <img
              className="ms-demo-image"
              src={demoImages[step]}
              alt={`Recorded pipeline example: ${steps[step]?.title}`}
            />
            <span className="ms-eyebrow">STEP {step + 1} OF 4</span>
            <h3>{steps[step]?.title}</h3>
            <p>{steps[step]?.text}</p>
            <div className="ms-demo-progress">
              <span style={{ width: `${(step + 1) * 25}%` }} />
            </div>
          </div>
        </div>
      </section>
      <section className="ms-revision-story">
        <div>
          <span className="ms-eyebrow">KEEP THE CONVERSATION GOING</span>
          <h2>
            “What if we added
            <br />a little more adventure?”
          </h2>
          <p>
            Choose a delivered version and describe your changes. Each revision becomes a new
            version, so you can compare the results and keep your favourites.
          </p>
        </div>
        <div className="ms-revision-card">
          <div className="ms-landing-compare">
            <figure>
              <img src={islands} alt="Original island concept from a curated game map" />
              <figcaption>Original concept</figcaption>
            </figure>
            <figure>
              <img src={hills} alt="Revised spacious concept from a curated game map" />
              <figcaption>Revised concept</figcaption>
            </figure>
          </div>
          <div className="ms-message ms-message-user">
            <p>Keep the island feel, but give the colonies more room to grow.</p>
          </div>
          <div className="ms-message ms-message-assistant">
            <p>Refine your idea together, then generate a revision when you’re ready.</p>
          </div>
          <span className="ms-landing-small">
            Illustrative revision comparison using curated game maps; conversation is illustrative.
            Each delivered revision costs 1 credit.
          </span>
        </div>
      </section>
      <section id="studio-packs" className="ms-purchase">
        <span className="ms-eyebrow">YOUR NEXT WORLD IS WAITING</span>
        <h2>Make room for your imagination.</h2>
        <p>
          One credit for each delivered map or revision.
          <br />
          Discussion is included while you have an available credit. Failed generations return it.
        </p>
        <div className="ms-packs">
          {wallet.packs.map((p) => (
            <div key={p.id}>
              <span className="ms-eyebrow">MAP STUDIO CREDITS</span>
              <h3>{p.credits} new possibilities</h3>
              <strong>{price(p)}</strong>
              <button
                className="primary"
                disabled={busy || !wallet.enabled}
                onClick={() => buy(p.id)}
              >
                Buy {p.credits} credits <span aria-hidden="true">↗</span>
              </button>
            </div>
          ))}
        </div>
        {!wallet.enabled ? (
          <p>AI Map Studio is not enabled on this instance.</p>
        ) : !wallet.packs.length ? (
          <p>Credit purchases are currently unavailable. Check back soon.</p>
        ) : null}
        {!!threads.length && (
          <p>
            Your creations are still here.{' '}
            <Link to={`/map-studio/${threads[0]?.id}`}>Open your saved projects →</Link>
          </p>
        )}
      </section>
    </div>
  );
}
