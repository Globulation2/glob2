// Every schema exported as a JSON Schema file for non-TypeScript consumers,
// by its stable name. Names are part of the contract: C++ tests and other
// workstreams refer to fixtures/schemas/<Name>.schema.json.
import type { TSchema } from 'typebox';
import { ErrorBody } from './common.ts';
import {
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
  RelayRegistration,
  RelayRegistrationResponse,
} from './relay.ts';
import {
  AuthTokens,
  CreateMapRequest,
  GuestSignInRequest,
  InstanceInfo,
  LeaderboardPage,
  MapInfo,
  MapList,
  MapReportRequest,
  MapVersionInfo,
  MatchDetail,
  MatchList,
  MatchSummary,
  PublicAccount,
  RefreshRequest,
  RoomChatMessage,
  RoomList,
  RoomMapSelection,
  RoomState,
  SelfAccount,
  SignInResponse,
  SignOutRequest,
  UpdateAccountRequest,
} from './resources.ts';
import { SimVersion } from './simVersion.ts';
import { MatchTicketClaims, MatchTicketHeader } from './ticket.ts';

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
  // Relay ↔ platform
  RelayRegistration: { schema: RelayRegistration },
  RelayRegistrationResponse: { schema: RelayRegistrationResponse },
  RelayHeartbeat: { schema: RelayHeartbeat },
  RelayHeartbeatResponse: { schema: RelayHeartbeatResponse },
  RelayMatchEnded: { schema: RelayMatchEnded },
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
  RoomMapSelection: { schema: RoomMapSelection },
  RoomState: { schema: RoomState },
  RoomList: { schema: RoomList },
  RoomChatMessage: { schema: RoomChatMessage },
  MatchSummary: { schema: MatchSummary },
  MatchDetail: { schema: MatchDetail },
  MatchList: { schema: MatchList },
  MapVersionInfo: { schema: MapVersionInfo },
  MapInfo: { schema: MapInfo },
  MapList: { schema: MapList },
  CreateMapRequest: { schema: CreateMapRequest },
  MapReportRequest: { schema: MapReportRequest },
  LeaderboardPage: { schema: LeaderboardPage },
  // Engine-agent jobs
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
