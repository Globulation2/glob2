# Troubleshoot an online instance

Start with service health and the relevant logs. Preserve database, blob and key
volumes while diagnosing a failed deployment.

## Collect the initial evidence

Run from the deployment checkout with the instance's environment file:

```sh
docker compose ps
docker compose logs --tail 200 init platform-api platform-worker engine-agent relay caddy
```

Record the deployed revision, image identities and the failing service. Remove
credentials and player data before sharing evidence. Health endpoints are backend
interfaces; their absence on the public origin is intentional.

## Choose the failing boundary

| Symptom | First checks | Follow-up guide |
| --- | --- | --- |
| Stack does not become healthy | Read `init` logs for migration/key initialization errors, then inspect the unhealthy service's logs. Check environment and instance-file mounts. | [Stack permissions](stack.md), [configuration](configuration.md) |
| Public app or API unavailable | Check Caddy, DNS, certificate trust and the configured public origin. Test the backend before changing public routing. | [DNS and TLS](networking.md) |
| Provider sign-in fails | Compare the provider's callback URL and configured origin; check that the expected provider secret is available to the API. | [Sign-in configuration](security.md) |
| Client reports an unsupported simulation version | Compare the client version with `/api/v1/instance`, then inspect agent registration and startup logs. Use a matching engine-agent image. | [Simulation versions](../multiplayer/architecture.md#simulation-versions), [agent upgrades](upgrades.md#sim-versions-and-engine-agents) |
| Map generation or publication remains queued | Inspect engine-agent last-seen state, supported job kinds and job errors in the administrator Operations view. | [Engine jobs](../multiplayer/engine-agents.md), [content validation](content-validation.md) |
| Match result remains failed or unrated | Inspect the verification job and relay upload logs before requesting re-verification. | [Verification operations](operations.md#operations), [admin reporting](admin-reporting.md) |
| Studio request has an uncertain provider outcome | Stop automatic redispatch and inspect metering and reservation evidence in the administrator Operations view. | [Admin recovery](admin-reporting.md), the relevant [Studio deployment guide](README.md#optional-services) |

## Verify recovery

Use the [deployment checks](operations.md#testing-a-deployment) appropriate to the
failure. An attached smoke test contacts the live instance; the isolated stack
test creates disposable services and volumes. Match tests create real accounts
or guest sessions and matches, so choose the intended environment explicitly.

For a failed upgrade, follow [rollback and upgrade procedures](upgrades.md)
rather than deleting volumes. Test [backup restoration](backup-restore.md) in a
separate instance before replacing production data.

[Hosting index](README.md) · [Documentation index](../README.md).
