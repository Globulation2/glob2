// GitHub issues for new crash groups (docs/multiplayer/crash-reports.md#github-issues).
//
// When the crash worker creates a group it queues one CRASH_GITHUB_ISSUE_TASK
// job (job key crash-issue-<group>), which opens an issue on
// CRASH_GITHUB_REPOSITORY and stores its number on the group. The issue holds
// only what the code tells: the signature, the top frames, the first release
// and report counts per platform and channel. Never install IDs, logs,
// descriptions or anything else a player sent.
import { sql, type Kysely } from 'kysely';
import type { Logger } from '@glob2/core';
import type { CrashFrameRow, Database } from '@glob2/db';

export const CRASH_GITHUB_REPOSITORY_ENV = 'CRASH_GITHUB_REPOSITORY';
export const CRASH_GITHUB_TOKEN_ENV = 'CRASH_GITHUB_TOKEN';
const REPOSITORY = /^[A-Za-z0-9_.-]{1,100}\/[A-Za-z0-9_.-]{1,100}$/;
const FRAMES_SHOWN = 10;

export interface GitHubIssueConfig {
  /** owner/name, for example Globulation2/glob2. */
  repository: string;
  token: string;
  /** Public origin of the platform, for the admin link. */
  publicOrigin: string;
  apiUrl?: string;
  fetch?: typeof fetch;
}

/** The configuration from the environment, or undefined when issues are off. */
export function githubIssueConfig(
  env: Record<string, string | undefined>,
  publicOrigin: string,
): GitHubIssueConfig | undefined {
  const repository = env[CRASH_GITHUB_REPOSITORY_ENV]?.trim();
  const token = env[CRASH_GITHUB_TOKEN_ENV]?.trim();
  if (!repository || !token) return undefined;
  if (!REPOSITORY.test(repository))
    throw new Error(`${CRASH_GITHUB_REPOSITORY_ENV} must look like owner/name`);
  return { repository, token, publicOrigin };
}

export interface IssueContent {
  title: string;
  body: string;
}

/** Repository-relative source path of a frame (src/unit/Unit.cpp), never a build machine path. */
function sourcePath(file: string): string {
  const parts = file.split(/[/\\]/);
  const root = parts.findIndex((part) => part === 'src' || part === 'libs');
  return (root >= 0 ? parts.slice(root) : parts.slice(-1)).join('/');
}

function fence(text: string): string {
  const longest = Math.max(2, ...[...text.matchAll(/`+/g)].map((m) => m[0].length));
  const ticks = '`'.repeat(longest + 1);
  return `${ticks}\n${text}\n${ticks}`;
}

export function issueContent(input: {
  groupId: string;
  signature: string;
  firstVersion: string;
  frames: CrashFrameRow[];
  counts: { platform: string; distribution: string; reports: number; installs: number }[];
  publicOrigin: string;
}): IssueContent {
  const frames = input.frames.slice(0, FRAMES_SHOWN).map((frame, index) => {
    const name = frame.function ?? `${frame.module}+${frame.offset}`;
    const where = frame.file
      ? ` ${sourcePath(frame.file)}${frame.line !== undefined ? `:${frame.line}` : ''}`
      : '';
    return `${index} ${frame.inGame ? '*' : ' '} ${name}${where}`;
  });
  const signature = input.signature.replace(/\s+/g, ' ');
  const title = `Crash: ${signature}`;
  const body = [
    'The crash worker filed a new crash group.',
    '',
    `**First release:** ${input.firstVersion}`,
    '',
    '**Signature**',
    '',
    fence(input.signature),
    '',
    `**Top frames** (innermost first, \`*\` marks game code)`,
    '',
    fence(frames.length ? frames.join('\n') : '(no frames)'),
    '',
    '**Reports so far**',
    '',
    '| Platform | Channel | Reports | Installs |',
    '| --- | --- | ---: | ---: |',
    ...input.counts.map(
      (c) => `| ${c.platform} | ${c.distribution} | ${c.reports} | ${c.installs} |`,
    ),
    '',
    `[Crash group in the admin console](${input.publicOrigin}/admin/crashes?group=${input.groupId})`,
  ].join('\n');
  return { title: title.length > 240 ? `${title.slice(0, 239)}…` : title, body };
}

/**
 * Opens the issue for group `groupId` unless it has one. Returns the issue
 * number, or undefined when the group is gone. A failed request throws, for
 * graphile-worker to retry.
 */
export async function openCrashIssue(
  db: Kysely<Database>,
  config: GitHubIssueConfig,
  groupId: string,
  logger: Pick<Logger, 'info'>,
): Promise<number | undefined> {
  const group = await db
    .selectFrom('crash_groups')
    .select(['id', 'signature', 'first_version', 'github_issue'])
    .where('id', '=', groupId)
    .executeTakeFirst();
  if (!group) return undefined;
  if (group.github_issue) return group.github_issue;
  const latest = await db
    .selectFrom('crash_reports')
    .select('frames')
    .where('group_id', '=', groupId)
    .where('frames', 'is not', null)
    .orderBy('received_at', 'desc')
    .limit(1)
    .executeTakeFirst();
  const counts = await db
    .selectFrom('crash_reports')
    .select([
      'platform',
      'distribution',
      sql<string>`count(*)`.as('reports'),
      sql<string>`count(DISTINCT install_id)`.as('installs'),
    ])
    .where('group_id', '=', groupId)
    .groupBy(['platform', 'distribution'])
    .orderBy('platform')
    .orderBy('distribution')
    .execute();
  const content = issueContent({
    groupId: group.id,
    signature: group.signature,
    firstVersion: group.first_version,
    frames: latest?.frames ?? [],
    counts: counts.map((c) => ({
      platform: c.platform,
      distribution: c.distribution,
      reports: Number(c.reports),
      installs: Number(c.installs),
    })),
    publicOrigin: config.publicOrigin,
  });
  const response = await (config.fetch ?? fetch)(
    `${config.apiUrl ?? 'https://api.github.com'}/repos/${config.repository}/issues`,
    {
      method: 'POST',
      headers: {
        accept: 'application/vnd.github+json',
        authorization: `Bearer ${config.token}`,
        'content-type': 'application/json',
        'user-agent': 'glob2-crash-worker',
        'x-github-api-version': '2022-11-28',
      },
      body: JSON.stringify(content),
      signal: AbortSignal.timeout(30_000),
    },
  );
  if (!response.ok)
    throw new Error(`GitHub refused the crash issue: ${response.status} ${response.statusText}`);
  const issue = (await response.json()) as { number?: unknown; html_url?: unknown };
  if (typeof issue.number !== 'number' || typeof issue.html_url !== 'string')
    throw new Error('GitHub answered without an issue number');
  await db
    .updateTable('crash_groups')
    .set({ github_issue: issue.number, github_issue_url: issue.html_url })
    .where('id', '=', groupId)
    .where('github_issue', 'is', null)
    .execute();
  logger.info({ group: groupId, issue: issue.number }, 'Crash issue opened');
  return issue.number;
}
