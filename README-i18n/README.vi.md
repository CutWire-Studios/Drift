<p align="center">
  <img src="../Drift_icon.png" alt="Biểu tượng Drift" width="128" height="128">
</p>

<h1 align="center">Drift</h1>

<p align="center">
  <strong>Trình chỉnh sửa video miễn phí cho máy tính giúp video của bạn trông chỉn chu và chuyên nghiệp — không chỉ dừng lại ở mức “tàm tạm”.</strong>
</p>

<p align="center">
  <a href="https://github.com/CutWire-Studios/Drift/releases/latest"><img src="https://img.shields.io/github/v/release/CutWire-Studios/Drift?label=release&style=flat&labelColor=28272f&color=feb504" alt="Bản phát hành mới nhất" height="24"></a>
  <a href="https://github.com/CutWire-Studios/Drift/releases"><img src="https://img.shields.io/github/downloads/CutWire-Studios/Drift/total?label=GitHub%20downloads&style=flat&labelColor=28272f&color=feb504" alt="Lượt tải về trên GitHub" height="24"></a>
  <a href="https://flathub.org/apps/org.cutwire.Drift"><img src="https://img.shields.io/flathub/downloads/org.cutwire.Drift?label=Flathub%20installs&style=flat&labelColor=28272f&color=feb504" alt="Lượt cài đặt Flathub" height="24"></a>
  <a href="https://discord.gg/J5ANFz6Z3y"><img src="https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Fdiscord.com%2Fapi%2Finvites%2FJ5ANFz6Z3y%3Fwith_counts%3Dtrue&query=%24.approximate_member_count&suffix=%20members&logo=discord&logoColor=F8F8F8&label=Discord&color=feb504&labelColor=28272f&style=flat" alt="Tham gia Discord Drift" height="24"></a>
  <a href="../LICENSE"><img src="https://img.shields.io/github/license/CutWire-Studios/Drift?style=flat&labelColor=28272f&color=feb504" alt="Giấy phép: GPL-3.0" height="24"></a>
  <img src="https://img.shields.io/badge/platform-Linux%20%7C%20Windows%20%7C%20macOS%20%7C%20Android-feb504?style=flat&labelColor=28272f" alt="Nền tảng: Linux | Windows | macOS | Android" height="24">
</p>

<p align="center">
  <a href="https://cutwire.org/drift"><strong>cutwire.org/drift</strong></a>
</p>

Drift là phần mềm biên tập video trên máy tính để bàn của CutWire Studios. Thêm clip, áp dụng hiệu ứng, phụ đề, nhãn dán
và âm nhạc, sau đó xuất ra video sắc nét — **hoàn toàn không có phí thuê bao, không hình mờ (watermark) và không bắt buộc đăng ký tài khoản**.

Phần mềm được xây dựng cho những dạng video mà người sáng tạo thực sự cần: Reels và Shorts, clip chơi game, bài tập học tập,
hướng dẫn (tutorial), giới thiệu sản phẩm, meme và bất kỳ nội dung nào bạn muốn có chất lượng cao mà không bị gò bó trong trình duyệt
hay phải trả phí hàng tháng.

Những gì bạn thấy trên khung xem trước chính là những gì bạn xuất ra. Một bộ xử lý hình ảnh duy nhất, chất lượng đồng nhất, không có sai lệch bất ngờ.

## Tải về

<p align="center">
  <a href="https://flathub.org/apps/org.cutwire.Drift">
    <img src="https://flathub.org/api/badge?locale=vi" alt="Tải từ Flathub" height="80">
  </a>
  <a href="https://apps.microsoft.com/detail/9PHHBZ07FRSZ">
    <img src="https://get.microsoft.com/images/en-us%20dark.svg" alt="Tải từ Microsoft Store" width="293">
  </a>
  <a href="../docs/play-testing.md">
    <img src="../docs/btn-gplay-en.png" alt="Tải trên Google Play (thử nghiệm đóng)" height="80">
  </a>
</p>

<p align="center">Google Play đang trong giai đoạn thử nghiệm đóng — <a href="../docs/play-testing.md">hướng dẫn tham gia</a>.</p>

**Linux** — cài đặt qua Flathub:

```bash
flatpak install flathub org.cutwire.Drift
flatpak run org.cutwire.Drift
```
**macOS** — cài đặt qua Homebrew:

```bash
brew install --cask cutwire-studios/tap/drift
```

> **Khởi chạy lần đầu trên macOS:** Drift được ký dạng ad-hoc (chưa được Apple công chứng notary). Thuộc tính cách ly (quarantine) sẽ được gỡ bỏ tự động khi cài qua Homebrew. Nếu bạn cài đặt thủ công từ file `.dmg` hoặc bị Gatekeeper chặn, hãy chạy lệnh:
> ```bash
> xattr -dr com.apple.quarantine /Applications/Drift.app
> ```

Hoặc tải bản đóng gói trực tiếp cho nền tảng của bạn từ
[bản phát hành mới nhất](https://github.com/CutWire-Studios/Drift/releases/latest):

| Nền tảng | Gói cài đặt |
|----------|-------------|
| Linux | [Flathub](https://flathub.org/apps/org.cutwire.Drift) · [AppImage](https://github.com/CutWire-Studios/Drift/releases/latest) |
| Windows | [Microsoft Store](https://apps.microsoft.com/detail/9PHHBZ07FRSZ) · [Trình cài đặt (.exe)](https://github.com/CutWire-Studios/Drift/releases/latest) · [Bản Portable zip](https://github.com/CutWire-Studios/Drift/releases/latest) |
| macOS | [Homebrew Tap](https://github.com/CutWire-Studios/homebrew-tap) · [Ảnh đĩa (.dmg, Apple Silicon)](https://github.com/CutWire-Studios/Drift/releases/latest) |
| Android | [Google Play (thử nghiệm đóng)](../docs/play-testing.md) · [APK (arm64-v8a)](https://github.com/CutWire-Studios/Drift/releases/latest) · [APK (armeabi-v7a)](https://github.com/CutWire-Studios/Drift/releases/latest) · [APK (x86_64)](https://github.com/CutWire-Studios/Drift/releases/latest) |

Trên điện thoại, hãy tham gia [chương trình thử nghiệm đóng Google Play](../docs/play-testing.md), hoặc tải file `Drift-*-arm64-v8a.apk` từ bản phát hành mới nhất để cài đặt (hoặc dùng `adb install Drift-*-arm64-v8a.apk`). Dùng bản `x86_64` cho trình giả lập.

Xem [tất cả bản phát hành](https://github.com/CutWire-Studios/Drift/releases) để xem lại các phiên bản trước đó và nhật ký thay đổi đầy đủ.

## Ảnh chụp màn hình

<p align="center">
  <img src="../docs/screenshots/main-window.png" alt="Giao diện Drift: ngăn phương tiện bên trái, camera 3D trong màn hình xem trước, bảng điều khiển văn bản bên phải, dòng thời gian đa track ở phía dưới" width="900">
</p>

## Tính năng

**Đại lý AI (Agent) có thể chỉnh sửa trực tiếp dự án đang mở.** Nhập dữ liệu, cắt, chỉnh màu và xuất video ngay trong dự án đang thao tác. Trình chỉnh sửa còn có thể chạy ở chế độ không cửa sổ (headless).

**Không gian 3D trên dòng thời gian.** Nghiêng clip trong không gian 3 chiều, thiết lập ánh sáng và đặt tiêu đề phía sau chủ thể. Kéo thả mô hình 3D vào và phát ngay trong cùng cảnh cắt.

**Hỗ trợ hoạt ảnh Lottie.** Đồ họa chuyển động và hình học vector giữ nguyên độ sắc nét ở mọi kích thước. Thay đổi màu sắc hoặc sửa từ ngữ trực tiếp tại chỗ.

**Hệ thống Keyframe nâng cao.** Tạo chuyển động cho vị trí, kích thước, xoay, độ mờ đục và các tham số hiệu ứng theo thời gian. Tự do tùy biến đường cong hoạt họa.

**Hơn 150 hiệu ứng chuyển cảnh và hơn 40 hiệu ứng hình ảnh.** Xem trước mọi hiệu ứng ngay trên khung hình thực tế. Chỉ một cú nhấp chuột để áp dụng cả bộ hiệu ứng — Beat Drop, Glitch Cut, Neon Cutout. Vệt khung hình trước và thiết lập màu được gắn chặt với từng clip.

**Xử lý cục bộ 100% trên máy tính.** Nhấp vào một đối tượng để tự động tách chủ thể khỏi hậu cảnh. Thêm độ sâu trường ảnh và ánh sáng thuộc về clip. Tự động tạo phụ đề, nâng cao độ phân giải (upscale) và làm đẹp khuôn mặt trực tiếp — tất cả ngay trên máy tính của bạn.

**Biên tập video qua văn bản.** Giọng nói được chuyển thành văn bản ngay trên clip. Cắt bỏ các câu từ thừa, loại bỏ từ đệm, xóa khoảng lặng, chọn các cảnh quay ưng ý nhất, gắn nhãn người nói và tự động tạo phụ đề chuẩn xác.

**Tiêu đề và chữ nghệ thuật bắt mắt.** 33 bộ phong cách phong phú: karaoke, phong cách Hormozi, neon, chrome, holographic, chữ viết tay. Sao chép một kiểu dáng cho toàn bộ phụ đề trên cùng một track chỉ bằng một cú nhấp.

**Di chuyển đồng loạt cả ngăn layer.** Một lớp biến đổi có thể kéo, phóng to thu nhỏ, xoay, nghiêng và làm mờ mọi thứ bên dưới nó. Nhóm layer lồng nhau hoặc mở bảng tổng hợp (composite) khi một khối cần dòng thời gian riêng.

**Cắt ghép theo nhịp điệu âm nhạc.** Dòng thời gian tự động bắt dính vào nhịp beat. Các đoạn clip được phân tách chuẩn xác theo phách nhạc. Âm nhạc tự động nhỏ dần dưới lời thoại (audio ducking) rồi trở lại mức âm ban đầu. Tự động đổi khung hình video ngang thành video dọc bám theo khuôn mặt.

**Dòng thời gian mượt mà và trực quan.** Dải cuộn phim đa track, chế độ cuộn đẩy (ripple), bắt dính (snap), mặt nạ (mask), đóng băng khung hình (freeze frame), dấu trang, bộ trộn âm thanh (mixer). Cắt clip mà không làm mất màu sắc đã chỉnh. Trường hợp phần mềm tắt đột ngột, bản lưu cuối cùng vẫn luôn nguyên vẹn.

**Âm thanh hoàn thiện chuyên nghiệp.** Lọc tạp âm giọng nói, đo âm lượng (loudness), cân bằng EQ và nén âm, giữ nguyên cao độ khi thay đổi tốc độ phát, thu âm giọng đọc thuyết minh trực tiếp.

**Tìm kiếm cảnh quay. Chuyển đổi đa góc máy (multicam).** Tìm kiếm các đoạn video dựa trên những gì xuất hiện trên màn hình. Quan sát tất cả góc máy cùng lúc và chuyển cảnh tức thì.

**Chống rung, xuất video và đóng gói dự án.** Giảm rung lắc video, nâng cấp chất lượng hình ảnh, phát mượt mà các file nặng. Xuất định dạng MP4, GIF, chỉ âm thanh hoặc xuất theo vùng chọn. Đóng gói phương tiện kèm dự án để giữ nguyên đường dẫn file.

**Android đồng bộ tính năng chỉnh sửa.** Đầy đủ dòng thời gian, hiệu ứng và xuất video ngay trên điện thoại di động. Chia sẻ trực tiếp từ ứng dụng.

**Kho tư liệu stock, tạo giọng nói và dung lượng gọn nhẹ.** Tìm kiếm ảnh, video và âm thanh stock ngay vào ngăn phương tiện. Tạo giọng đọc AI và hiệu ứng âm thanh. Phông chữ, nhãn dán và các mô hình mở rộng tải về khi cần dùng. Giao diện hỗ trợ tiếng Ả Rập, tiếng Bengali, tiếng Tây Ban Nha (Tây Ban Nha và Colombia), tiếng Pháp (Canada), tiếng Ý, tiếng Nhật, tiếng Bồ Đào Nha (Brazil và Bồ Đào Nha), tiếng Nga, tiếng Sinhala, tiếng Tagalog, tiếng Việt và tiếng Trung giản thể.

## Vì sao mọi người lựa chọn Drift

Hầu hết các trình biên tập “miễn phí” trên thị trường đều đòi hỏi đăng nhập, chèn watermark hoặc bắt mua gói thuê bao ngay khi video của bạn bắt đầu đẹp mắt. Drift thì ngược lại: **thuộc về bạn, chạy trên máy bạn, giấy phép mã nguồn mở GPLv3, không có rào cản đăng nhập.**

Tốc độ cực nhanh cho một video ngắn 30 giây trên mạng xã hội, đồng thời đủ mạnh mẽ cho một dự án phim chuyên nghiệp — AI Agent trên dòng thời gian, 3D & Lottie, tạo phụ đề, kỹ xảo, âm thanh phòng thu, bóc tách vật thể và dựng đa camera.

## Chung tay dịch thuật Drift

[![Trạng thái dịch thuật](https://hosted.weblate.org/widget/cutwire-drift/drift-desktop/multi-auto.svg)](https://hosted.weblate.org/engage/cutwire-drift/)

## Dành cho lập trình viên

Hướng dẫn biên dịch, đóng gói, kiến trúc và giao thức agent được đặt trong thư mục `docs/`:

- [Biên dịch, kiểm thử, đóng gói và kiến trúc](../docs/BUILDING.md)
- [Hiệu ứng GPU](../docs/gpu-effects.md)
- [Chuyển cảnh GPU](../docs/gpu-transitions.md)
- [Kết nối Agent / Giao thức MCP](../docs/MCP.md)

## Hỗ trợ và Đóng góp ý kiến

Bạn phát hiện lỗi hoặc có đề xuất tính năng mới? Hãy tạo
[issue trên GitHub](https://github.com/CutWire-Studios/Drift/issues).

## Đóng góp mã nguồn

[CONTRIBUTING.md](../CONTRIBUTING.md) là tài liệu hướng dẫn tạo pull request: vui lòng mở thảo luận trước khi thực hiện thay đổi lớn, mỗi pull request chỉ tập trung một nội dung, chạy kiểm thử đầy đủ và cấp phép mã nguồn theo GPLv3.

Hiệu ứng, chuyển cảnh, mẫu dựng sẵn và bộ lọc âm thanh là các tiện ích bổ sung (addons). Hãy gửi những nội dung đó đến [Drift-Addons](https://github.com/CutWire-Studios/Drift-Addons). Các cải tiến lõi động cơ sẽ được lưu giữ tại kho lưu trữ này.

Chỉ cần cài đặt [bản phát hành chính thức](https://github.com/CutWire-Studios/Drift/releases) và dựng một dự án thực tế để phản hồi những điểm chưa tốt đã là sự hỗ trợ to lớn cho dự án. Trên [Discord](https://discord.gg/J5ANFz6Z3y), những người đóng góp có thể yêu cầu vai trò `@Contributor`.

## Người đóng góp

<a href="https://github.com/CutWire-Studios/Drift/graphs/contributors">
  <img alt="Người đóng góp" src="https://contrib.rocks/image?repo=CutWire-Studios/Drift">
</a>

## Giấy phép

GPLv3 — xem [LICENSE](../LICENSE).

## Lịch sử đánh giá sao (Star History)

<a href="https://www.star-history.com/?repos=CutWire-Studios%2FDrift&type=date&legend=top-left">
 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/chart?repos=CutWire-Studios/Drift&type=date&legend=top-left&sealed_token=zHW_d2jon9Wn-HYP2SWQWLC7qDRaY7qwsvHS0Cp0Ywk1Rf1UvyxhWsakIrx2c11OijPJQ9o52W99jdigV7MOz5RuvLsyWQmBiMvMdk99mcfbgb591WtzNXQO8_K2YhgdbiPD9by00lwl69ZgCZnThFKBwhRbK7IQzIeFkIFnb2o0r5GhJh0HAX6Q8yTM" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/chart?repos=CutWire-Studios/Drift&type=date&legend=top-left&sealed_token=zHW_d2jon9Wn-HYP2SWQWLC7qDRaY7qwsvHS0Cp0Ywk1Rf1UvyxhWsakIrx2c11OijPJQ9o52W99jdigV7MOz5RuvLsyWQmBiMvMdk99mcfbgb591WtzNXQO8_K2YhgdbiPD9by00lwl69ZgCZnThFKBwhRbK7IQzIeFkIFnb2o0r5GhJh0HAX6Q8yTM" />
   <img alt="Biểu đồ Star History" src="https://api.star-history.com/chart?repos=CutWire-Studios/Drift&type=date&legend=top-left&sealed_token=zHW_d2jon9Wn-HYP2SWQWLC7qDRaY7qwsvHS0Cp0Ywk1Rf1UvyxhWsakIrx2c11OijPJQ9o52W99jdigV7MOz5RuvLsyWQmBiMvMdk99mcfbgb591WtzNXQO8_K2YhgdbiPD9by00lwl69ZgCZnThFKBwhRbK7IQzIeFkIFnb2o0r5GhJh0HAX6Q8yTM" />
 </picture>
</a>
