// Placeholder home page: shows which instance this is and what it serves.
// Sign-in, invite landing pages, profiles, leaderboards and maps arrive with
// their milestones (M3-M7).
import { useEffect, useState } from 'react';
import { simVersionKey, type InstanceInfo } from '@glob2/protocol';
import { fetchInstance } from './api.ts';

type State =
  | { status: 'loading' }
  | { status: 'ready'; instance: InstanceInfo }
  | { status: 'error'; message: string };

export function App() {
  const [state, setState] = useState<State>({ status: 'loading' });

  useEffect(() => {
    const controller = new AbortController();
    fetchInstance(controller.signal).then(
      (instance) => setState({ status: 'ready', instance }),
      (error: unknown) => {
        if (!controller.signal.aborted) {
          setState({
            status: 'error',
            message: error instanceof Error ? error.message : String(error),
          });
        }
      },
    );
    return () => controller.abort();
  }, []);

  if (state.status === 'loading') return <main>Loading…</main>;
  if (state.status === 'error') {
    return (
      <main>
        <h1>Globulation 2</h1>
        <p role="alert">The platform is unavailable: {state.message}</p>
      </main>
    );
  }
  const { instance } = state;
  return (
    <main>
      <h1>{instance.name}</h1>
      <p>{instance.guestsAllowed ? 'Guests may play.' : 'Sign-in required.'}</p>
      <h2>Game versions served</h2>
      {instance.supportedSimVersions.length === 0 ? (
        <p>No engine agents are online.</p>
      ) : (
        <ul>
          {instance.supportedSimVersions.map((version) => (
            <li key={simVersionKey(version)}>
              Format {version.versionMinor}, network protocol {version.netProtocol}
            </li>
          ))}
        </ul>
      )}
      <h2>Quick match queues</h2>
      {instance.queues.length === 0 ? (
        <p>None configured.</p>
      ) : (
        <ul>
          {instance.queues.map((queue) => (
            <li key={queue.id}>
              {queue.name} ({queue.mode}
              {queue.rated ? ', rated' : ''})
            </li>
          ))}
        </ul>
      )}
    </main>
  );
}
