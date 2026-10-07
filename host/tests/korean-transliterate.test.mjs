import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  intToKorean,
  stripMeaninglessHashes,
  transliterateEnglishToHangul,
  cleanTextForSpeech,
} from '../dist/audio/korean-transliterate.js';

test('intToKorean: accurately converts numbers to Sino-Korean', () => {
  assert.equal(intToKorean(0), '영');
  assert.equal(intToKorean(1), '일');
  assert.equal(intToKorean(4), '사');
  assert.equal(intToKorean(8), '팔');
  assert.equal(intToKorean(12), '십이');
  assert.equal(intToKorean(16), '십육');
  assert.equal(intToKorean(20), '이십');
  assert.equal(intToKorean(100), '백');
  assert.equal(intToKorean(2026), '이천이십육');
});

test('stripMeaninglessHashes: strips git commit hashes, UUIDs, and hex pointers', () => {
  // Commit hashes
  assert.equal(
    stripMeaninglessHashes('커밋 854fc69를 머지했습니다'),
    '커밋을 머지했습니다'
  );
  assert.equal(
    stripMeaninglessHashes('commit a1b2c3d4e5f6 updated'),
    'commit updated'
  );
  assert.equal(
    stripMeaninglessHashes('해시 (854fc69) 및 [a1b2c3d4e5f6] 확인'),
    '해시 및 확인'
  );

  // UUIDs
  assert.equal(
    stripMeaninglessHashes('세션 011fdc9f-299b-4a1e-be4a-7e15a89000cc 활성화'),
    '세션 활성화'
  );

  // Hex memory addresses
  assert.equal(
    stripMeaninglessHashes('메모리 0x7ffee4b2a810 확인'),
    '메모리 확인'
  );

  // Does NOT strip normal numbers or non-hash words
  assert.equal(
    stripMeaninglessHashes('1234567개의 파일과 beef 고기'),
    '1234567개의 파일과 beef 고기'
  );
});

test('transliterateEnglishToHangul: transliterates developer vocabulary and hardware acronyms', () => {
  const input = 'Github 저장소 정리와 커밋.. Powershell 셸 문법과 MK20 장비 및 PC 스피커';
  const expected = '깃허브 저장소 정리와 커밋.. 파워셸 셸 문법과 엠케이이십 장비 및 피씨 스피커';
  assert.equal(transliterateEnglishToHangul(input), expected);

  // Hardware key references
  assert.equal(
    transliterateEnglishToHangul('K12 버튼과 K8 버튼 및 K4 버튼'),
    '케이십이 버튼과 케이팔 버튼 및 케이사 버튼'
  );

  // Tech acronyms
  assert.equal(
    transliterateEnglishToHangul('CPU 및 GPU 가속, DirectML 과 CUDA'),
    '씨피유 및 지피유 가속, 다이렉트엠엘 과 쿠다'
  );
});

test('cleanTextForSpeech: integrates markdown cleaning, hash stripping, and transliteration', () => {
  const raw = `
### 작업 완료 보고
- **Github** 저장소 정리와 커밋 854fc69 완료.
- [ARCHITECTURE.md](file:///docs/ARCHITECTURE.md) 문서 업데이트.
- MK20 기기의 K12(Speak)와 K8(Speak on PC) 지원 추가.
`;
  const cleaned = cleanTextForSpeech(raw);

  // Markdown headers, bold, bullet points, links removed
  assert.ok(!cleaned.includes('###'));
  assert.ok(!cleaned.includes('**'));
  assert.ok(!cleaned.includes('-'));
  assert.ok(!cleaned.includes('file:///'));
  assert.ok(!cleaned.includes('854fc69')); // Hash stripped

  // Transliteration applied
  assert.ok(cleaned.includes('깃허브'));
  assert.ok(cleaned.includes('엠케이이십'));
  assert.ok(cleaned.includes('케이십이'));
  assert.ok(cleaned.includes('케이팔'));
  assert.ok(cleaned.includes('피씨'));
});
