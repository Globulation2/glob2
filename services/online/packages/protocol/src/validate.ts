import type { Static, TSchema } from 'typebox';
import { Compile, type Validator } from 'typebox/compile';

const compiled = new WeakMap<TSchema, Validator>();

function validatorFor(schema: TSchema): Validator {
  let validator = compiled.get(schema);
  if (!validator) {
    validator = Compile(schema);
    compiled.set(schema, validator);
  }
  return validator;
}

export interface ValidationIssue {
  path: string;
  message: string;
}

export function schemaIssues(schema: TSchema, value: unknown): ValidationIssue[] {
  const validator = validatorFor(schema);
  if (validator.Check(value)) return [];
  return [...validator.Errors(value)].map((error) => ({
    path: error.instancePath || '/',
    message: error.message,
  }));
}

export function isValid<T extends TSchema>(schema: T, value: unknown): value is Static<T> {
  return validatorFor(schema).Check(value);
}

export class ProtocolValidationError extends Error {
  readonly issues: ValidationIssue[];
  constructor(what: string, issues: ValidationIssue[]) {
    super(
      `${what} is invalid: ${issues
        .slice(0, 5)
        .map((issue) => `${issue.path} ${issue.message}`)
        .join('; ')}`,
    );
    this.name = 'ProtocolValidationError';
    this.issues = issues;
  }
}

/** Returns the value typed as the schema, or throws ProtocolValidationError. */
export function parse<T extends TSchema>(schema: T, value: unknown, what = 'value'): Static<T> {
  const issues = schemaIssues(schema, value);
  if (issues.length > 0) throw new ProtocolValidationError(what, issues);
  return value as Static<T>;
}
