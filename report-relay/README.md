# report-relay

The tiny Vercel function that lets spdsx-patchedit users file bug reports
without a GitHub account: the app POSTs a report here (Help ▸ Report a Bug
or Send Feedback…), and this relay — which holds the GitHub token — files
it as an issue.

One endpoint: `POST /api/v1/report` with
`{category, text, environment, log}`; replies `201 {number, url}`.
Reports are labeled `in-app` plus `bug`/`enhancement`/`feedback`, with the
user's text, the environment block, and the app's recent-activity log all
fenced (so markdown, `@mentions` and `#refs` render inert).

## Deploy

```sh
cd report-relay
vercel deploy --prod
```

Environment variables (Vercel dashboard ▸ Settings ▸ Environment Variables):

| var | required | meaning |
| --- | --- | --- |
| `GITHUB_TOKEN` | yes | fine-grained PAT scoped to ONE repo, permission Issues: read/write, nothing else |
| `GITHUB_REPO` | no | target repo, default `jdf/spdsx-pro-kitedit` |
| `REPORT_SECRET` | no | if set, requests must carry it in `x-report-secret`; deters drive-by scanners (it ships inside the app binary, so it is a filter, not a lock) |

After the first deploy, point the app at the real URL: the endpoint
constant lives in `source/feedback_dialog.cc` (`ReportEndpoint()`), and
`SPDSX_REPORT_URL` overrides it at runtime for testing against
`vercel dev`.

## Smoke test

```sh
curl -sS -X POST "$URL/api/v1/report" \
  -H 'Content-Type: application/json' \
  -d '{"category":"feedback","text":"relay smoke test — please close me",
       "environment":"app: smoke","log":""}'
```

Expect `201` and the issue URL in the reply (then close the issue).

## Abuse posture

Payload caps (20 KB text / 40 KB log), a best-effort per-instance rate
limit (10/hour/IP), and everything it creates carries the `in-app` label
for one-click triage. If real abuse ever shows up: set `REPORT_SECRET`,
move the rate limit to Upstash, or point `GITHUB_REPO` at a dedicated
reports repo so the blast radius is an issue list and nothing else.
