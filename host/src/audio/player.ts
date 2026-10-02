import { spawn, type ChildProcess } from "node:child_process";
import { EventEmitter } from "node:events";
import * as fs from "node:fs";

export class AudioPlayer extends EventEmitter {
  private activeProc?: ChildProcess;
  private _isPlaying = false;

  get isPlaying(): boolean {
    return this._isPlaying;
  }

  /**
   * Plays a WAV audio file on the host system speakers.
   * Resolves when playback completes, or immediately returns if stopped.
   */
  public async play(wavPath: string, volume = 1.0): Promise<void> {
    if (!fs.existsSync(wavPath)) {
      throw new Error(`Audio file does not exist: ${wavPath}`);
    }

    // Stop any in-flight playback first
    await this.stop();

    this._isPlaying = true;
    this.emit("play", wavPath);

    return new Promise<void>((resolve, reject) => {
      let proc: ChildProcess;

      if (process.platform === "win32") {
        // Use PowerShell SoundPlayer for instant zero-dependency synchronous playback
        const psScript = `
          $player = New-Object System.Media.SoundPlayer('${wavPath.replace(/'/g, "''")}');
          $player.PlaySync();
          $player.Dispose();
        `;
        const cmd = psScript.split(/\r?\n/).map(s => s.trim()).filter(Boolean).join(" ");
        proc = spawn("powershell", ["-NoProfile", "-NonInteractive", "-Command", cmd], {
          windowsHide: true,
          stdio: "ignore",
        });
      } else if (process.platform === "darwin") {
        proc = spawn("afplay", [wavPath], { stdio: "ignore" });
      } else {
        // Linux: try aplay or paplay
        proc = spawn("aplay", [wavPath], { stdio: "ignore" });
      }

      this.activeProc = proc;

      proc.on("exit", (code) => {
        this._isPlaying = false;
        this.activeProc = undefined;
        this.emit("ended", wavPath);
        resolve();
      });

      proc.on("error", (err) => {
        this._isPlaying = false;
        this.activeProc = undefined;
        this.emit("error", err);
        resolve(); // Don't throw unhandled rejection, resolve quietly
      });
    });
  }

  /**
   * Instantly stops any active audio playback.
   */
  public async stop(): Promise<void> {
    if (this.activeProc) {
      try {
        if (process.platform === "win32" && this.activeProc.pid) {
          spawn("taskkill", ["/F", "/T", "/PID", String(this.activeProc.pid)], { windowsHide: true });
        } else {
          this.activeProc.kill("SIGTERM");
        }
      } catch {}
      this.activeProc = undefined;
    }
    this._isPlaying = false;
    this.emit("stopped");
  }
}
