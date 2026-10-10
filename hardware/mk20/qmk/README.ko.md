<p align="right">
  <a href="README.md">English</a> | <strong>한국어</strong>
</p>

# MK20 수정 QMK 업데이트

USB host 없이 키 스캔이 멈추는 기존 펌웨어 문제를 해결하려면 **수정 QMK 업데이트가 필수**입니다. 원인·빌드 조건·바이너리 해시·제조사 제공 소스와 배포 범위는 [단일 설계 문서](../../../docs/ARCHITECTURE.ko.md)에 있습니다.

현재 MK20 대상 파일은 `bin/syk_keyboards_mk20_plus_via.bin`입니다. 같은 폴더의 MK10 바이너리는 MK20에 사용하지 마세요. QMK 소스는 제조사에서 직접 이메일로 제공받았으며, 이 파일의 제조사 안내는 `NO_USB_STARTUP_CHECK` 적용을 설명합니다. `source/qmk/mk20_plus/rules.mk`에는 `NO_USB_STARTUP_CHECK = yes`, `NO_SUSPEND_POWER_DOWN = yes`가 설정되어 있습니다.

일반 업데이트는 미들웨어 트레이의 **기기 설정… → MK20**에서 진행합니다. USB를 분리하고 좌측 상단 키를 누른 채 연결하면 DFU를 발견합니다. 실물 기종과 선택한 연결을 확인한 뒤 업데이트를 선택하세요. 변경할 앱·설정 영역 전체를 실제로 읽어 비공개 백업과 SHA-256을 검증해야 쓰기를 시작합니다. 수정 바이너리 밖의 바이트·EEPROM은 보존하고 쓰기 후 전체 읽기 비교, 자동 재기동 요청 및 같은 USB 위치의 키보드 재등장을 확인합니다. 부트로더는 변경하지 않습니다. 드라이버·DFU 읽기 미지원은 쓰기 전에 차단하며 같은 창에서 안내합니다. 일반 경로에는 QMK Toolbox나 등록 스크립트가 필요 없습니다. SD 런타임 준비도 같은 창에서 카드 전체 이미지 백업을 먼저 수행합니다.

이번 공개 점검에서는 기기 플래싱과 독립 전원 키 테스트를 수행하지 않았습니다. QMK 기반 코드/펌웨어는 [QMK upstream GPLv2](https://github.com/qmk/qmk_firmware/blob/master/LICENSE)와 원래 고지를 확인해야 하며 Snowball의 Apache 라이선스로 일괄 재표시하지 않습니다.
