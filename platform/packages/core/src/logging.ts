import { pino, type Logger } from 'pino';
import type { LogLevel } from './config.ts';

export type { Logger };

/** Structured JSON logs on stdout, one line per event, tagged with the service name. */
export function createLogger(service: string, level: LogLevel = 'info'): Logger {
  return pino({
    level,
    base: { service },
    timestamp: pino.stdTimeFunctions.isoTime,
    redact: {
      paths: [
        'req.headers.authorization',
        'req.headers.cookie',
        '*.accessToken',
        '*.refreshToken',
        '*.deviceCredential',
        '*.password',
        '*.ticket',
      ],
      censor: '[redacted]',
    },
  });
}
