import * as dgram from "node:dgram";
import { DeviceInputPacket, HostSyncPacket, KEY_FLAG_FILLED, KEY_FLAG_EDITING, KEY_FLAG_FOCUSED, KEY_FLAG_DISABLED, KEY_FLAG_LIST, KEY_FLAG_LINES } from "../protocol/messages.js";
import { EventEmitter } from "node:events";
import type { V2DeviceState } from "../types.js";

export class UdpTransport extends EventEmitter {
  private socket: dgram.Socket;
  private port: number;
  private targetHost: string = process.env.SNOWBALL_DEVICE_IP || "192.168.1.248";
  private targetPort: number = 7701;
  private syncSeq = 1;

  constructor(port = 7701, targetHost = process.env.SNOWBALL_DEVICE_IP || "192.168.1.248", targetPort = 7701) {
    super();
    this.port = port;
    this.targetHost = targetHost;
    this.targetPort = targetPort;
    this.socket = dgram.createSocket("udp4");

    this.socket.on("message", (msg, rinfo) => {
      this.handleIncoming(msg, rinfo);
    });

    this.socket.on("error", (err) => {
      console.error("[UDP] Socket error:", err);
    });
  }

  public start(): Promise<void> {
    return new Promise((resolve, reject) => {
      this.socket.once("error", reject);
      this.socket.bind(this.port, "0.0.0.0", () => {
        this.socket.off("error", reject);
        console.log(`[UDP] Server listening on port ${this.port}`);
        resolve();
      });
    });
  }

  public stop(): Promise<void> {
    return new Promise((resolve) => {
      this.socket.close(() => resolve());
    });
  }

  private handleIncoming(msg: Buffer, rinfo: dgram.RemoteInfo) {
    if (rinfo.address !== this.targetHost || rinfo.port !== this.targetPort || msg.length > 1024) return;
    const str = msg.toString("utf-8").trim();
    try {
      const packet = JSON.parse(str);
      if (!packet || typeof packet !== "object" || Array.isArray(packet)) return;
      if (packet.seq !== undefined && (!Number.isSafeInteger(packet.seq) || packet.seq < 0)) return;
      const valid = packet.type === "ping" ||
        (packet.type === "key" && Number.isInteger(packet.keyId) && packet.keyId >= 1 && packet.keyId <= 20 && typeof packet.isDown === "boolean") ||
        (["knob_left", "knob_right"].includes(packet.type) &&
          (packet.delta !== undefined ? Number.isInteger(packet.delta) && Math.abs(packet.delta) <= 1024 : packet.isClick === true));
      if (valid) this.emit("device_input", packet as DeviceInputPacket);
    } catch {
      // Could be raw legacy string e.g. "PING"
      if (str === "PING") {
        this.emit("device_input", { type: "ping" });
      }
    }
  }

  public sendSync(context: { getDeviceState(): V2DeviceState }) {
    const state = context.getDeviceState();

    const hostKeys = state.keys.map((k) => {
      let flags = 0;
      if (k.isFilled) flags |= KEY_FLAG_FILLED;
      if (k.isEditing) flags |= KEY_FLAG_EDITING;
      if (k.isFocused) flags |= KEY_FLAG_FOCUSED;
      if (k.isDisabled) flags |= KEY_FLAG_DISABLED;
      if (k.isLinesMode) {
        flags |= KEY_FLAG_LINES;
      } else if (k.items && k.items.length > 0) {
        flags |= KEY_FLAG_LIST;
      }

      const obj: any = {
        id: k.keyId,
        top: k.labelTop || "",
        main: k.labelMain || "",
        flags,
      };
      if (k.labelSub) obj.sub = k.labelSub;
      if (k.items && k.items.length > 0) {
        obj.items = k.items;
        obj.activeItem = k.activeItem ?? 0;
      }
      if (k.scrollTotal !== undefined) {
        obj.total = k.scrollTotal;
        obj.activeItem = k.activeItem ?? 0;
      }
      if (k.itemColors && k.itemColors.length > 0) {
        obj.colors = k.itemColors;
      }
      return obj;
    });

    const totalLines = state.topTotalLines || state.topBodyLines.length;
    const windowStart = Math.max(0, Math.min(state.topScrollLine, Math.max(0, totalLines - 1)));
    const windowEnd = Math.min(windowStart + 8, totalLines);
    const visibleLines = state.topBodyLines.slice(windowStart, windowEnd);

    const packet: HostSyncPacket = {
      type: "v2_sync",
      seq: this.syncSeq++,
      viewMode: state.viewMode,
      topTitle: state.topTitle,
      topSubtitle: state.topSubtitle,
      topBody: visibleLines.join("\n"),
      topScroll: state.topScrollLine,
      topTotalLines: totalLines,
      volume: state.volume,
      isMuted: state.isMuted,
      skinId: state.skinId,
      skinName: state.skinName,
      keys: hostKeys,
    };

    const buf = Buffer.from(JSON.stringify(packet), "utf-8");
    if (buf.length > 65507) throw new Error("HUD state exceeds the UDP datagram limit");
    this.socket.send(buf, 0, buf.length, this.targetPort, this.targetHost, (err) => {
      if (err) console.error("[UDP] Device send error:", err);
    });
  }
}
