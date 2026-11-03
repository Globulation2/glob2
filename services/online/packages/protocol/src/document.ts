import { schemaRegistry } from './registry.ts';
import { schemaIssues, type ValidationIssue } from './validate.ts';

export interface DocumentCheck {
  /** 'schema' when the JSON Schema rejects it, 'semantic' when only cross-field rules do. */
  stage: 'ok' | 'schema' | 'semantic';
  issues: ValidationIssue[];
}

/** Validates a document against a registered schema and its semantic rules. */
export function checkDocument(schemaName: string, value: unknown): DocumentCheck {
  const entry = schemaRegistry[schemaName];
  if (!entry) throw new Error(`unknown schema ${schemaName}`);
  const issues = schemaIssues(entry.schema, value);
  if (issues.length > 0) return { stage: 'schema', issues };
  const problems = entry.semantic ? entry.semantic(value) : [];
  if (problems.length > 0) return { stage: 'semantic', issues: problems };
  return { stage: 'ok', issues: [] };
}
