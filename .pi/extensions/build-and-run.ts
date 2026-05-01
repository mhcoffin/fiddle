import { exec } from "child_process";
import { promisify } from "util";
import type { ExtensionAPI } from "@mariozechner/pi-coding-agent";

const execAsync = promisify(exec);

export default function (pi: ExtensionAPI) {
  pi.registerCommand("run", {
    description: "Build, kill running instance, and start FiddleServer",
    handler: async (args, ctx) => {
      ctx.ui.notify("Building FiddleServer... this may take a moment.", "info");
      try {
        const { stdout, stderr } = await execAsync("./fiddle.sh", { cwd: ctx.cwd });
        ctx.ui.notify("FiddleServer built and started successfully.", "success");
      } catch (err: any) {
        ctx.ui.notify(`Failed to build FiddleServer: ${err.message}`, "error");
      }
    },
  });
}
