import { studioSchemas } from './mapStudio.ts';
import { hiveSchemas } from './hive.ts';
// Every schema exported as a JSON Schema file for non-TypeScript consumers,
// by its stable name. Names are part of the contract: C++ tests and other
// workstreams refer to fixtures/schemas/<Name>.schema.json.
import type { TSchema } from 'typebox';
import { ErrorBody } from './common.ts';
import {
  ImportAiMapPayload,
  ImportAiMapResult,
  EngineJob,
  EngineJobResult,
  GenerateMapPayload,
  GenerateMapResult,
  RenderPreviewPayload,
  RenderPreviewResult,
  ValidateMapPayload,
  ValidateMapResult,
  VerifyMatchPayload,
  VerifyVerdict,
} from './jobs.ts';
import {
  GeneratorDescriptor,
  MapSource,
  MatchRules,
  MatchSetup,
  matchSetupProblems,
  type MatchSetup as MatchSetupType,
  type SetupProblem,
} from './matchSetup.ts';
import {
  MatchAssignment,
  RealtimeEvent,
  RealtimeRequest,
  RealtimeResponse,
  RealtimeServerMessage,
  realtimeEvents,
  realtimeMethods,
  realtimeSchemaName,
} from './realtime.ts';
import {
  RelayHeartbeat,
  RelayHeartbeatResponse,
  RelayMatchEnded,
  RelayMatchEndedResponse,
  RelayRecordReceipt,
  RelayRegionList,
  RelayRegistration,
  RelayRegistrationResponse,
} from './relay.ts';
import {
  AuthTokens,
  CreateMapRequest,
  GuestSignInRequest,
  IdentityConflict,
  InstanceInfo,
  InviteInfo,
  LocalRegisterRequest,
  LocalSignInRequest,
  LeaderboardPage,
  MapDetail,
  MapHideRequest,
  MapInfo,
  MapLikeResult,
  MapList,
  MapReportInfo,
  MapReportList,
  MapReportReceipt,
  MapReportRequest,
  MapUpload,
  MapVersionInfo,
  MatchList,
  MatchSummary,
  PublicAccount,
  RefreshRequest,
  ResolveMapReportRequest,
  RoomChatMessage,
  RoomList,
  RoomMapSelection,
  RoomState,
  SelfAccount,
  SignInResponse,
  SignOutRequest,
  UpdateAccountRequest,
  UpdateMapRequest,
} from './resources.ts';
import { SimVersion } from './simVersion.ts';
import { ClientNetworkSummary, RelayNetworkSummary } from './network.ts';
import { AiLeaderboard, InstanceStats, MatchDetail, PlayerProfile } from './history.ts';
import {
  AccessTokenClaims,
  AccessTokenHeader,
  MatchTicketClaims,
  MatchTicketHeader,
  PlatformJwks,
} from './ticket.ts';
import {
  AdminAccount,
  AdminAccountList,
  AdminBanRequest,
  AdminMuteRequest,
  AdminRenameRequest,
  AdminRoleRequest,
} from './admin.ts';

export interface RegisteredSchema {
  schema: TSchema;
  /**
   * Cross-field rules beyond JSON Schema; a document must pass both. Called only
   * with values that already passed the schema.
   */
  semantic?: (value: unknown) => SetupProblem[];
}

function setupAt(pick: (value: unknown) => MatchSetupType | undefined) {
  return (value: unknown): SetupProblem[] => {
    const setup = pick(value);
    return setup
      ? matchSetupProblems(setup).map((p) => ({ path: `/setup${p.path}`, message: p.message }))
      : [];
  };
}

const matchAssignmentSemantic = setupAt((v) => (v as { setup: MatchSetupType }).setup);

const realtimeEntries: Record<string, RegisteredSchema> = {};
for (const [method, contract] of Object.entries(realtimeMethods)) {
  realtimeEntries[realtimeSchemaName(method, 'Params')] = { schema: contract.params };
  realtimeEntries[realtimeSchemaName(method, 'Result')] = { schema: contract.result };
}
for (const [event, contract] of Object.entries(realtimeEvents)) {
  realtimeEntries[realtimeSchemaName(event, 'Event')] = { schema: contract.data };
}
realtimeEntries[realtimeSchemaName('match.reconnect', 'Result')] = {
  schema: realtimeMethods['match.reconnect'].result,
  semantic: matchAssignmentSemantic,
};
realtimeEntries[realtimeSchemaName('match.start', 'Event')] = {
  schema: realtimeEvents['match.start'].data,
  semantic: matchAssignmentSemantic,
};

export const schemaRegistry: Record<string, RegisteredSchema> = {
  ...Object.fromEntries(Object.entries(studioSchemas).map(([name, schema]) => [name, { schema }])),
  ...Object.fromEntries(Object.entries(hiveSchemas).map(([name, schema]) => [name, { schema }])),
  // Simulation and match description
  SimVersion: { schema: SimVersion },
  GeneratorDescriptor: { schema: GeneratorDescriptor },
  MapSource: { schema: MapSource },
  MatchRules: { schema: MatchRules },
  MatchSetup: {
    schema: MatchSetup,
    semantic: (value) => matchSetupProblems(value as MatchSetupType),
  },
  // Tickets
  MatchTicketHeader: { schema: MatchTicketHeader },
  MatchTicketClaims: { schema: MatchTicketClaims },
  // Access tokens and keys
  AccessTokenHeader: { schema: AccessTokenHeader },
  AccessTokenClaims: { schema: AccessTokenClaims },
  PlatformJwks: { schema: PlatformJwks },
  // Relay ↔ platform
  RelayRegistration: { schema: RelayRegistration },
  RelayRegistrationResponse: { schema: RelayRegistrationResponse },
  RelayHeartbeat: { schema: RelayHeartbeat },
  RelayHeartbeatResponse: { schema: RelayHeartbeatResponse },
  RelayMatchEnded: { schema: RelayMatchEnded },
  RelayMatchEndedResponse: { schema: RelayMatchEndedResponse },
  RelayRecordReceipt: { schema: RelayRecordReceipt },
  RelayRegionList: { schema: RelayRegionList },
  // Network telemetry (docs/development/network-telemetry.md)
  RelayNetworkSummary: { schema: RelayNetworkSummary },
  ClientNetworkSummary: { schema: ClientNetworkSummary },
  // Realtime envelopes and messages
  RealtimeRequest: { schema: RealtimeRequest },
  RealtimeResponse: { schema: RealtimeResponse },
  RealtimeEvent: { schema: RealtimeEvent },
  RealtimeServerMessage: { schema: RealtimeServerMessage },
  MatchAssignment: { schema: MatchAssignment, semantic: matchAssignmentSemantic },
  ...realtimeEntries,
  // REST
  ErrorBody: { schema: ErrorBody },
  InstanceInfo: { schema: InstanceInfo },
  PublicAccount: { schema: PublicAccount },
  SelfAccount: { schema: SelfAccount },
  AuthTokens: { schema: AuthTokens },
  GuestSignInRequest: { schema: GuestSignInRequest },
  SignInResponse: { schema: SignInResponse },
  RefreshRequest: { schema: RefreshRequest },
  SignOutRequest: { schema: SignOutRequest },
  UpdateAccountRequest: { schema: UpdateAccountRequest },
  LocalRegisterRequest: { schema: LocalRegisterRequest },
  LocalSignInRequest: { schema: LocalSignInRequest },
  IdentityConflict: { schema: IdentityConflict },
  AdminAccount: { schema: AdminAccount },
  AdminAccountList: { schema: AdminAccountList },
  AdminRenameRequest: { schema: AdminRenameRequest },
  AdminMuteRequest: { schema: AdminMuteRequest },
  AdminBanRequest: { schema: AdminBanRequest },
  AdminRoleRequest: { schema: AdminRoleRequest },
  RoomMapSelection: { schema: RoomMapSelection },
  RoomState: { schema: RoomState },
  RoomList: { schema: RoomList },
  RoomChatMessage: { schema: RoomChatMessage },
  InviteInfo: { schema: InviteInfo },
  MapUpload: { schema: MapUpload },
  MatchSummary: { schema: MatchSummary },
  MatchDetail: { schema: MatchDetail },
  MatchList: { schema: MatchList },
  MapVersionInfo: { schema: MapVersionInfo },
  MapInfo: { schema: MapInfo },
  MapList: { schema: MapList },
  CreateMapRequest: { schema: CreateMapRequest },
  MapReportRequest: { schema: MapReportRequest },
  MapDetail: { schema: MapDetail },
  UpdateMapRequest: { schema: UpdateMapRequest },
  MapLikeResult: { schema: MapLikeResult },
  MapReportReceipt: { schema: MapReportReceipt },
  MapReportInfo: { schema: MapReportInfo },
  MapReportList: { schema: MapReportList },
  ResolveMapReportRequest: { schema: ResolveMapReportRequest },
  MapHideRequest: { schema: MapHideRequest },
  LeaderboardPage: { schema: LeaderboardPage },
  AiLeaderboard: { schema: AiLeaderboard },
  PlayerProfile: { schema: PlayerProfile },
  InstanceStats: { schema: InstanceStats },
  // Engine-agent jobs
  ImportAiMapPayload: { schema: ImportAiMapPayload },
  ImportAiMapResult: { schema: ImportAiMapResult },
  EngineJob: {
    schema: EngineJob,
    semantic: (value) => {
      const job = value as { kind: string; payload: { setup?: MatchSetupType } };
      return job.kind === 'verify-match'
        ? setupAt(() => job.payload.setup)(value).map((p) => ({ ...p, path: `/payload${p.path}` }))
        : [];
    },
  },
  EngineJobResult: { schema: EngineJobResult },
  GenerateMapPayload: { schema: GenerateMapPayload },
  GenerateMapResult: { schema: GenerateMapResult },
  ValidateMapPayload: { schema: ValidateMapPayload },
  ValidateMapResult: { schema: ValidateMapResult },
  RenderPreviewPayload: { schema: RenderPreviewPayload },
  RenderPreviewResult: { schema: RenderPreviewResult },
  VerifyMatchPayload: { schema: VerifyMatchPayload, semantic: matchAssignmentSemantic },
  VerifyVerdict: { schema: VerifyVerdict },
};
