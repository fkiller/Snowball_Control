import { spawn, type ChildProcess } from "node:child_process";
import { EventEmitter } from "node:events";
import * as fs from "node:fs";
import * as path from "node:path";
import * as os from "node:os";
import * as net from "node:net";
import { audioEndpoint, snauHeader, openAudio, type AudioEndpoint } from "./snau.js";

export class AudioPlayer extends EventEmitter {
  private activeProc?: ChildProcess;
  private activeDeviceSocket?: net.Socket;
  private _isPlaying = false;
  private endpoint?: AudioEndpoint;
  private generation=0;
  private currentVolume = 1.0;
  private isMuted = false;
  constructor(address = process.env.SNOWBALL_MK20_ADDRESS || "", _maintenanceTool?: string, lease?: string, port = 7702, localAddress?: string) {
    super(); if (address) this.endpoint = audioEndpoint(address, port, lease, localAddress);
  }
  get isPlaying(): boolean {return this._isPlaying;}
  public setVolume(volume: number, muted = false): void {
    this.currentVolume = Math.max(0, Math.min(1, volume)); this.isMuted = muted;
    if (!this.endpoint) return;
    const endpoint = this.endpoint;
    const socket = net.createConnection({host:endpoint.host, port:endpoint.port, localAddress:endpoint.localAddress});
    socket.once("connect", () => socket.end(snauHeader(endpoint, 4, 1, this.currentVolume * 100, muted)));
    socket.on("error", () => {}); socket.setTimeout(1000, () => socket.destroy()); socket.resume();
  }
  public async play(wavPath: string, volume = 1.0): Promise<void> {
    if (!fs.existsSync(wavPath)) {
      throw new Error(`Audio file does not exist: ${wavPath}`);
    }

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

    const volClamped = Math.max(0, Math.min(1.0, volume));
    if (volClamped <= 0.001) {
      return;
    }

    this._isPlaying = true;
    this.emit("play", { wavPath, target: "host" });

    let playbackWav = wavPath;
    let tempScaledWav: string | undefined;

    // Apply software PCM volume scaling for host output if volume < 0.99
    if (volClamped < 0.99) {
      try {
        const rawWav = fs.readFileSync(wavPath);
        if (rawWav.length > 44 && rawWav.toString("utf8", 0, 4) === "RIFF") {
          let dataOffset = 44;
          const dataIdx = rawWav.indexOf("data");
          if (dataIdx >= 0 && dataIdx + 8 <= rawWav.length) {
            dataOffset = dataIdx + 8;
          }
          const scaledWav = Buffer.from(rawWav);
          const numSamples = Math.floor((rawWav.length - dataOffset) / 2);
          for (let i = 0; i < numSamples; i++) {
            const sample = rawWav.readInt16LE(dataOffset + i * 2);
            const scaled = Math.max(-32768, Math.min(32767, Math.round(sample * volClamped)));
            scaledWav.writeInt16LE(scaled, dataOffset + i * 2);
          }
          tempScaledWav = path.join(os.tmpdir(), `snowball_host_vol_${Date.now()}_${Math.random().toString(36).slice(2, 6)}.wav`);
          fs.writeFileSync(tempScaledWav, scaledWav);
          playbackWav = tempScaledWav;
        }
      } catch (err: any) {
        console.warn("[AudioPlayer] Host PCM volume scaling error:", err.message);
      }
    }

    return new Promise<void>((resolve, reject) => {
      let proc: ChildProcess;

      if (process.platform === "win32") {
        // Use PowerShell SoundPlayer for instant zero-dependency synchronous playback
        const psScript = `
          $ErrorActionPreference = 'Stop';
          $player = New-Object System.Media.SoundPlayer('${playbackWav.replace(/'/g, "''")}');
          $player.PlaySync();
          $player.Dispose();
        `;
        const cmd = psScript.split(/\r?\n/).map(s => s.trim()).filter(Boolean).join(" ");
        proc = spawn("powershell", ["-NoProfile", "-NonInteractive", "-Command", cmd], {
          windowsHide: true,
          stdio: "ignore",
        });
      } else if (process.platform === "darwin") {
        proc = spawn("afplay", [playbackWav], { stdio: "ignore" });
      } else {
        // Linux: try aplay or paplay
        proc = spawn("aplay", [playbackWav], { stdio: "ignore" });
      }

      this.activeProc = proc;

      const cleanup = () => {
        if (tempScaledWav) {
          try { fs.rmSync(tempScaledWav, { force: true }); } catch {}
          tempScaledWav = undefined;
        }
      };

      proc.once("close", (code, signal) => {
        cleanup();
        if(this.activeProc===proc){this._isPlaying=false;this.activeProc=undefined;}
        if(code===0){this.emit("ended", {wavPath,target:"host"});resolve();}
        else reject(Error(`Host playback failed (${signal ?? code})`));
      });
      proc.once("error", (err) => {
        cleanup();
        if(this.activeProc===proc){this._isPlaying=false;this.activeProc=undefined;}
        reject(err);
      });
    });
  }


  public async playOnDevice(wavPath: string, volume = 1): Promise<void> {
    const generation=++this.generation;
    if (!this.endpoint) throw Error("No selected MK20 audio endpoint");
    const wav = fs.readFileSync(wavPath);
    if (wav.length < 44 || wav.toString("ascii", 0, 4) !== "RIFF" || wav.readUInt16LE(20) !== 1 || wav.readUInt16LE(34) !== 16) throw Error("Expected PCM16 WAV");
    let offset = 12, pcm: Buffer | undefined;
    while (offset + 8 <= wav.length) {
      const size = wav.readUInt32LE(offset + 4);
      if (offset + 8 + size > wav.length) throw Error("Truncated WAV");
      if (wav.toString("ascii", offset, offset + 4) === "data") {pcm = wav.subarray(offset + 8, offset + 8 + size); break;}
      offset += 8 + size + (size % 2);
    }
    if (!pcm) throw Error("Missing WAV PCM data");
    const {socket} = await openAudio(this.endpoint, snauHeader(this.endpoint, 1, wav.readUInt16LE(22), volume * 100, this.isMuted, wav.readUInt32LE(24), pcm.length));
    if(generation!==this.generation){socket.destroy();throw Error('MK20 playback cancelled before readiness');}
    this.activeDeviceSocket = socket; this._isPlaying = true;
    try {
      await new Promise<void>((resolve, reject) => {
        let response = "";
        socket.on("data", data => { response += data.toString("ascii"); if (response.length > 64) socket.destroy(Error("Invalid audio completion")); });
        socket.once("error", reject);
        socket.once("end", () => response === "DONE" ? resolve() : reject(Error("MK20 playback interrupted or failed")));
        socket.once("close", () => {if (response !== "DONE") reject(Error("MK20 playback connection closed"));});
        socket.setTimeout(120000, () => socket.destroy(Error("MK20 playback timed out")));
        socket.end(pcm);
      });
    } finally {socket.destroy(); if(this.activeDeviceSocket===socket)this.activeDeviceSocket=undefined; if(generation===this.generation)this._isPlaying=false;}
  }
  public async stop(): Promise<void> {
    this.generation++;
    if (this.activeProc) {
      if(process.platform === "win32" && this.activeProc.pid) spawn("taskkill", ["/F","/T","/PID",String(this.activeProc.pid)],{windowsHide:true});
      else this.activeProc.kill("SIGTERM");
      this.activeProc = undefined;
    }
    this.activeDeviceSocket?.destroy(); this.activeDeviceSocket = undefined;
    if (this.endpoint) {
      const endpoint = this.endpoint;
      await openAudio(endpoint, snauHeader(endpoint,5)).then(({socket})=>socket.destroy()).catch(()=>{});
    }
    this._isPlaying=false; this.emit("stopped");
  }
}
