import { Type, type Static } from 'typebox';
import { Open, Strict, Timestamp, Uuid } from './common.ts';
import { Page } from './resources.ts';
import { InstanceStats } from './history.ts';

export const AdminLibrary = Type.Union([
  Type.Literal('maps'),
  Type.Literal('ais'),
  Type.Literal('generators'),
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
export const AdminOperation = Open({
  id: Type.String(),
  product: Type.String(),
  accountId: Type.Union([Uuid, Type.Null()]),
  status: Type.String(),
  createdAt: Timestamp,
  reserved: Type.Integer({ minimum: 0 }),
  kind: Type.String(),
  error: Type.Union([Type.String(), Type.Null()]),
});
export type AdminOperation = Static<typeof AdminOperation>;
export const AdminOperations = Open({
  items: Type.Array(AdminOperation),
  agents: Type.Array(Open({ id: Type.String(), lastSeenAt: Timestamp })),
  queueAgeSeconds: Type.Union([Type.Number({ minimum: 0 }), Type.Null()]),
  reservedCredits: Type.Array(
    Open({ product: Type.String(), reserved: Type.Number({ minimum: 0 }) }),
  ),
  workers: Type.Array(Open({ name: Type.String(), holder: Type.String(), renewedAt: Timestamp })),
  nextCursor: Type.Optional(Type.String()),
});
export type AdminOperations = Static<typeof AdminOperations>;
export const AdminOperationDetail = Open({
  id: Type.String(),
  product: Type.String(),
  status: Type.String(),
  createdAt: Timestamp,
  completedAt: Type.Union([Timestamp, Type.Null()]),
  reserved: Type.Number(),
  charged: Type.Union([Type.Number(), Type.Null()]),
  usage: Type.Union([
    Open({
      input: Type.Number(),
      cached: Type.Number(),
      output: Type.Number(),
      cacheWrite: Type.Optional(Type.Integer({ minimum: 0 })),
    }),
    Type.Null(),
  ]),
  creditConsequence: Type.String(),
  attempts: Type.Array(
    Open({
      model: Type.String(),
      stage: Type.String(),
      status: Type.String(),
      usage: Type.Union([
        Open({
          input: Type.Number(),
          cached: Type.Number(),
          output: Type.Number(),
          cacheWrite: Type.Optional(Type.Integer({ minimum: 0 })),
        }),
        Type.Null(),
      ]),
      createdAt: Timestamp,
    }),
  ),
});
export type AdminOperationDetail = Static<typeof AdminOperationDetail>;
export const AdminUsageReconcile = Strict({
  evidence: Type.String({ minLength: 1, maxLength: 2000 }),
  usage: Strict({
    input: Type.Integer({ minimum: 0 }),
    cachedInput: Type.Integer({ minimum: 0 }),
    output: Type.Integer({ minimum: 0 }),
    cacheWrite: Type.Optional(Type.Integer({ minimum: 0 })),
  }),
});
export const AdminMetric = Open({
  day: Type.String(),
  metric: Type.String(),
  dimension: Type.String(),
  value: Type.Number(),
});
export const AdminAnalytics = Open({
  attention: Type.Record(Type.String(), Type.Integer({ minimum: 0 })),
  days: Type.Integer(),
  generatedAt: Timestamp,
  live: InstanceStats,
  metrics: Type.Array(AdminMetric),
  active: Open({
    daily: Type.Integer(),
    weekly: Type.Integer(),
    monthly: Type.Integer(),
    registered: Open({ daily: Type.Integer(), weekly: Type.Integer(), monthly: Type.Integer() }),
    guests: Open({ daily: Type.Integer(), weekly: Type.Integer(), monthly: Type.Integer() }),
  }),
  participants: Open({ current: Type.Integer(), previous: Type.Integer() }),
  coverage: Type.Array(
    Open({ metric: Type.String(), since: Type.String(), historicalIncomplete: Type.Boolean() }),
  ),
  topContent: Type.Array(AdminContent),
  collection: Type.Boolean(),
});
export type AdminAnalytics = Static<typeof AdminAnalytics>;
export const AdminFinanceCash = Open({
  product: Type.String(),
  currency: Type.String(),
  mode: Type.String(),
  period: Type.String(),
  kind: Type.String(),
  amount: Type.Number(),
  events: Type.Integer(),
});
export const AdminFinanceCredit = Open({
  product: Type.String(),
  kind: Type.String(),
  amount: Type.Number(),
});
export const AdminFinanceCost = Open({
  product: Type.String(),
  model: Type.String(),
  period: Type.String(),
  currency: Type.Union([Type.String(), Type.Null()]),
  rateVersion: Type.Union([Type.String(), Type.Null()]),
  attempts: Type.Integer(),
  metered: Type.Integer(),
  priced: Type.Integer(),
  estimatedMicros: Type.Union([Type.Number(), Type.Null()]),
});
export const AdminFinances = Open({
  days: Type.Integer(),
  mode: Type.String(),
  generatedAt: Timestamp,
  cash: Type.Array(AdminFinanceCash),
  credits: Type.Array(AdminFinanceCredit),
  costs: Type.Array(AdminFinanceCost),
  unknownPurchases: Type.Integer(),
  historicalIncomplete: Type.Boolean(),
});
export type AdminFinances = Static<typeof AdminFinances>;
export const adminConsoleSchemas = {
  AdminFinanceCash,
  AdminFinanceCredit,
  AdminFinanceCost,
  AdminFinances,
  AdminMetric,
  AdminAnalytics,
  AdminOperation,
  AdminOperations,
  AdminOperationDetail,
  AdminUsageReconcile,
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
