// Time source for the matchmaker, so tests can drive waits and deadlines.
export interface Clock {
  now(): Date;
}

export const systemClock: Clock = { now: () => new Date() };

/** A clock that only moves when told to. */
export class FakeClock implements Clock {
  private ms: number;
  constructor(start: Date | string = '2026-10-01T12:00:00Z') {
    this.ms = new Date(start).getTime();
  }
  now(): Date {
    return new Date(this.ms);
  }
  advance(seconds: number): Date {
    this.ms += seconds * 1000;
    return this.now();
  }
}
