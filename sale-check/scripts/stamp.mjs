// Writes public/build.json so the site footer and /api/version can say which
// build is live. Run automatically before dev and deploy.
import { execSync } from 'node:child_process';
import { writeFileSync } from 'node:fs';

const git = (cmd) => { try { return execSync(cmd, { stdio: ['ignore', 'pipe', 'ignore'] }).toString().trim(); } catch { return null; } };

const build = {
    number: Number(git('git rev-list --count HEAD')) || 0,
    commit: git('git rev-parse --short HEAD') || 'dev',
    date: new Date().toISOString(),
};
writeFileSync(new URL('../public/build.json', import.meta.url), JSON.stringify(build, null, 2) + '\n');
console.log(`build ${build.number} (${build.commit}) ${build.date}`);
