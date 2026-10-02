const { CodexDesktopClient } = require("../dist/codex/desktop.js");

async function main() {
  console.log("=== Testing CodexDesktopClient ===");
  const client = new CodexDesktopClient();
  const connected = await client.connect();
  console.log("Connected:", connected);

  if (!connected) {
    console.log("Desktop not running or pipe not found.");
    process.exit(1);
  }

  const projects = await client.listProjects();
  console.log(`Discovered ${projects.length} projects:`);
  for (const p of projects) {
    console.log(` - [${p.projectKind}] ${p.label} (${p.projectId}) -> ${p.path || "N/A"}`);
  }

  const threads = await client.listThreads(5);
  console.log(`\nDiscovered ${threads.length} threads:`);
  for (const t of threads) {
    console.log(` - [${t.kind}] ${t.id} (${t.status}): "${t.title}"`);
  }

  const limits = await client.getUsageLimits();
  if (limits) {
    console.log(`\nLive Usage Limits:`);
    console.log(` - Primary window (5-hr): ${limits.primary.usedPercent}% used, resets at ${new Date(limits.primary.resetsAt * 1000).toISOString()}`);
    console.log(` - Secondary window (weekly): ${limits.secondary.usedPercent}% used, resets at ${new Date(limits.secondary.resetsAt * 1000).toISOString()}`);
    console.log(` - Plan: ${limits.planType}`);
  }

  client.disconnect();
  console.log("\nDesktop client verification SUCCESSFUL!");
}

main().catch((err) => {
  console.error("Test failed:", err);
  process.exit(1);
});
