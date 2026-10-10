<p align="center">
  <img src="../Drift_icon.png" alt="Drift 아이콘" width="128" height="128">
</p>

<h1 align="center">Drift</h1>

<p align="center">
  <strong>단순히 “적당한” 수준이 아닌 완성도 높은 비디오를 만들어주는 무료 데스크톱 동영상 편집기.</strong>
</p>

<p align="center">
  <a href="https://github.com/CutWire-Studios/Drift/releases/latest"><img src="https://img.shields.io/github/v/release/CutWire-Studios/Drift?label=release&style=flat&labelColor=28272f&color=feb504" alt="최신 릴리스" height="24"></a>
  <a href="https://github.com/CutWire-Studios/Drift/releases"><img src="https://img.shields.io/github/downloads/CutWire-Studios/Drift/total?label=GitHub%20downloads&style=flat&labelColor=28272f&color=feb504" alt="GitHub 다운로드 수" height="24"></a>
  <a href="https://flathub.org/apps/org.cutwire.Drift"><img src="https://img.shields.io/flathub/downloads/org.cutwire.Drift?label=Flathub%20installs&style=flat&labelColor=28272f&color=feb504" alt="Flathub 설치 수" height="24"></a>
  <a href="https://discord.gg/J5ANFz6Z3y"><img src="https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Fdiscord.com%2Fapi%2Finvites%2FJ5ANFz6Z3y%3Fwith_counts%3Dtrue&query=%24.approximate_member_count&suffix=%20members&logo=discord&logoColor=F8F8F8&label=Discord&color=feb504&labelColor=28272f&style=flat" alt="Drift Discord 참가하기" height="24"></a>
  <a href="../LICENSE"><img src="https://img.shields.io/github/license/CutWire-Studios/Drift?style=flat&labelColor=28272f&color=feb504" alt="라이선스: GPL-3.0" height="24"></a>
  <img src="https://img.shields.io/badge/platform-Linux%20%7C%20Windows%20%7C%20macOS%20%7C%20Android-feb504?style=flat&labelColor=28272f" alt="플랫폼: Linux | Windows | macOS | Android" height="24">
</p>

<p align="center">
  <a href="https://cutwire.org/drift"><strong>cutwire.org/drift</strong></a>
</p>

Drift는 CutWire Studios에서 제작한 데스크톱용 동영상 편집기입니다. 클립을 끌어다 놓고, 효과, 자막, 스티커,
음악을 추가한 뒤 세련된 비디오로 내보내세요 — **구독료 없음, 워터마크 없음, 계정 가입 필요 없음**.

사람들이 실제로 제작하는 영상에 맞춰 설계되었습니다: 릴스(Reels)와 쇼츠(Shorts), 게임 하이라이트, 학교 과제,
튜토리얼, 제품 시연, 밈 영상 등 웹 브라우저의 제약에 갇히거나 매달 요금을 결제하지 않고도 전문적인 결과물을 만들 수 있습니다.

미리보기 창에서 보이는 그대로 출력됩니다. 단일 컴포지터로 일관된 비주얼을 제공하며 어떠한 왜곡도 없습니다.

## 다운로드

<p align="center">
  <a href="https://flathub.org/apps/org.cutwire.Drift">
    <img src="https://flathub.org/api/badge?locale=ko" alt="Flathub에서 다운로드" height="80">
  </a>
  <a href="https://apps.microsoft.com/detail/9PHHBZ07FRSZ">
    <img src="https://get.microsoft.com/images/en-us%20dark.svg" alt="Microsoft Store에서 다운로드" width="293">
  </a>
  <a href="../docs/play-testing.md">
    <img src="../docs/btn-gplay-en.png" alt="Google Play에서 다운로드 (비공개 테스트)" height="80">
  </a>
</p>

<p align="center">Google Play는 현재 비공개 테스트 중입니다 — <a href="../docs/play-testing.md">참여 방법 확인</a>.</p>

**Linux** — Flathub에서 설치:

```bash
flatpak install flathub org.cutwire.Drift
flatpak run org.cutwire.Drift
```
**macOS** — Homebrew로 설치:

```bash
brew install --cask cutwire-studios/tap/drift
```

> **macOS 최초 실행 시 참고:** Drift는 ad-hoc 서명되어 있습니다 (Apple 공증 미포함). Homebrew로 설치 시 격리(quarantine) 속성이 자동으로 해제됩니다. `.dmg` 파일로 직접 설치했거나 Gatekeeper가 실행을 차단하는 경우 터미널에서 다음을 실행하세요:
> ```bash
> xattr -dr com.apple.quarantine /Applications/Drift.app
> ```

또는 [최신 릴리스 페이지](https://github.com/CutWire-Studios/Drift/releases/latest)에서 운영체제별 빌드를 직접 다운로드할 수 있습니다:

| 플랫폼 | 패키지 |
|--------|--------|
| Linux | [Flathub](https://flathub.org/apps/org.cutwire.Drift) · [AppImage](https://github.com/CutWire-Studios/Drift/releases/latest) |
| Windows | [Microsoft Store](https://apps.microsoft.com/detail/9PHHBZ07FRSZ) · [설치 프로그램 (.exe)](https://github.com/CutWire-Studios/Drift/releases/latest) · [포터블 zip](https://github.com/CutWire-Studios/Drift/releases/latest) |
| macOS | [Homebrew Tap](https://github.com/CutWire-Studios/homebrew-tap) · [디스크 이미지 (.dmg, Apple Silicon)](https://github.com/CutWire-Studios/Drift/releases/latest) |
| Android | [Google Play (비공개 테스트)](../docs/play-testing.md) · [APK (arm64-v8a)](https://github.com/CutWire-Studios/Drift/releases/latest) · [APK (armeabi-v7a)](https://github.com/CutWire-Studios/Drift/releases/latest) · [APK (x86_64)](https://github.com/CutWire-Studios/Drift/releases/latest) |

스마트폰의 경우 [Google Play 비공개 테스트](../docs/play-testing.md)에 참여하거나 최신 릴리스에서 `Drift-*-arm64-v8a.apk` 파일을 받아 설치할 수 있습니다 (또는 `adb install Drift-*-arm64-v8a.apk`). 에뮬레이터 환경에서는 `x86_64` 빌드를 사용하세요.

이전 버전 및 상세 변경 사항은 [전체 릴리스 목록](https://github.com/CutWire-Studios/Drift/releases)에서 확인할 수 있습니다.

## 스크린샷

<p align="center">
  <img src="../docs/screenshots/main-window.png" alt="Drift 편집기: 좌측 미디어 보관함, 미리보기 내 3D 카메라, 우측 텍스트 인스펙터, 하단 멀티트랙 타임라인" width="900">
</p>

## 주요 기능

**AI 에이전트가 열려 있는 프로젝트를 직접 편집.** 열려 있는 프로젝트에서 클립 가져오기, 컷 편집, 컬러 그레이딩, 내보내기를 지시할 수 있습니다. 헤드리스(무화면) 모드로도 완벽히 작동합니다.

**타임라인 내 3D 공간 지원.** 클립을 입체 공간에서 기울이고, 조명을 비추며, 피사체 뒤에 타이틀을 배치하세요. 3D 모델을 타임라인에 바로 끌어다 놓아 함께 렌더링할 수 있습니다.

**Lottie 벡터 애니메이션 지원.** 모션 그래픽과 벡터 아트가 어떤 해상도에서도 선명함을 유지합니다. 타임라인 상에서 색상이나 문구를 즉시 수정하세요.

**고급 키프레임 시스템.** 위치, 크기, 회전, 불투명도 및 다양한 이펙트 파라미터를 시간에 따라 자유롭게 제어할 수 있습니다. 보간 곡선을 직접 그려보세요.

**150개 이상의 트랜지션 및 40개 이상의 이펙트.** 모든 효과를 실제 영상 위에서 실시간으로 미리 확인할 수 있습니다. 비트 드롭, 글리치 컷, 네온 컷아웃 등 여러 효과 스택을 한 번의 클릭으로 일괄 적용하세요.

**로컬 PC 기반 처리.** 피사체를 클릭 한 번으로 배경과 분리할 수 있습니다. 피사계 심도와 전용 조명을 추가하세요. 자동 자막 생성, 비디오 업스케일링, 실시간 얼굴 보정까지 모두 내 컴퓨터에서 안전하게 처리됩니다.

**텍스트 기반 영상 편집.** 음성이 클립 위에서 텍스트로 자동 변환됩니다. 불필요한 문장, 추임새, 무음 구간을 손쉽게 잘라내고 최적의 테이크를 선택하며 발화자 태그를 지정해 즉각 자막을 생성하세요.

**트렌디한 비주얼 타이틀.** 노래방 스타일, Hormozi 스타일, 네온, 크롬, 홀로그램, 손글씨 등 33가지 타이틀 팩을 제공합니다. 트랙 내 모든 자막에 동일 스타일을 클릭 한 번으로 일괄 적용할 수 있습니다.

**스택 전체 일괄 제어.** 단 하나의 변형 레이어로 하위의 모든 트랙을 동시에 이동, 크기 조절, 회전, 기울임 및 페이드 처리할 수 있습니다. 별도의 타임라인이 필요한 경우 레이어를 중첩하거나 컴포지트로 변환하세요.

**음악 비트 맞춤 편집.** 타임라인이 음악 비트에 자동으로 스냅됩니다. 클립이 박자에 맞춰 정밀하게 나뉩니다. 음성이 나올 때 음악 볼륨이 자연스럽게 줄어들고(Audio Ducking) 끝나면 복구됩니다. 얼굴을 추적하여 가로 영상을 세로형 쇼츠로 자동 리프레임할 수도 있습니다.

**안정적이고 직관적인 타임라인.** 멀티트랙 필름스트립, 리플(Ripple) 편집, 스냅, 마스크, 프레임 고정(Freeze Frame), 북마크, 내장 오디오 믹서를 지원합니다. 클립을 분할해도 컬러 그레이딩은 유지되며, 비정상 종료 시에도 마지막 저장본이 온전하게 보호됩니다.

**완성도 높은 전문 오디오.** 음성 노이즈 제거, 라우드니스 측정, EQ 및 컴프레서, 배속 변경 시 피치 유지, 보이스오버(내레이션) 직접 녹음을 지원합니다.

**샷 검색 및 멀티캠 편집.** 화면에 나타나는 내용을 기반으로 푸티지를 검색하세요. 모든 카메라 앵글을 동시에 모니터링하며 원하는 타이밍에 정확하게 컷을 전환할 수 있습니다.

**손떨림 보정, 내보내기, 프로젝트 패키징.** 흔들리는 클립을 부드럽게 보정하고, 해상도를 높이며, 고용량 파일도 매끄럽게 재생합니다. MP4, GIF, 오디오 전용 및 지정 구간 내보내기를 지원합니다. 미디어 파일을 프로젝트와 함께 패키징하여 파일 경로가 끊어지지 않도록 관리하세요.

**Android에서도 동일한 편집 기능.** 스마트폰에서도 타임라인, 이펙트, 내보내기를 동일하게 사용할 수 있습니다. 완성된 영상을 앱에서 바로 공유하세요.

**스톡 라이브러리, 음성 합성, 가벼운 용량.** 미디어 보관함에서 스톡 비디오, 이미지, 음원을 직접 검색하세요. 보이스오버와 음향 효과를 즉각 생성할 수 있습니다. 폰트, 스티커 및 추가 모델은 필요 시 다운로드됩니다. UI는 아랍어, 벵골어, 스페인어(스페인/콜롬비아), 프랑스어(캐나다), 이탈리아어, 일본어, 포르투갈어(브라질/포르투갈), 러시아어, 싱할라어, 타갈로그어, 베트남어, 중국어 간체를 완벽히 지원합니다.

## Drift를 선택하는 이유

시중의 많은 “무료” 편집기는 영상 결과물이 좋아질 때쯤 계정 가입, 워터마크 또는 유료 구독을 강요합니다. Drift는 정반대입니다: **사용자의 컴퓨터에서 구동되며, GPLv3 라이선스를 따르며, 로그인 절차가 전혀 없습니다.**

30초짜리 SNS 숏폼 제작에 충분히 빠르고, 타임라인 AI 에이전트, 3D 및 Lottie, 자동 자막, 다양한 이펙트, 스튜디오 오디오, 스마트 누끼 분리, 멀티캠 등 전문 프로젝트에도 충분히 강력합니다.

## Drift 번역 참여하기

[![번역 상태](https://hosted.weblate.org/widget/cutwire-drift/drift-desktop/multi-auto.svg)](https://hosted.weblate.org/engage/cutwire-drift/)

## 개발자 정보

빌드, 패키징, 아키텍처 및 에이전트 프로토콜 문서는 `docs/` 디렉터리에 마련되어 있습니다:

- [빌드, 테스트, 패키징 및 아키텍처 안내](../docs/BUILDING.md)
- [GPU 이펙트](../docs/gpu-effects.md)
- [GPU 트랜지션](../docs/gpu-transitions.md)
- [에이전트 연결 / MCP](../docs/MCP.md)

## 지원 및 피드백

버그를 발견하셨거나 새로운 아이디어가 있으신가요? [GitHub Issue](https://github.com/CutWire-Studios/Drift/issues)를 등록해 주세요.

## 기여하기

[CONTRIBUTING.md](../CONTRIBUTING.md)에서 풀 리퀘스트 가이드를 확인하실 수 있습니다: 대규모 변경 전 Issue를 열고, 각 요청을 단일 변경 사항으로 유지하며, 테스트를 실행하고, GPLv3 라이선스로 배포해 주세요.

이펙트, 트랜지션, 템플릿, 오디오 필터는 애드온으로 취급됩니다. 해당 항목은 [Drift-Addons](https://github.com/CutWire-Studios/Drift-Addons)에 기여해 주세요. 이를 구동하는 코어 엔진 변경 사항은 본 저장소에서 관리됩니다.

[공식 릴리스](https://github.com/CutWire-Studios/Drift/releases)를 설치하고 실제 프로젝트를 편집해 보며 문제를 보고하는 것만으로도 큰 도움이 됩니다. [Discord](https://discord.gg/J5ANFz6Z3y)에서 기여자는 `@Contributor` 역할을 요청할 수 있습니다.

## 기여자

<a href="https://github.com/CutWire-Studios/Drift/graphs/contributors">
  <img alt="기여자" src="https://contrib.rocks/image?repo=CutWire-Studios/Drift">
</a>

## 라이선스

GPLv3 — [LICENSE](../LICENSE) 파일을 참고하세요.

## 스타 히스토리

<a href="https://www.star-history.com/?repos=CutWire-Studios%2FDrift&type=date&legend=top-left">
 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/chart?repos=CutWire-Studios/Drift&type=date&legend=top-left&sealed_token=zHW_d2jon9Wn-HYP2SWQWLC7qDRaY7qwsvHS0Cp0Ywk1Rf1UvyxhWsakIrx2c11OijPJQ9o52W99jdigV7MOz5RuvLsyWQmBiMvMdk99mcfbgb591WtzNXQO8_K2YhgdbiPD9by00lwl69ZgCZnThFKBwhRbK7IQzIeFkIFnb2o0r5GhJh0HAX6Q8yTM" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/chart?repos=CutWire-Studios/Drift&type=date&legend=top-left&sealed_token=zHW_d2jon9Wn-HYP2SWQWLC7qDRaY7qwsvHS0Cp0Ywk1Rf1UvyxhWsakIrx2c11OijPJQ9o52W99jdigV7MOz5RuvLsyWQmBiMvMdk99mcfbgb591WtzNXQO8_K2YhgdbiPD9by00lwl69ZgCZnThFKBwhRbK7IQzIeFkIFnb2o0r5GhJh0HAX6Q8yTM" />
   <img alt="Star History 차트" src="https://api.star-history.com/chart?repos=CutWire-Studios/Drift&type=date&legend=top-left&sealed_token=zHW_d2jon9Wn-HYP2SWQWLC7qDRaY7qwsvHS0Cp0Ywk1Rf1UvyxhWsakIrx2c11OijPJQ9o52W99jdigV7MOz5RuvLsyWQmBiMvMdk99mcfbgb591WtzNXQO8_K2YhgdbiPD9by00lwl69ZgCZnThFKBwhRbK7IQzIeFkIFnb2o0r5GhJh0HAX6Q8yTM" />
 </picture>
</a>
