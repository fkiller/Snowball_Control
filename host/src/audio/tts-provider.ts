export interface TtsSynthesizeOptions {
  voice?: string;
  speed?: number;
  volume?: number;
  language?: string;
  destination?: "device" | "host";
}

export interface TtsSynthesizeResult {
  wavPath: string;
  durationMs: number;
  sampleRate: number;
  text: string;
  engine: "supertonic-gpu" | "supertonic-cpu" | "os-native" | "kokoro-onnx" | "windows-sapi" | "macos-say" | "mock" | "error";
}

/**
 * Host owns destination & session context; platform workers own native TTS synthesis and playback.
 */
export interface NativeTtsProvider {
  synthesize(text: string, options?: TtsSynthesizeOptions): Promise<TtsSynthesizeResult>;
  stop(): Promise<void>;
  close(): void;
  setVolume?(volume: number, isMuted?: boolean): void;
}
