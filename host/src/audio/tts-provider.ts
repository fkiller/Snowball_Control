export interface TtsSynthesizeOptions {
  voice?: string;
  speed?: number;
  volume?: number;
  language?: string;
}

export interface TtsSynthesizeResult {
  wavPath: string;
  durationMs: number;
  sampleRate: number;
  text: string;
  engine: "kokoro-onnx" | "windows-sapi" | "macos-say" | "mock" | "error";
}

/**
 * Host owns destination & session context; platform workers own native TTS synthesis and playback.
 */
export interface NativeTtsProvider {
  synthesize(text: string, options?: TtsSynthesizeOptions): Promise<TtsSynthesizeResult>;
  stop(): Promise<void>;
  close(): void;
}
