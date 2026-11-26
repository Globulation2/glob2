// Problem and crash reports (docs/multiplayer/problem-reports.md). The game
// uploads one zip bundle to POST /api/v1/crash-reports; its context.json entry
// is a CrashReportContext. The same bundle is what "Save to file" writes, so
// a report saved offline can be read with the same tools.
import { Type, type Static } from 'typebox';
import { Strict, Timestamp, Uuid } from './common.ts';
import { Page } from './resources.ts';

/** Short report ID a player can quote: unambiguous letters and digits. */
export const CrashReportId = Type.String({ pattern: '^[A-HJ-NP-Z2-9]{8}$' });

export const CrashReportKind = Type.Union([Type.Literal('problem')], {
  description: 'What sent the report. Phase 1 knows only Report a Problem.',
});

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
  createdAt: Timestamp,
});
export type CrashReportContext = Static<typeof CrashReportContext>;

/** Answer to an accepted upload. */
export const CrashReportReceipt = Strict({ id: CrashReportId });
export type CrashReportReceipt = Static<typeof CrashReportReceipt>;

export const AdminCrashReportAttachment = Strict({
  name: Type.String(),
  size: Type.Integer({ minimum: 0 }),
});
export type AdminCrashReportAttachment = Static<typeof AdminCrashReportAttachment>;

export const AdminCrashReport = Strict({
  id: CrashReportId,
  kind: CrashReportKind,
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

export const crashReportSchemas = {
  CrashReportContext,
  CrashReportReceipt,
  AdminCrashReport,
  AdminCrashReportList,
};
