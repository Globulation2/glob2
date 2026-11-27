// Problem and crash reports (docs/multiplayer/problem-reports.md). The game
// uploads one zip bundle to POST /api/v1/crash-reports; its context.json entry
// is a CrashReportContext. The same bundle is what "Save to file" writes, so
// a report saved offline can be read with the same tools.
//
// Crash reports (docs/multiplayer/crash-reports.md) use the same bundle with
// one more entry holding the crash itself (CRASH_PAYLOAD_ENTRIES). The crash
// worker symbolicates it against the release's uploaded symbols and files it
// under a crash group.
import { Type, type Static } from 'typebox';
import { Strict, Timestamp, Uuid } from './common.ts';
import { Page } from './resources.ts';

/** Short report ID a player can quote: unambiguous letters and digits. */
export const CrashReportId = Type.String({ pattern: '^[A-HJ-NP-Z2-9]{8}$' });

export const CrashReportKind = Type.Union(
  [
    Type.Literal('problem'),
    Type.Literal('crash'),
    Type.Literal('metrickit'),
    Type.Literal('web'),
    Type.Literal('java'),
  ],
  {
    description:
      'What sent the report: Report a Problem, a Crashpad minidump, an iOS MetricKit crash diagnostic, a browser error, or an uncaught Java exception on Android.',
  },
);
export type CrashReportKind = Static<typeof CrashReportKind>;

/**
 * The bundle entry that holds the crash, by kind. A crash bundle without it is
 * refused; Report a Problem bundles have none.
 */
export const CRASH_PAYLOAD_ENTRIES = {
  crash: 'minidump.dmp',
  metrickit: 'metrickit.json',
  web: 'web-error.json',
  java: 'java-exception.txt',
} as const satisfies Record<Exclude<CrashReportKind, 'problem'>, string>;

/** Build distribution channels (buildsys/client_features.py DISTRIBUTIONS). */
export const CrashReportDistribution = Type.Union([
  Type.Literal('direct'),
  Type.Literal('browser'),
  Type.Literal('steam'),
  Type.Literal('epic'),
  Type.Literal('google_play'),
  Type.Literal('app_store'),
  Type.Literal('microsoft'),
  Type.Literal('amazon'),
  Type.Literal('china'),
]);

export const CrashReportPlatform = Type.Union([
  Type.Literal('linux'),
  Type.Literal('windows'),
  Type.Literal('macos'),
  Type.Literal('ios'),
  Type.Literal('android'),
  Type.Literal('web'),
  Type.Literal('other'),
]);

/**
 * The context.json entry of a bundle. Minimum data only: no account, name,
 * chat or address. installId is random per install and counts affected
 * installs; it is not linked to an account.
 */
export const CrashReportContext = Strict({
  schema: Type.Literal(1),
  kind: CrashReportKind,
  installId: Uuid,
  /** Random per report: a retried upload of the same report is not stored twice. */
  submissionId: Uuid,
  /** PACKAGE_VERSION of the build, for example "0.9.6". */
  version: Type.String({ minLength: 1, maxLength: 64, pattern: '^[0-9A-Za-z.+_~-]+$' }),
  versionMinor: Type.Integer({ minimum: 0, maximum: 65535 }),
  simRevision: Type.Integer({ minimum: 0, maximum: 4294967295 }),
  distribution: CrashReportDistribution,
  platform: CrashReportPlatform,
  /** Operating system name and version as SDL reports it. */
  os: Type.String({ maxLength: 128 }),
  /** Interface layout in use: the device-adaptation class. */
  deviceClass: Type.Union([Type.Literal('desktop'), Type.Literal('tablet'), Type.Literal('phone')]),
  /** DeviceProfile tier (src/app/DeviceProfile.h). */
  deviceTier: Type.Union([Type.Literal('low'), Type.Literal('mid'), Type.Literal('high')]),
  /** The player's "What happened?" text. */
  description: Type.String({ maxLength: 2000 }),
  /** The match that was running, if any: its tick and map seed. */
  match: Type.Optional(
    Strict({
      tick: Type.Integer({ minimum: 0, maximum: 4294967295 }),
      seed: Type.Integer({ minimum: 0, maximum: 4294967295 }),
    }),
  ),
  createdAt: Timestamp,
});
export type CrashReportContext = Static<typeof CrashReportContext>;

/**
 * Identifies one WebAssembly build: the first 32 hex digits of the SHA-256 of
 * its .wasm file. The browser client reports it with every error, and the
 * release workflow uploads that build's symbol map under it.
 */
export const WasmBuildId = Type.String({ pattern: '^[0-9a-f]{32}$' });

/** The web-error.json entry of a browser crash bundle. */
export const WebErrorReport = Strict({
  schema: Type.Literal(1),
  /** What caught it: window.onerror, an unhandled rejection, or Emscripten's abort. */
  source: Type.Union([
    Type.Literal('error'),
    Type.Literal('unhandledrejection'),
    Type.Literal('abort'),
  ]),
  message: Type.String({ maxLength: 1024 }),
  /** The JavaScript stack as the browser wrote it, wasm-function[N] frames included. */
  stack: Type.String({ maxLength: 32768 }),
  buildId: WasmBuildId,
});
export type WebErrorReport = Static<typeof WebErrorReport>;

/** Answer to an accepted upload. */
export const CrashReportReceipt = Strict({ id: CrashReportId });
export type CrashReportReceipt = Static<typeof CrashReportReceipt>;

export const AdminCrashReportAttachment = Strict({
  name: Type.String(),
  size: Type.Integer({ minimum: 0 }),
});
export type AdminCrashReportAttachment = Static<typeof AdminCrashReportAttachment>;

/**
 * Processing state of a report: Report a Problem bundles are never processed
 * ('none'); crash bundles wait for the crash worker ('pending'), then hold a
 * symbolicated stack ('done') or the reason none could be made ('failed').
 */
export const CrashReportStatus = Type.Union([
  Type.Literal('none'),
  Type.Literal('pending'),
  Type.Literal('done'),
  Type.Literal('failed'),
]);
export type CrashReportStatus = Static<typeof CrashReportStatus>;

export const CrashGroupId = Uuid;

/** A released version: three or four dot-separated numbers, as tags name them (v0.11.8.1). */
export const ReleaseVersion = Type.String({ pattern: '^[0-9]+(\\.[0-9]+){2,3}$' });

/**
 * Triage state of a crash group (docs/multiplayer/crash-reports.md#triage):
 * every group starts 'new'; 'resolved' names the release that fixed it, and a
 * resolved group that recurs in that release or a later one is 'new' again.
 */
export const CrashGroupState = Type.Union([
  Type.Literal('new'),
  Type.Literal('triaged'),
  Type.Literal('resolved'),
]);
export type CrashGroupState = Static<typeof CrashGroupState>;

/** One frame of a symbolicated stack, innermost first. */
export const CrashFrame = Strict({
  /** File name of the module (glob2, glob2.exe, libmain.so, glob2.wasm, ...). */
  module: Type.String({ maxLength: 256 }),
  /** Offset into the module, as 0x-prefixed hex. */
  offset: Type.String({ maxLength: 32 }),
  function: Type.Optional(Type.String({ maxLength: 1024 })),
  file: Type.Optional(Type.String({ maxLength: 1024 })),
  line: Type.Optional(Type.Integer({ minimum: 0 })),
  /** Whether the frame is Glob2's own code (src/ or libs/), which crash signatures use. */
  inGame: Type.Boolean(),
});
export type CrashFrame = Static<typeof CrashFrame>;

export const AdminCrashReport = Strict({
  id: CrashReportId,
  kind: CrashReportKind,
  status: CrashReportStatus,
  /** The crash group, once the worker filed the report. */
  groupId: Type.Optional(CrashGroupId),
  /** What the crash was (for example SIGSEGV or EXCEPTION_ACCESS_VIOLATION_READ), or why processing failed. */
  reason: Type.Optional(Type.String()),
  receivedAt: Timestamp,
  version: Type.String(),
  versionMinor: Type.Integer(),
  simRevision: Type.Integer(),
  distribution: Type.String(),
  platform: Type.String(),
  os: Type.String(),
  deviceClass: Type.String(),
  deviceTier: Type.String(),
  description: Type.String(),
  installId: Uuid,
  size: Type.Integer({ minimum: 0 }),
  attachments: Type.Array(AdminCrashReportAttachment),
  /** False once the bundle passed its retention (the summary stays). */
  bundleAvailable: Type.Boolean(),
});
export type AdminCrashReport = Static<typeof AdminCrashReport>;

export const AdminCrashReportList = Page(AdminCrashReport);
export type AdminCrashReportList = Static<typeof AdminCrashReportList>;

/** One report with its symbolicated crashing-thread stack. */
export const AdminCrashReportDetail = Strict({
  report: AdminCrashReport,
  frames: Type.Array(CrashFrame),
});
export type AdminCrashReportDetail = Static<typeof AdminCrashReportDetail>;

/** Reports whose top in-game frames match, across releases and platforms. */
export const AdminCrashGroup = Strict({
  id: CrashGroupId,
  /** The frames the group is keyed by, innermost first, joined by " | ". */
  signature: Type.String(),
  /** Earliest and latest release the group appeared in. */
  firstVersion: Type.String(),
  lastVersion: Type.String(),
  firstSeenAt: Timestamp,
  lastSeenAt: Timestamp,
  /** Matching reports and distinct installs, within the list's filters. */
  reports: Type.Integer({ minimum: 0 }),
  installs: Type.Integer({ minimum: 0 }),
  platforms: Type.Array(Type.String()),
  distributions: Type.Array(Type.String()),
  state: CrashGroupState,
  /** The release a resolved group was fixed in; kept when the group reopens. */
  fixedIn: Type.Optional(Type.String()),
  /** The GitHub issue opened for the group, if any. */
  githubIssue: Type.Optional(
    Strict({ number: Type.Integer({ minimum: 1 }), url: Type.String({ maxLength: 512 }) }),
  ),
});
export type AdminCrashGroup = Static<typeof AdminCrashGroup>;

export const AdminCrashGroupList = Page(AdminCrashGroup);
export type AdminCrashGroupList = Static<typeof AdminCrashGroupList>;

/**
 * Symbol files (docs/multiplayer/crash-reports.md#symbols). The release
 * workflows upload one per shipped binary: a Breakpad .sym written by
 * dump_syms, keyed by its MODULE line, or an Emscripten symbol map keyed by
 * the wasm build ID.
 */
export const CrashSymbolKind = Type.Union([Type.Literal('breakpad'), Type.Literal('wasm')]);
export type CrashSymbolKind = Static<typeof CrashSymbolKind>;

export const CrashSymbolReceipt = Strict({
  kind: CrashSymbolKind,
  /** The debug file name minidumps refer to (glob2, glob2.pdb, libmain.so, glob2.wasm). */
  module: Type.String(),
  /** Breakpad debug identifier, or the wasm build ID. */
  debugId: Type.String(),
  version: ReleaseVersion,
  /** True when a file with this key was already stored (it is replaced). */
  replaced: Type.Boolean(),
});
export type CrashSymbolReceipt = Static<typeof CrashSymbolReceipt>;

/** Body of POST /api/v1/admin/crash-groups/:id/state. */
export const CrashGroupStateRequest = Strict({
  state: CrashGroupState,
  /** Required for 'resolved': the first release that has the fix. */
  fixedIn: Type.Optional(ReleaseVersion),
});
export type CrashGroupStateRequest = Static<typeof CrashGroupStateRequest>;

export const CrashGroupStateReceipt = Strict({
  id: CrashGroupId,
  state: CrashGroupState,
  fixedIn: Type.Optional(ReleaseVersion),
});
export type CrashGroupStateReceipt = Static<typeof CrashGroupStateReceipt>;

/** A UTC calendar day, YYYY-MM-DD. */
export const UtcDay = Type.String({ pattern: '^[0-9]{4}-[0-9]{2}-[0-9]{2}$' });

/**
 * Body of POST /api/v1/crash-sessions (docs/multiplayer/crash-reports.md#session-counts):
 * how many sessions one install started on one UTC day, sent only by installs
 * whose player chose "Always send". The count is the day's running total, so
 * a resent or retried ping changes nothing; the server keeps the largest.
 */
export const CrashSessionPing = Strict({
  schema: Type.Literal(1),
  installId: Uuid,
  version: ReleaseVersion,
  versionMinor: Type.Integer({ minimum: 0, maximum: 65535 }),
  distribution: CrashReportDistribution,
  platform: CrashReportPlatform,
  /** The UTC day the sessions started on: today or one of the six days before. */
  day: UtcDay,
  /** Sessions started that day so far, the crashed ones included. */
  sessions: Type.Integer({ minimum: 1, maximum: 500 }),
});
export type CrashSessionPing = Static<typeof CrashSessionPing>;

/** Crash-free rates over one slice of a release's opted-in sessions. */
export const CrashFreeStats = Strict({
  /** Sessions and distinct installs that "Always send" installs reported. */
  sessions: Type.Integer({ minimum: 0 }),
  installs: Type.Integer({ minimum: 0 }),
  /** Crash reports from those installs, and how many of them crashed at all. */
  crashedSessions: Type.Integer({ minimum: 0 }),
  crashedInstalls: Type.Integer({ minimum: 0 }),
  /** Share of sessions and installs without a crash, 0 to 1; absent without sessions. */
  crashFreeSessions: Type.Optional(Type.Number({ minimum: 0, maximum: 1 })),
  crashFreeInstalls: Type.Optional(Type.Number({ minimum: 0, maximum: 1 })),
});
export type CrashFreeStats = Static<typeof CrashFreeStats>;

/** A group first seen in a release, with its reports in that release. */
export const AdminCrashReleaseGroup = Strict({
  id: CrashGroupId,
  signature: Type.String(),
  state: CrashGroupState,
  reports: Type.Integer({ minimum: 0 }),
  installs: Type.Integer({ minimum: 0 }),
});
export type AdminCrashReleaseGroup = Static<typeof AdminCrashReleaseGroup>;

/** One release in GET /api/v1/admin/crash-releases. */
export const AdminCrashRelease = Strict({
  version: ReleaseVersion,
  /** When its first symbols were uploaded. */
  registeredAt: Timestamp,
  /** Crash-free rates over every opted-in session. */
  overall: CrashFreeStats,
  /** The same per platform and channel, most sessions first. */
  breakdown: Type.Array(
    Strict({ platform: Type.String(), distribution: Type.String(), stats: CrashFreeStats }),
  ),
  /** Every crash report of the release (any consent) and distinct installs sending them. */
  reports: Type.Integer({ minimum: 0 }),
  affectedInstalls: Type.Integer({ minimum: 0 }),
  /** Groups whose first release this is, most affected installs first (at most ten). */
  newGroups: Type.Array(AdminCrashReleaseGroup),
});
export type AdminCrashRelease = Static<typeof AdminCrashRelease>;

export const AdminCrashReleaseList = Page(AdminCrashRelease);
export type AdminCrashReleaseList = Static<typeof AdminCrashReleaseList>;

/**
 * Answer of GET /api/v1/crash-releases/:version/gate
 * (docs/multiplayer/crash-reports.md#promotion-gate): whether a release may be
 * promoted to a wider audience.
 */
export const CrashReleaseGate = Strict({
  version: ReleaseVersion,
  verdict: Type.Union([
    Type.Literal('pass'),
    Type.Literal('fail'),
    Type.Literal('insufficient_data'),
  ]),
  /** One line per check that failed or could not be decided, or why it passed. */
  reasons: Type.Array(Type.String()),
  /** The release compared against: the newest registered one before this. */
  previousVersion: Type.Optional(ReleaseVersion),
  sessions: Type.Integer({ minimum: 0 }),
  crashFreeSessions: Type.Optional(Type.Number({ minimum: 0, maximum: 1 })),
  previousSessions: Type.Optional(Type.Integer({ minimum: 0 })),
  previousCrashFreeSessions: Type.Optional(Type.Number({ minimum: 0, maximum: 1 })),
  /** How far below the previous release crash-free sessions may fall (a fraction). */
  tolerance: Type.Number({ minimum: 0, maximum: 1 }),
  /** Fewest sessions either release needs before the comparison counts. */
  minimumSessions: Type.Integer({ minimum: 0 }),
  /** New groups among the release's five most affecting installs that are still 'new'. */
  untriagedGroups: Type.Array(AdminCrashReleaseGroup),
});
export type CrashReleaseGate = Static<typeof CrashReleaseGate>;

export const crashReportSchemas = {
  CrashReportContext,
  CrashReportReceipt,
  WebErrorReport,
  AdminCrashReport,
  AdminCrashReportList,
  AdminCrashReportDetail,
  AdminCrashGroup,
  AdminCrashGroupList,
  CrashSymbolReceipt,
  CrashGroupStateRequest,
  CrashGroupStateReceipt,
  CrashSessionPing,
  CrashFreeStats,
  AdminCrashRelease,
  AdminCrashReleaseList,
  CrashReleaseGate,
};
