// @glob2/play: the match domain shared by the API and the worker. Rating maths
// and application, queue tickets and proposals, the match start sequence,
// relay placement, map sources, match-end intake, the map catalog's job
// results and the warm map pool. The worker's matchmaker loop and the API's
// handlers both build on it; neither app imports the other.
//
// Test doubles (InMemoryMatchStarter, RecordingQueueNotifier) and fixtures
// live in `@glob2/play/testing`, not here.
export * from './clock.ts';
export * from './stored.ts';
export * from './ratings/scale.ts';
export * from './ratings/outcome.ts';
export * from './ratings/entities.ts';
export * from './ratings/apply.ts';
export * from './ratings/preview.ts';
export * from './matchmaking/grouping.ts';
export * from './matchmaking/notifier.ts';
export * from './matchmaking/starter.ts';
export * from './matchmaking/tickets.ts';
export * from './matchmaking/proposalView.ts';
export * from './play/notify.ts';
export * from './play/relays.ts';
export * from './play/maps.ts';
export * from './play/start.ts';
export * from './play/intake.ts';
export * from './play/catalog.ts';
export * from './warmMaps.ts';
export * from './accountScrub.ts';
export * from './play/jobSweep.ts';
