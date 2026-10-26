<!-- SPDX-License-Identifier: Apache-2.0 -->
# Catalog deployment

## CI credential

The pinned GitHub Actions workflow requires a repository secret named
`CONVEX_PREVIEW_DEPLOY_KEY` for non-pull-request runs. It must be a
least-privilege Convex preview deploy key, never a personal token or production
credential. The CI job runs `convex deploy --dry-run` to validate the exact
schema, indexes, functions, generated API, and typecheck without promoting a
deployment. A missing key deliberately fails the protected main-branch gate;
fork pull requests run the credential-free host, web, and OVMF jobs only.

The `web/` application is a Next.js client whose server state and uploaded
artifacts live in Convex. Local tests use isolated development state and do not
prove a public deployment.

1. Authenticate the Convex and Vercel CLIs without committing tokens.
2. Link distinct development, preview, and production projects. Configure the
   public Convex URL and deployment name through provider-managed environment
   variables, never `.env` committed to source.
3. Configure a non-anonymous reviewer identity/policy before any publication.
4. Push the schema/functions, verify declared indexes and storage quotas, and
   exercise a realistic upload through analyzer completion.
5. Deploy the exact tested source digest to a Vercel preview. Run accessibility,
   upload, review/publication, catalog, download, and independent SHA-256 checks.
6. Download that published OCG2 into the OVMF acceptance flow, transactionally
   install it, launch/return, and exercise quarantine/recovery.
7. Promote only the exact preview deployment and artifact digests recorded by
   the clean release matrix. Never copy development fixture credentials into
   preview or production.

Required variables and provider identifiers are documented by `web/.env.example`.
The repository intentionally contains no deployment credential or reviewer
identity. Until linked-provider acceptance is recorded, hosted status is
“not deployed,” regardless of local Convex or Next.js success.
