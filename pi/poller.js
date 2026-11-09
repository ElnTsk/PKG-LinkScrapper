// Watches the Logs channel for dead link reports from the PKG-LinkScrapper
// webhook, puts a tick and a cross on each one, and writes the reports the
// owner approves into deadlinks.txt in the GitHub repo. REST only - it never
// opens a gateway connection, so the community bot is untouched.
'use strict';

const fs = require('fs');
const path = require('path');

const DIR = '/opt/pkg-deadlinks';
const STATE_FILE = path.join(DIR, 'state.json');
const CHANNEL = '1311681018612158508';
const WEBHOOK_ID = '1559172286479601734';
const OWNER = '413515992790990848';
const REPO = 'ElnTsk/PKG-LinkScrapper';
const FILE = 'deadlinks.txt';
const API = 'https://discord.com/api/v10';
const TICK_E = encodeURIComponent('\u2705');
const CROSS_E = encodeURIComponent('\u274C');
const INTERVAL_MS = 15000;

const TOKEN = JSON.parse(fs.readFileSync('/opt/pshomebrew-bot/config.json', 'utf8')).token;
const PAT = fs.readFileSync(path.join(DIR, 'pat'), 'utf8').trim();

let state = { last: '0', pending: {}, approved: [], denied: [] };
try {
  state = Object.assign(state, JSON.parse(fs.readFileSync(STATE_FILE, 'utf8')));
} catch (e) {
  // first run
}

const save = () => fs.writeFileSync(STATE_FILE, JSON.stringify(state, null, 1));

function log(line) {
  console.log(new Date().toISOString() + ' ' + line);
}

async function api(method, route, body) {
  for (let attempt = 0; attempt < 4; ++attempt) {
    const response = await fetch(API + route, {
      method,
      headers: { Authorization: 'Bot ' + TOKEN, 'Content-Type': 'application/json' },
      body: body ? JSON.stringify(body) : undefined
    });
    if (response.status === 204) return null;
    const text = await response.text();
    if (response.status === 429) {
      let waitMs = 1000;
      try {
        waitMs = Math.ceil((JSON.parse(text).retry_after || 1) * 1000) + 150;
      } catch (e) {
        // fall through with the default
      }
      await new Promise((resolve) => setTimeout(resolve, waitMs));
      continue;
    }
    if (!response.ok) throw new Error('discord ' + response.status + ' ' + text.slice(0, 200));
    return text ? JSON.parse(text) : null;
  }
  throw new Error('discord rate limited past four attempts');
}

async function github(method, route, body) {
  const response = await fetch('https://api.github.com' + route, {
    method,
    headers: {
      Authorization: 'Bearer ' + PAT,
      Accept: 'application/vnd.github+json',
      'User-Agent': 'pkg-deadlinks'
    },
    body: body ? JSON.stringify(body) : undefined
  });
  const text = await response.text();
  if (!response.ok) throw new Error('github ' + response.status + ' ' + text.slice(0, 200));
  return text ? JSON.parse(text) : null;
}

const clean = (value) => String(value || '').replace(/[\t\r\n]+/g, ' ').slice(0, 300);

function parseReport(message) {
  if (message.webhook_id !== WEBHOOK_ID) return null;
  const embed = (message.embeds || [])[0];
  if (!embed || !/^Dead link report #/.test(embed.title || '')) return null;
  const match = embed.title.match(/#([0-9a-f]{6})/);
  if (!match) return null;
  const field = (name) => {
    const found = (embed.fields || []).find((item) => item.name === name);
    return found ? found.value : '';
  };
  const url = field('URL');
  if (!url) return null;
  return {
    code: match[1],
    url,
    host: field('Host'),
    source: field('Source'),
    game: field('Game')
  };
}

async function approve(entry) {
  const line = [entry.code, new Date().toISOString(), clean(entry.host), clean(entry.source),
                clean(entry.game), entry.url].join('\t');
  const current = await github('GET', `/repos/${REPO}/contents/${FILE}?ref=main`);
  const text = current.content ? Buffer.from(current.content, 'base64').toString('utf8') : '';
  const known = text.split('\n').some((row) => row.split('\t').pop() === entry.url);
  if (!known) {
    const next = text.replace(/\n*$/, '\n') + line + '\n';
    await github('PUT', `/repos/${REPO}/contents/${FILE}`, {
      message: 'deadlink ' + entry.code,
      content: Buffer.from(next, 'utf8').toString('base64'),
      sha: current.sha,
      branch: 'main'
    });
  }
  if (entry.messageId) {
    await api('POST', `/channels/${CHANNEL}/messages`, {
      content: '#' + entry.code + ' written to deadlinks.txt',
      message_reference: { message_id: entry.messageId, fail_if_not_exists: false },
      allowed_mentions: { parse: [], replied_user: false }
    }).catch(() => {});
  }
  log('approved ' + entry.code + ' ' + entry.url);
}

async function verdict(code, allow) {
  const entry = state.pending[code];
  if (!entry) return;
  if (allow) {
    try {
      await approve(entry);
      state.approved.push(code);
    } catch (error) {
      log('approve failed for ' + code + ': ' + error.message + ' - will retry');
      return;
    }
  } else {
    state.denied.push(code);
    if (entry.messageId) {
      await api('POST', `/channels/${CHANNEL}/messages`, {
        content: '#' + code + ' rejected - not stored',
        message_reference: { message_id: entry.messageId, fail_if_not_exists: false },
        allowed_mentions: { parse: [] }
      }).catch(() => {});
    }
    log('denied ' + code);
  }
  delete state.pending[code];
  save();
}

async function tick() {
  // after= walks forward from an old id; without one, start at the newest page
  const route = state.last && state.last !== '0'
    ? `/channels/${CHANNEL}/messages?limit=50&after=${state.last}`
    : `/channels/${CHANNEL}/messages?limit=50`;
  const batch = await api('GET', route);
  if (Array.isArray(batch) && batch.length) {
    const newest = batch.reduce((best, m) => (BigInt(m.id) > BigInt(best) ? m.id : best), batch[0].id);
    if (batch.length === 50) log('50 messages in one tick - older ones in the gap are skipped');
    for (const message of batch.slice().reverse()) {
      if (message.author && message.author.id === OWNER && typeof message.content === 'string') {
        const command = message.content.match(/^!(allow|deny)\s+([0-9a-f]{6})/i);
        if (command) await verdict(command[2].toLowerCase(), command[1].toLowerCase() === 'allow');
      }
      const report = parseReport(message);
      if (report && !state.approved.includes(report.code) && !state.denied.includes(report.code)) {
        state.pending[report.code] = Object.assign(report, { messageId: message.id });
        await api('PUT', `/channels/${CHANNEL}/messages/${message.id}/reactions/${TICK_E}/@me`);
        await api('PUT', `/channels/${CHANNEL}/messages/${message.id}/reactions/${CROSS_E}/@me`);
        log('report ' + report.code + ' ' + report.url + ' - reactions added');
      }
    }
    // advance only once the batch is fully handled - a failed tick retries it
    state.last = newest;
    save();
  }

  for (const [code, entry] of Object.entries(state.pending)) {
    try {
      const ticks = await api('GET', `/channels/${CHANNEL}/messages/${entry.messageId}/reactions/${TICK_E}?limit=100`);
      if (Array.isArray(ticks) && ticks.some((user) => user.id === OWNER)) {
        await verdict(code, true);
        continue;
      }
      const crosses = await api('GET', `/channels/${CHANNEL}/messages/${entry.messageId}/reactions/${CROSS_E}?limit=100`);
      if (Array.isArray(crosses) && crosses.some((user) => user.id === OWNER)) {
        await verdict(code, false);
      }
    } catch (error) {
      log('reaction check ' + code + ': ' + error.message);
    }
  }
}

async function main() {
  log('poller up - channel ' + CHANNEL + ', every ' + INTERVAL_MS / 1000 + 's');
  for (;;) {
    try {
      await tick();
    } catch (error) {
      log('tick failed: ' + error.message);
    }
    await new Promise((resolve) => setTimeout(resolve, INTERVAL_MS));
  }
}

module.exports = { approve, verdict, tick };

if (require.main === module) main();
