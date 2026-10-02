# MK20 수정 QMK 업데이트

USB host 없이 키 스캔이 멈추는 기존 펌웨어 문제를 해결하려면 **수정 QMK 업데이트가 필수**입니다. 원인·빌드 조건·바이너리 해시·제조사 제공 소스와 배포 범위는 [단일 설계 문서](../../../docs/ARCHITECTURE.md)에 있습니다.

현재 MK20 대상 파일은 `bin/syk_keyboards_mk20_plus_via.bin`입니다. 같은 폴더의 MK10 바이너리는 MK20에 사용하지 마세요. QMK 소스는 제조사에서 직접 이메일로 제공받았으며, 이 파일의 제조사 안내는 `NO_USB_STARTUP_CHECK` 적용을 설명합니다. `source/qmk/mk20_plus/rules.mk`에는 `NO_USB_STARTUP_CHECK = yes`, `NO_SUSPEND_POWER_DOWN = yes`가 설정되어 있습니다.

변경 전 SD 전체 이미지·키맵·복구 자료 백업을 권장합니다. [QMK Toolbox](https://qmk.fm/toolbox)를 설치한 뒤:

1. MK20 USB를 PC에서 분리합니다.
2. 왼쪽 위 첫 키를 누른 상태로 USB를 연결합니다.
3. STM32duino DFU 장치가 인식되면 MK20 대상 파일을 선택합니다.
4. Flash를 실행하고 완료 전 케이블을 분리하지 않습니다.
5. 재연결 후 USB host가 없는 독립 전원에서도 키/UART 입력을 검증합니다.

이번 공개 점검에서는 기기 플래싱과 독립 전원 키 테스트를 수행하지 않았습니다. QMK 기반 코드/펌웨어는 [QMK upstream GPLv2](COPYING)와 원래 고지를 확인해야 하며 Snowball의 Apache 라이선스로 일괄 재표시하지 않습니다.
