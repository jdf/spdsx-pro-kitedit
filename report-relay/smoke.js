const handler = require('./api/v1/report.js');

let captured = null;
global.fetch = async (url, opts) => {
  captured = {url, payload: JSON.parse(opts.body)};
  return {ok: true, status: 201,
          json: async () => ({number: 47, url: 'https://github.com/jdf/spdsx-patchedit/issues/47'})};
};
process.env.GITHUB_TOKEN = 'test-token';

function fakeRes() {
  const res = {code: 0, body: null};
  res.status = (c) => { res.code = c; return res; };
  res.json = (b) => { res.body = b; return res; };
  return res;
}

async function run(name, req, expectCode) {
  const res = fakeRes();
  await handler(req, res);
  const ok = res.code === expectCode;
  console.log(`${ok ? 'PASS' : 'FAIL'} ${name}: ${res.code} ${JSON.stringify(res.body)}`);
  return ok;
}

(async () => {
  let all = true;
  all &= await run('happy path', {method:'POST', headers:{},
    body:{category:'bug', text:'pad 3 plays nothing\nafter sync',
          environment:'app: spdsx-patchedit 0.1.0', log:'  0.0  sync started'}}, 201);
  console.log('  title:', captured.payload.title);
  console.log('  labels:', JSON.stringify(captured.payload.labels));
  console.log('  body head:', JSON.stringify(captured.payload.body.slice(0,80)));
  all &= await run('mention neutralized', {method:'POST', headers:{},
    body:{category:'feedback', text:'hey @torvalds look at ```this```', environment:'x', log:''}}, 201);
  const fencedOk = captured.payload.body.includes('````') && !captured.payload.body.match(/^@torvalds/m);
  console.log(`  ${fencedOk ? 'PASS' : 'FAIL'} user text fenced`);
  all &= await run('GET rejected', {method:'GET', headers:{}}, 405);
  all &= await run('no text', {method:'POST', headers:{}, body:{category:'bug', text:'  '}}, 400);
  all &= await run('bad category', {method:'POST', headers:{}, body:{category:'rant', text:'x'}}, 400);
  all &= await run('oversized', {method:'POST', headers:{},
    body:{category:'bug', text:'x'.repeat(30000)}}, 400);
  process.env.REPORT_SECRET = 's3cret';
  all &= await run('secret enforced', {method:'POST', headers:{},
    body:{category:'bug', text:'x'}}, 401);
  all &= await run('secret accepted', {method:'POST', headers:{'x-report-secret':'s3cret'},
    body:{category:'bug', text:'x'}}, 201);
  delete process.env.REPORT_SECRET;
  // rate limit: same ip, 10 allowed + 11th refused (2 already used above with 'unknown' ip... count)
  let last = 0;
  for (let i = 0; i < 12; i++) {
    const res = fakeRes();
    await handler({method:'POST', headers:{'x-forwarded-for':'9.9.9.9'},
      body:{category:'bug', text:'flood'}}, res);
    last = res.code;
  }
  console.log(`${last === 429 ? 'PASS' : 'FAIL'} rate limit kicks in (${last})`);
  all &= last === 429;
  console.log(all ? 'ALL PASS' : 'SOME FAILED');
})();
