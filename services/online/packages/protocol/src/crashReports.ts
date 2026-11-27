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

/** A released version: three or four dot-separated numbers, as tags name them (v0.11.8.1). */
export const ReleaseVersion = Type.String({ pattern: '^[0-9]+(\\.[0-9]+){2,3}$' });

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
};
