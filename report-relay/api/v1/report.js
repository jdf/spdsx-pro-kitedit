// The report relay: accepts a feedback report POSTed by spdsx-patchedit
// (see source/feedback_dialog.cc) and files it as a GitHub issue, so
// users can report bugs without a GitHub account. The GitHub token lives
// here, server-side, and nowhere in the app.
//
// POST /api/v1/report  {category, text, environment, log}
//   -> 201 {number, url} on success.
//
// Deployed on Vercel (zero-config functions dir); see ../README.md.

const LABELS = {bug: 'bug', feature: 'enhancement', feedback: 'feedback'};
const EMOJI = {bug: '\u{1F41E}', feature: '\u{2728}', feedback: '\u{1F4AC}'};

// Hard caps: anything bigger is discarded, not truncated silently at
// GitHub's end. Generous for real reports, stingy for floods.
const MAX_TEXT = 20000;
const MAX_ENV = 4000;
const MAX_LOG = 40000;

// Best-effort per-instance rate limit: enough to stop a dumb loop, not a
// distributed abuser (move to Upstash/KV if that day comes).
const kWindowMs = 60 * 60 * 1000;
const kMaxPerWindow = 10;
const hits = new Map(); // ip -> [timestamps]

function limited(ip) {
  const now = Date.now();
  const recent = (hits.get(ip) || []).filter((t) => now - t < kWindowMs);
  recent.push(now);
  hits.set(ip, recent);
  return recent.length > kMaxPerWindow;
}

// User text goes into the issue inside a fence, so markdown, @mentions
// and #refs render inert. Four backticks so embedded ``` can't escape.
function fenced(text) {
  return '````\n' + text.replace(/````/g, "''''") + '\n````';
}

function issueTitle(category, text) {
  const firstLine = text.trim().split('\n')[0].slice(0, 80).trim();
  return `${EMOJI[category]} ${firstLine || 'in-app report'}`;
}

function issueBody(category, text, environment, log) {
  const parts = [fenced(text.trim()), '', '### Environment',
                 fenced(environment.trim())];
  if (log.trim()) {
    parts.push('', '<details><summary>Recent app activity</summary>', '',
               fenced(log), '', '</details>');
  }
  parts.push('', `_Filed from the app (${category})._`);
  return parts.join('\n');
}

async function createIssue(repo, token, title, body, labels) {
  const post = (payload) =>
      fetch(`https://api.github.com/repos/${repo}/issues`, {
        method: 'POST',
        headers: {
          'Authorization': `Bearer ${token}`,
          'Accept': 'application/vnd.github+json',
          'Content-Type': 'application/json',
          'User-Agent': 'spdsx-patchedit-report-relay',
        },
        body: JSON.stringify(payload),
      });
  let r = await post({title, body, labels});
  if (r.status === 422) {
    // A missing label must not eat the report.
    r = await post({title, body});
  }
  return r;
}

module.exports = async (req, res) => {
  if (req.method !== 'POST') {
    return res.status(405).json({error: 'POST only'});
  }
  const secret = process.env.REPORT_SECRET;
  if (secret && req.headers['x-report-secret'] !== secret) {
    return res.status(401).json({error: 'bad secret'});
  }
  const ip = (req.headers['x-forwarded-for'] || 'unknown').split(',')[0];
  if (limited(ip)) {
    return res.status(429).json({error: 'too many reports; try later'});
  }

  const {category, text, environment = '', log = ''} = req.body || {};
  if (!LABELS[category] || typeof text !== 'string' || !text.trim()) {
    return res.status(400).json({error: 'need category and text'});
  }
  if (typeof environment !== 'string' || typeof log !== 'string'
      || text.length > MAX_TEXT || environment.length > MAX_ENV
      || log.length > MAX_LOG) {
    return res.status(400).json({error: 'report too large'});
  }

  const repo = process.env.GITHUB_REPO || 'jdf/spdsx-patchedit';
  const token = process.env.GITHUB_TOKEN;
  if (!token) {
    return res.status(500).json({error: 'relay not configured'});
  }

  const r = await createIssue(repo, token,
                              issueTitle(category, text),
                              issueBody(category, text, environment, log),
                              ['in-app', LABELS[category]]);
  if (!r.ok) {
    return res.status(502).json({error: `github said ${r.status}`});
  }
  const issue = await r.json();
  return res.status(201).json({number: issue.number, url: issue.html_url});
};
