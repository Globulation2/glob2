# Deploy Generator Studio

Opt-in generator coding, browser isolation and release gates.

## Generator Studio rollout

Deploy migration `0058_generator_studio.sql` and API support before installing the
web client. Generator Studio is independently opt-in: leave
`generatorStudio.enabled` false until a maintainer has played through authoring,
preview, checks and publication. The flag gates project and tool APIs as well as
model requests; account export remains available for recovery when disabled.
Configure its `model`, versioned token `rate`,
`maxRequestCredits` and `maxOutputTokens` as for AI Studio, with
`GENERATOR_STUDIO_OPENAI_API_KEY`. Optional sales additionally require packs,
`salesEnabled`, `GENERATOR_STUDIO_STRIPE_SECRET_KEY` and
`GENERATOR_STUDIO_STRIPE_WEBHOOK_SECRET`. Webhook and reconciliation paths are
`/api/v1/generator-studio/stripe` and `/api/v1/generator-studio/reconcile`.
Generator credits, reservations, purchases and financial reporting are separate.

Serve `/generator-studio` and its project routes with COOP/COEP, including the
Monaco editor, TypeScript and JSON worker responses. Only the dedicated
`/play/generator-studio.html` entry permits same-origin framing. The installer
copies the entry and available precompressed versions before the main index;
ordinary game and account pages remain unframeable. Keep the existing isolated
Generator Library engine validators available for checks and publication; browser
reports are untrusted development summaries. Monitor generator request leases,
uncertain calls, validator queue age and browser launch failures.

[Hosting index](README.md) · [Documentation index](../README.md).
