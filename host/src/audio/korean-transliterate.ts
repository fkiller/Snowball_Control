/**
 * Korean Transliteration and Text Sanitization for Developer & AI TTS Pipeline
 * 
 * Provides:
 * 1. stripMeaninglessHashes: Filters Git commit hashes, UUIDs, hex memory pointers,
 *    and random tokens that disrupt speech synthesis.
 * 2. transliterateEnglishToHangul: Transliterates developer/tech vocabulary and acronyms
 *    into natural Korean Hangul pronunciations so Korean TTS models (like Supertonic)
 *    produce clean, natural, seamless speech without English pronunciation errors.
 */

// Comprehensive developer, computing, AI, and hardware terminology dictionary
export const TECH_HANGUL_MAP: Record<string, string> = {
  // Brand & Project names
  github: "깃허브",
  git: "깃",
  powershell: "파워셸",
  bash: "배시",
  terminal: "터미널",
  console: "콘솔",
  python: "파이썬",
  typescript: "타입스크립트",
  javascript: "자바스크립트",
  node: "노드",
  "node.js": "노드제이에스",
  codex: "코덱스",
  antigravity: "안티그래비티",
  opencode: "오픈코드",
  snowball: "스노우볼",
  whisper: "위스퍼",
  supertonic: "슈퍼토닉",
  kokoro: "코코로",
  docker: "도커",
  linux: "리눅스",
  windows: "윈도우",
  mac: "맥",
  macos: "맥오에스",
  android: "안드로이드",
  ios: "아이오에스",

  // Hardware & Hardware terminal
  mk20: "엠케이이십",
  pc: "피씨",
  speaker: "스피커",
  microphone: "마이크",
  mic: "마이크",
  display: "디스플레이",
  screen: "스크린",
  knob: "노브",
  volume: "볼륨",
  hardware: "하드웨어",
  software: "소프트웨어",
  device: "디바이스",

  // Computing & Architecture
  api: "에이피아이",
  sdk: "에스디케이",
  cli: "씨엘아이",
  gui: "지유아이",
  ui: "유아이",
  hud: "허드",
  os: "오에스",
  cpu: "씨피유",
  gpu: "지피유",
  npu: "엔피유",
  ram: "램",
  rom: "롬",
  simd: "심디",
  avx: "에이브이에스",
  avx2: "에이브이에스투",
  cuda: "쿠다",
  vulkan: "불칸",
  directml: "다이렉트엠엘",
  onnx: "온닉스",
  json: "제이슨",
  rpc: "알피씨",
  alsa: "알사",
  adb: "에이디비",
  usb: "유에스비",
  uart: "유아트",
  spi: "에스피아이",
  i2c: "아이투씨",
  wav: "웨이브",
  mp3: "엠피쓰리",
  pid: "피아이디",
  ip: "아이피",
  tcp: "티씨피",
  udp: "유디피",
  http: "에이치티티피",
  https: "에이치티티피에스",
  url: "유알엘",
  uri: "유알아이",
  tts: "티티에스",
  stt: "에스티티",
  llm: "엘엘엠",
  ai: "에이아이",
  ml: "엠엘",

  // Git & Workflow
  commit: "커밋",
  branch: "브랜치",
  merge: "머지",
  push: "푸시",
  pull: "풀",
  fetch: "페치",
  checkout: "체크아웃",
  rebase: "리베이스",
  stash: "스태시",
  cherrypick: "체리픽",
  repo: "리포",
  repository: "리포지토리",
  diff: "디프",
  patch: "패치",
  issue: "이슈",
  review: "리뷰",
  pr: "피알",
  tag: "태그",

  // Engineering & Runtime
  build: "빌드",
  compile: "컴파일",
  run: "런",
  test: "테스트",
  tests: "테스트",
  debug: "디버그",
  deploy: "디플로이",
  server: "서버",
  client: "클라이언트",
  daemon: "데몬",
  process: "프로세스",
  thread: "스레드",
  worker: "워커",
  socket: "소켓",
  port: "포트",
  host: "호스트",
  stream: "스트림",
  streaming: "스트리밍",
  pipeline: "파이프라인",
  queue: "큐",
  buffer: "버퍼",
  cache: "캐시",
  memory: "메모리",
  latency: "레이턴시",
  speed: "스피드",
  status: "상태",
  session: "세션",
  workspace: "워크스페이스",
  prompt: "프롬프트",
  model: "모델",
  context: "컨텍스트",
  token: "토큰",
  tokens: "토큰",
  database: "데이터베이스",
  db: "디비",
  config: "컨피그",
  settings: "설정",
  option: "옵션",
  options: "옵션",
  param: "파라미터",
  params: "파라미터",
  parameter: "파라미터",
  parameters: "파라미터",
  rule: "룰",
  task: "태스크",
  script: "스크립트",
  file: "파일",
  files: "파일",
  folder: "폴더",
  directory: "디렉터리",
  path: "패스",
  code: "코드",
  log: "로그",
  logs: "로그",
  agent: "에이전트",
  user: "유저",
  role: "역할",

  // Status & Actions
  ok: "오케이",
  error: "에러",
  warning: "경고",
  warn: "경고",
  info: "정보",
  success: "성공",
  fail: "실패",
  failed: "실패",
  failure: "실패",
  pass: "통과",
  passed: "통과",
  done: "완료",
  ready: "준비",
  start: "시작",
  stop: "정지",
  cancel: "취소",
  discard: "삭제",
  submit: "제출",
  send: "전송",
  talk: "음성",
  speak: "재생",
  reset: "초기화",
  reboot: "재부팅",
  update: "업데이트",
  fix: "수정",
  fixed: "수정 완료",
  bug: "버그",
  timeout: "타임아웃",
  idle: "대기",
  active: "활성",
  online: "온라인",
  offline: "오프라인",
  sync: "동기화",
  async: "비동기",
  true: "참",
  false: "거짓",
  null: "널",
  undefined: "정의되지 않음",
  function: "함수",
  class: "클래스",
  module: "모듈",
  package: "패키지",
  library: "라이브러리",
  plugin: "플러그인",
  version: "버전",
  release: "릴리즈",
};

// Letter-by-letter Korean pronunciation for remaining acronyms
const ENGLISH_LETTER_MAP: Record<string, string> = {
  A: "에이", B: "비", C: "씨", D: "디", E: "이", F: "에프", G: "지",
  H: "에이치", I: "아이", J: "제이", K: "케이", L: "엘", M: "엠", N: "엔",
  O: "오", P: "피", Q: "큐", R: "알", S: "에스", T: "티", U: "유",
  V: "브이", W: "더블유", X: "엑스", Y: "와이", Z: "제트",
};

/**
 * Converts integer numbers (up to 9999) into Sino-Korean Hangul pronunciation.
 */
export function intToKorean(n: number): string {
  if (n === 0) return "영";
  if (n < 0) return "마이너스 " + intToKorean(-n);
  const digits = ["", "일", "이", "삼", "사", "오", "육", "칠", "팔", "구"];
  const units = ["", "십", "백", "천"];
  if (n < 10) return digits[n];
  if (n < 10000) {
    let s = "";
    const str = String(n);
    const len = str.length;
    for (let i = 0; i < len; i++) {
      const d = parseInt(str[i], 10);
      const unit = units[len - 1 - i];
      if (d === 0) continue;
      if (d === 1 && unit) {
        s += unit;
      } else {
        s += digits[d] + unit;
      }
    }
    return s;
  }
  return String(n);
}

/**
 * Attaches Korean postposition (josa / particle) with correct phonetic consonant agreement (e.g. 커밋 + 를 -> 커밋을).
 */
export function attachParticle(noun: string, particle: string): string {
  if (!particle) return noun;
  const lastChar = noun.charCodeAt(noun.length - 1);
  const hasJongseong = lastChar >= 0xac00 && lastChar <= 0xd7a3 && (lastChar - 0xac00) % 28 > 0;

  let normParticle = particle;
  if (particle === "를" || particle === "을") normParticle = hasJongseong ? "을" : "를";
  else if (particle === "가" || particle === "이") normParticle = hasJongseong ? "이" : "가";
  else if (particle === "는" || particle === "은") normParticle = hasJongseong ? "은" : "는";
  else if (particle === "와" || particle === "과") normParticle = hasJongseong ? "과" : "와";
  else if (particle === "로" || particle === "으로") normParticle = hasJongseong ? "으로" : "로";

  return noun + normParticle;
}

/**
 * Strips meaningless hashes and machine-generated IDs from text for clear speech synthesis:
 * - Git commit hashes (7 to 64 hex characters with mixed digits and hex letters, e.g. 854fc69)
 * - UUIDs (e.g. 011fdc9f-299b-4a1e-be4a-7e15a89000cc)
 * - Hexadecimal memory addresses (e.g. 0x7ffee4b2a810)
 * - SHA-256 / SHA-1 hashes (e.g. sha256:abcdef...)
 */
export function stripMeaninglessHashes(text: string): string {
  if (!text) return "";
  let out = text;

  // 1. Strip UUIDs
  out = out.replace(/\b[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}\b/gi, "");

  // 2. Strip Hex memory pointers
  out = out.replace(/\b0x[0-9a-fA-F]+\b/gi, "");

  // 3. Strip sha256 / sha1 prefixes with hashes
  out = out.replace(/\b(sha256|sha1|md5):[0-9a-fA-F]{16,64}\b/gi, "");

  // 4. Clean up "commit <hash>" or "커밋 <hash>" with optional Korean particles
  out = out.replace(/(commit|커밋)\s+[0-9a-fA-F]{7,64}(?:([을를이가은는과와]|에서|으로|로)?)/gi, (_m, head, particle) => {
    const baseNoun = head.toLowerCase() === "commit" ? "commit" : "커밋";
    return baseNoun === "커밋" ? attachParticle(baseNoun, particle) : (particle ? `${baseNoun} ${particle}` : baseNoun);
  });

  // 5. Strip isolated git commit hashes (7-64 hex chars with both letters and numbers)
  // Matching inside or outside brackets/parentheses
  const HASH_REGEX = /(?:\b|[([#])(?=[0-9a-fA-F]{7,64}(?:[)\]]|\b))(?=[0-9a-fA-F]*[0-9])(?=[0-9a-fA-F]*[a-fA-F])[0-9a-fA-F]{7,64}(?:[)\]]|\b)/g;
  out = out.replace(HASH_REGEX, "");

  // 6. Clean empty brackets or parentheses left behind: (), [], ( ), [ ]
  out = out.replace(/\(\s*\)/g, "").replace(/\[\s*\]/g, "");

  // 7. Collapse spaces
  out = out.replace(/\s+/g, " ").trim();
  return out;
}

/**
 * Transliterates English tech terms and acronyms into natural Korean Hangul
 * when the text is in Korean context.
 */
export function transliterateEnglishToHangul(text: string): string {
  if (!text) return "";
  let out = text;

  // 1. Hardware button keys with numbers: e.g. K12, K8, K4, K20 -> 케이십이, 케이팔, 케이사, 케이이십
  out = out.replace(/\bK(20|16|12|8|4|1|2|3|5|6|7|9|10|11|13|14|15)\b/gi, (_m, numStr) => {
    const num = parseInt(numStr, 10);
    return `케이${intToKorean(num)}`;
  });

  // 2. Hardware terminal: MK20, MK-20 -> 엠케이이십
  out = out.replace(/\bMK[-_]?20\b/gi, "엠케이이십");

  // 3. Operating systems: Win11, Win10 -> 윈십일, 윈십
  out = out.replace(/\bWin(11|10)\b/gi, (_m, v) => `윈${intToKorean(parseInt(v, 10))}`);

  // 4. Replace tech terms from dictionary using case-insensitive whole-word boundary
  // Sort keys by length descending to match compound terms first (e.g. "node.js" before "node")
  const sortedTerms = Object.keys(TECH_HANGUL_MAP).sort((a, b) => b.length - a.length);
  for (const term of sortedTerms) {
    const hangul = TECH_HANGUL_MAP[term];
    const escaped = term.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
    const regex = new RegExp(`(?<![a-zA-Z0-9])${escaped}(?![a-zA-Z0-9])`, "gi");
    out = out.replace(regex, hangul);
  }

  // 5. Letter-by-letter expansion for remaining all-caps acronyms (2 to 5 letters, e.g. SDK, RPC, SAPI)
  out = out.replace(/\b([A-Z]{2,5})\b/g, (match) => {
    let result = "";
    for (const ch of match) {
      if (ENGLISH_LETTER_MAP[ch]) {
        result += ENGLISH_LETTER_MAP[ch];
      } else {
        return match;
      }
    }
    return result;
  });

  return out;
}

/**
 * Prepares raw agent response text for Korean TTS synthesis:
 * 1. Strips markdown fences, links, and formatting
 * 2. Strips meaningless commit hashes, UUIDs, hex pointers
 * 3. Transliterates tech terms and acronyms to Hangul if text has Korean
 * 4. Normalizes punctuation so speech pauses naturally
 */
export function cleanTextForSpeech(rawText: string, targetLang?: string): string {
  if (!rawText) return "";
  let t = String(rawText).trim();

  // Strip markdown code fences (```...```)
  t = t.replace(/```[\s\S]*?```/g, " 코드 블록 생략. ");
  // Strip inline code backticks (`code` -> code)
  t = t.replace(/`([^`]+)`/g, "$1");
  // Strip markdown links [text](url) -> text
  t = t.replace(/\[([^\]]+)\]\([^\)]+\)/g, "$1");
  // Strip markdown headers (# Header -> Header)
  t = t.replace(/^#{1,6}\s+/gm, "");
  // Strip blockquotes (> Quote -> Quote)
  t = t.replace(/^>\s+/gm, "");
  // Strip bold/italics (**text** or *text* -> text)
  t = t.replace(/[*_]{1,3}([^*_]+)[*_]{1,3}/g, "$1");
  // Strip bullet markers (- item -> item, * item -> item)
  t = t.replace(/^[\s]*[-*+]\s+/gm, "");
  // Strip numbered lists (1. item -> item)
  t = t.replace(/^[\s]*\d+\.\s+/gm, "");
  // Strip horizontal rules (---, ***, ___)
  t = t.replace(/^[-\*_]{3,}\s*$/gm, "");

  // 1. Filter hashes
  t = stripMeaninglessHashes(t);

  // 2. Check if Korean is present or targetLang is 'ko'
  const hasKorean = /[\uac00-\ud7af\u1100-\u11ff\u3130-\u318f]/.test(t);
  if (hasKorean || targetLang === "ko") {
    t = transliterateEnglishToHangul(t);
  }

  // 3. Normalize line breaks and end punctuation for natural TTS cadence
  t = t
    .split("\n")
    .map((line) => line.trim())
    .filter(Boolean)
    .map((line) => {
      if (/[.!?:,;~]$/.test(line)) return line;
      return line + ".";
    })
    .join(" ");

  // 4. Collapse whitespace
  t = t.replace(/\s+/g, " ").trim();
  return t;
}
