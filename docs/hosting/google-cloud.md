# Example Google Cloud deployment

A single-VM deployment example. The generic [quickstart](quickstart.md) and [backup guide](backup-restore.md) apply to other hosts.

## Example: one virtual machine on Google Cloud

A small instance fits on one Compute Engine VM. Building the images and the
WebAssembly client on the VM itself avoids a registry. The commands below use
placeholders (`glob2-host`, `REGION`, `ZONE`, `play.example.org`); keep secrets on
the VM only.

1. **Machine.** `e2-standard-4` (4 vCPUs, 16 GB) with a 100 GB balanced disk and
   Debian 12 is enough for the stack, one engine agent and image builds:

   ```sh
   gcloud compute addresses create glob2-host-ip --region REGION
   gcloud compute firewall-rules create glob2-host-web --network default \
       --allow tcp:80,tcp:443,udp:443 --target-tags glob2-host
   gcloud compute instances create glob2-host --zone ZONE --machine-type e2-standard-4 \
       --image-family debian-12 --image-project debian-cloud \
       --boot-disk-size 100GB --boot-disk-type pd-balanced \
       --address <reserved IP> --tags glob2-host --labels app=glob2
   ```

2. **DNS.** An `A` record for the domain (and `www` if wanted) pointing at the
   reserved address, with a short TTL while setting up. Caddy obtains the
   certificate from Let's Encrypt on first start, so the record must resolve before
   that.
3. **Docker.** Install Docker Engine and the Compose plugin from Docker's Debian
   repository, add your user to the `docker` group, and clone the repository.
4. **Configuration.** Keep the env file and `instance.yaml` outside the checkout,
   e.g. in a `0700` directory. Beyond [Setup from zero](quickstart.md#setup-from-zero), set
   `GLOB2_BIND=0.0.0.0`, `GLOB2_HTTP_PORT=80`, `GLOB2_HTTPS_PORT=443`,
   `GLOB2_DOMAIN`, `GLOB2_PUBLIC_ORIGIN` (and `GLOB2_REDIRECT_DOMAINS=www.<domain>`
   to redirect the `www` name), and point `GLOB2_INSTANCE_CONFIG`,
   `GLOB2_ENV_FILE` and `GLOB2_WEB_CLIENT_DIR` at absolute paths. With no sign-in
   providers yet, enable guests and `auth.local` in `instance.yaml`; a provider is
   added later by registering it ([Sign-in providers](security.md#sign-in-providers)), adding
   it to `instance.yaml` and its secret to the env file, and redeploying.
5. **Deploy and redeploy.** `deploy/update-host.sh /path/to/deployment.env
origin/<branch>` builds and starts everything; run it again for each new
   revision. The first build takes about half an hour on four vCPUs; later builds
   reuse the BuildKit caches. The official instance is redeployed from GitHub Actions instead;
   see [Automatic deployment](upgrades.md#automatic-deployment).
6. **Check.** Run the [attached smoke test and live match](operations.md#testing-a-deployment).

Estimate VM, disk, address, DNS and egress charges using the provider's current
pricing for your region. Stopping the VM does not remove persistent disks or
reserved addresses. To remove the deployment, delete the VM, address, firewall
rule and DNS records after preserving the backups you need.

[Hosting index](README.md) · [Documentation index](../README.md).
