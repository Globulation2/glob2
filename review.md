Independent review completed in two subagent passes, followed by rereview of the fixes. No remaining blockers were reported.

Core review covered collector/history accounting, bounded executor metadata, handoff synchronization, teardown, and simulation independence. Findings addressed:

- Budget the retained decorative menu executor as well as the foreground match; preallocate only the timing banks each executor needs before workers start.
- Transfer mailbox samples and their logical-tick watermark under the same lock.
- Retain direct external-completion suspension timing before retiring its completion ticket.
- Drain outstanding background jobs before final telemetry even when optional diagnostics are off.
- Distinguish GPU timing sources and frame-level counter populations in descriptors.

Presentation/GPU/export review covered shared live/results charts, caching and geometry, units, availability, query lifecycle, and lossless reader behavior. Findings addressed:

- Select retained windows by index when paused windows share a tick.
- Show sampled WebGPU render-pass timing in the primary rendering card.
- Correct sampled-call rates, capture-latency means, and unavailable distributions.
- Respect enlarged text and measured plot margins; bound projected chart capacity.
- Roll back partial WebGPU query allocation, disable only optional timing on failure, and ignore late retired callbacks.
- Remove obsolete compatibility-version prose from tournament documentation.

Final rereviews found no new architectural complexity or correctness blocker. These are independent static reviews plus browser unit execution, not a human maintainer approval or physical GPU certification. Final Linux native/integration checks are being attached separately.
