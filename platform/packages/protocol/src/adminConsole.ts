import { Type, type Static } from 'typebox';
import { Open, Strict, Timestamp, Uuid } from './common.ts';
import { Page } from './resources.ts';

export const AdminLibrary = Type.Union([
  Type.Literal('maps'),
  Type.Literal('ais'),
  Type.Literal('buildings'),
  Type.Literal('sets'),
  Type.Literal('skins'),
  Type.Literal('music'),
]);
export type AdminLibrary = Static<typeof AdminLibrary>;
export const AdminContent = Open({
  id: Uuid,
  library: AdminLibrary,
  name: Type.String(),
  ownerId: Type.Union([Uuid, Type.Null()]),
  hidden: Type.Boolean(),
  reason: Type.Union([Type.String(), Type.Null()]),
  createdAt: Timestamp,
  href: Type.String(),
  previewHref: Type.Union([Type.String(), Type.Null()]),
  downloads: Type.Union([Type.Integer({ minimum: 0 }), Type.Null()]),
});
export type AdminContent = Static<typeof AdminContent>;
export const AdminContentList = Page(AdminContent);
export type AdminContentList = Static<typeof AdminContentList>;
export const AdminReport = Open({
  id: Uuid,
  library: AdminLibrary,
  contentId: Uuid,
  name: Type.String(),
  href: Type.String(),
  previewHref: Type.Union([Type.String(), Type.Null()]),
  hidden: Type.Boolean(),
  reporterId: Type.Union([Uuid, Type.Null()]),
  reporterName: Type.String(),
  reason: Type.String(),
  details: Type.String(),
  createdAt: Timestamp,
  status: Type.String(),
  resolution: Type.Union([Type.String(), Type.Null()]),
  resolvedAt: Type.Union([Timestamp, Type.Null()]),
});
export type AdminReport = Static<typeof AdminReport>;
export const AdminReportList = Type.Intersect([
  Page(AdminReport),
  Open({ counts: Type.Record(Type.String(), Type.Integer({ minimum: 0 })) }),
]);
export type AdminReportList = Static<typeof AdminReportList>;
export const AdminResolveReport = Strict({
  resolution: Type.Union([Type.Literal('resolved'), Type.Literal('dismissed')]),
  reason: Type.String({ minLength: 1, maxLength: 2000 }),
  hide: Type.Optional(Type.Boolean()),
});
export const AdminModerateContent = Strict({
  hidden: Type.Boolean(),
  reason: Type.String({ minLength: 1, maxLength: 2000 }),
});
export const AdminAuditEntry = Open({
  id: Type.String(),
  actorId: Type.Union([Uuid, Type.Null()]),
  actorName: Type.String(),
  action: Type.String(),
  targetType: Type.String(),
  targetId: Type.String(),
  details: Type.Record(Type.String(), Type.Unknown()),
  createdAt: Timestamp,
});
export type AdminAuditEntry = Static<typeof AdminAuditEntry>;
export const AdminAuditList = Page(AdminAuditEntry);
export type AdminAuditList = Static<typeof AdminAuditList>;
export const adminConsoleSchemas = {
  AdminLibrary,
  AdminContent,
  AdminContentList,
  AdminReport,
  AdminReportList,
  AdminResolveReport,
  AdminModerateContent,
  AdminAuditEntry,
  AdminAuditList,
};
