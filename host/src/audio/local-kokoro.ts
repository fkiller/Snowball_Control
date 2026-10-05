/**
 * Backward compatibility alias for LocalSupertonicProvider.
 * Migrated from Kokoro to Supertonic-3 TTS.
 */
export {
  LocalSupertonicProvider,
  LocalSupertonicProvider as LocalKokoroProvider,
  splitIntoSentences,
} from "./local-supertonic.js";
