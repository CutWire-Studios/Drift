<p align="center">
  <img src="../Drift_icon.png" alt="أيقونة Drift" width="128" height="128">
</p>

<h1 align="center">Drift</h1>

<p align="center">
  <strong>محرر الفيديو المكتبي المجاني الذي يجعل مقاطعك تبدو متقنة واحترافية — وليست مجرد "مقبولة".</strong>
</p>

<p align="center">
  <a href="https://github.com/CutWire-Studios/Drift/releases/latest"><img src="https://img.shields.io/github/v/release/CutWire-Studios/Drift?label=release&style=flat&labelColor=28272f&color=feb504" alt="أحدث إصدار" height="24"></a>
  <a href="https://github.com/CutWire-Studios/Drift/releases"><img src="https://img.shields.io/github/downloads/CutWire-Studios/Drift/total?label=GitHub%20downloads&style=flat&labelColor=28272f&color=feb504" alt="تنزيلات GitHub" height="24"></a>
  <a href="https://flathub.org/apps/org.cutwire.Drift"><img src="https://img.shields.io/flathub/downloads/org.cutwire.Drift?label=Flathub%20installs&style=flat&labelColor=28272f&color=feb504" alt="تثبيتات Flathub" height="24"></a>
  <a href="https://discord.gg/J5ANFz6Z3y"><img src="https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Fdiscord.com%2Fapi%2Finvites%2FJ5ANFz6Z3y%3Fwith_counts%3Dtrue&query=%24.approximate_member_count&suffix=%20members&logo=discord&logoColor=F8F8F8&label=Discord&color=feb504&labelColor=28272f&style=flat" alt="انضم إلى مجتمع Drift على Discord" height="24"></a>
  <a href="../LICENSE"><img src="https://img.shields.io/github/license/CutWire-Studios/Drift?style=flat&labelColor=28272f&color=feb504" alt="الترخيص: GPL-3.0" height="24"></a>
  <img src="https://img.shields.io/badge/platform-Linux%20%7C%20Windows%20%7C%20macOS%20%7C%20Android-feb504?style=flat&labelColor=28272f" alt="المنصات المدعومة: Linux | Windows | macOS | Android" height="24">
</p>

<p align="center">
  <a href="https://cutwire.org/drift"><strong>cutwire.org/drift</strong></a>
</p>

برنامج Drift هو محرر فيديو مكتبي لأجهزة الكمبيوتر من تطوير CutWire Studios. اسحب مقاطعك وأفلتها، أضف التأثيرات، والترجمات، والملصقات،
والموسيقى، ثم قم بتصدير فيديو مصقول — **بدون اشتراكات، وبدون علامات مائية، وبدون الحاجة لإنشاء حساب**.

تم بناؤه لخدمة المونتاج الحقيقي الذي يحتاجه صُنّاع المحتوى اليوم: مقاطع Reels و Shorts، ولقطات الألعاب، والمشاريع المدرسية،
والشروحات التعليمية، والعروض التقديمية، والميمز، وكل ما تريد تقديمه بمظهر احترافي دون التقيد بمتصفح الويب
أو دفع رسوم شهرية.

ما تراه في المعاينة هو بالضبط ما يتم تصديره. محرك تركيب رسومي واحد، مظهر موحد، وبدون مفاجآت.

## التنزيل

<p align="center">
  <a href="https://flathub.org/apps/org.cutwire.Drift">
    <img src="https://flathub.org/api/badge?locale=ar" alt="احصل عليه من Flathub" height="80">
  </a>
  <a href="https://apps.microsoft.com/detail/9PHHBZ07FRSZ">
    <img src="https://get.microsoft.com/images/en-us%20dark.svg" alt="احصل عليه من متجر Microsoft" width="293">
  </a>
  <a href="../docs/play-testing.md">
    <img src="../docs/btn-gplay-en.png" alt="احصل عليه من Google Play (اختبار مغلق)" height="80">
  </a>
</p>

<p align="center">تطبيق Google Play في مرحلة اختبار مغلق — <a href="../docs/play-testing.md">طريقة الانضمام</a>.</p>

**Linux** — التثبيت عبر Flathub:

```bash
flatpak install flathub org.cutwire.Drift
flatpak run org.cutwire.Drift
```
**macOS** — التثبيت عبر Homebrew:

```bash
brew install --cask cutwire-studios/tap/drift
```

> **التشغيل الأول على macOS:** برنامج Drift موقّع ذاتياً (ad-hoc وليس موثقاً رسمياً من Apple). يتم تلقائياً إزالة سمة الحجر الصحي (quarantine) عند التثبيت عبر Homebrew. إذا قمت بالتثبيت يدوياً عبر ملف `.dmg` أو قام نظام Gatekeeper بمنع الفتح، نفّذ الأمر التالي:
> ```bash
> xattr -dr com.apple.quarantine /Applications/Drift.app
> ```

أو حمّل النسخة المجهزة لنظامك مباشرة من
[أحدث إصدار](https://github.com/CutWire-Studios/Drift/releases/latest):

| المنصة | الحزمة |
|--------|--------|
| Linux | [Flathub](https://flathub.org/apps/org.cutwire.Drift) · [AppImage](https://github.com/CutWire-Studios/Drift/releases/latest) |
| Windows | [Microsoft Store](https://apps.microsoft.com/detail/9PHHBZ07FRSZ) · [برنامج التثبيت (.exe)](https://github.com/CutWire-Studios/Drift/releases/latest) · [نسخة محمولة zip](https://github.com/CutWire-Studios/Drift/releases/latest) |
| macOS | [Homebrew Tap](https://github.com/CutWire-Studios/homebrew-tap) · [ملف صورة قرص (.dmg, Apple Silicon)](https://github.com/CutWire-Studios/Drift/releases/latest) |
| Android | [Google Play (اختبار مغلق)](../docs/play-testing.md) · [APK (arm64-v8a)](https://github.com/CutWire-Studios/Drift/releases/latest) · [APK (armeabi-v7a)](https://github.com/CutWire-Studios/Drift/releases/latest) · [APK (x86_64)](https://github.com/CutWire-Studios/Drift/releases/latest) |

على الهواتف، انضم إلى [برنامج الاختبار المغلق على Google Play](../docs/play-testing.md)، أو حمّل ملف `Drift-*-arm64-v8a.apk` من صفحة أحدث إصدار وقم بتثبيته (أو عبر `adb install Drift-*-arm64-v8a.apk`). استخدم إصدار `x86_64` للمحاكيات.

راجع [جميع الإصدارات](https://github.com/CutWire-Studios/Drift/releases) للاطلاع على الإصدارات السابقة وسجلات التغييرات الكاملة.

## لقطة شاشة

<p align="center">
  <img src="../docs/screenshots/main-window.png" alt="واجهة محرر Drift: لوحة الوسائط على اليسار، كاميرا ثلاثية الأبعاد في المعاينة، فاحص النصوص على اليمين، والمخطط الزمني متعدد المسارات في الأسفل" width="900">
</p>

## الميزات الرئيسية

**إمكانية تحرير المشروع المفتوح عبر وكيل ذكاء اصطناعي (Agent).** استيراد المقاطع، والقص، وتصحيح الألوان، والتصدير مباشرة داخل المشروع المفتوح. يمكن للمحرر نفسه العمل بدون نافذة رسومية (وضع Headless).

**ثلاثي الأبعاد داخل المخطط الزمني مباشرة.** يمكنك إمالة المقاطع في الفضاء ثلاثي الأبعاد، وإضاءتها، ووضع العناوين خلف العنصر المصوّر. اسحب نموذجاً ثلاثي الأبعاد وسيعمل فوراً داخل المشهد.

**دعم رسوميات Lottie المتحركة.** تظل الرسومات المتحركة وفنون المتجهات (Vector) فائقة الوضوح عند أي تكبير. يمكنك تغيير الألوان أو النصوص فوراً في مكانها.

**نظام إطارات مفتاحية (Keyframing) متقدم.** تحريك الموضع، والمقاس، والتدوير، والشفافية، ومعاملات التأثيرات بمرور الوقت مع إمكانية رسم منحنيات الحركة يدوياً.

**أكثر من 150 انتقالاً و 40 تأثيراً.** معاينة فورية لكل نمط على لقطاتك الفعلية. نقرة واحدة كافية لتطبيق حزمة تأثيرات كاملة — مثل Beat Drop و Glitch Cut و Neon Cutout. ومسارات إطارات سابقة، وتدريج لوني يلتزم بكل مقطع.

**معالجة محلية بالكامل على جهازك.** انقر على أي شخص أو عنصر لعزله وقصه من المشهد. أضف عمق مجال وإضاءة خاصة بالمقطع. تفريغ صوتي آلي للترجمات، ورفع دقة الفيديو (Upscale)، وتحسين ملامح الوجه المباشر — كل ذلك محلياً على حاسوبك.

**مونتاج الفيديو عبر تحرير النص.** يتحول الكلام المنطوق إلى نص مكتوب على المقطع. احذف العبارات، وتخلص من الكلمات الزائدة، وأزل فترات الصمت، واختر أفضل اللقطات، وميّز المتحدثين، وأنشئ شريط ترجمة دقيق من تلك التوقيتات.

**عناوين بتصميم احترافي وجذاب.** 33 حزمة أنماط جاهزة — كاريوكي، أسلوب Hormozi، نيون، كروم، هولوغرام، وخط يدوي. انسخ نمطاً واحداً وطبّقه على كل نصوص الترجمة في المسار دفعة واحدة.

**تحريك مجموعات الطبقات معاً.** يمكن لطبقة تحويل واحدة سحب، وتكبير، وتدوير، وإمالة، وتعتيم كل ما يقع تحتها. اجمع المسارات في طبقات متداخلة أو افتح تركيباً مستقلاً متى احتاج المشهد إلى مخطط زمني خاص به.

**المونتاج على إيقاع الموسيقى.** ينجذب المخطط الزمني تلقائياً إلى النبضات الموسيقية. تنقسم المقاطع وتستقر بدقة على المازورة. تنخفض الموسيقى تلقائياً (Audio Ducking) أسفل الصوت البشري وتعود لمستواها الطبيعي بعدها. إمكانية إعادة تأطير اللقطات الأفقية وتحويلها إلى فيديو طولي يتبع الوجه تلقائياً.

**مخطط زمني عالي الاستجابة والتحكم.** شرائط تصوير متعددة المسارات، تحرير متتالي لسد الفراغات (Ripple)، محاذاة تلقائية (Snap)، أقنعة (Masks)، تجميد الإطار (Freeze Frame)، إشارات مرجعية، ومازج صوت مدمج. عند تقسيم مقطع ما يظل التلوين ثابتاً، وفي حال حدوث إغلاق مفاجئ يظل آخر حفظ للمشروع كما هو بدون تلف.

**هندسة صوتية مكتملة.** تنقية الكلام من الضوضاء، وقياس جهارة الصوت، ومعادلة الترددات (EQ)، والضغط (Compressor)، والحفاظ على طبقة الصوت عند تغيير السرعة، وتسجيل التعليق الصوتي المباشر.

**البحث في اللقطات والتبديل بين الكاميرات المتعددة (Multicam).** ابحث في المقاطع عما يظهر على الشاشة. شاهد جميع زوايا التصوير معاً وبدّل بينها بسلاسة.

**تثبيت الاهتزاز، والتصدير، وحزم المشاريع المتنقلة.** معالجة اللقطات المهتزة، ورفع الدقة، وتشغيل الملفات الثقيلة بسلاسة. التصدير بصيغ MP4 و GIF، أو ملف صوتي فقط، أو نطاق محدد. إمكانية حزم ملفات الوسائط مع التعديل لحفظ مسارات الملفات دون انقطاع.

**نفس التجربة الاحترافية على Android.** مخطط زمني وتأثيرات وتصدير كامل على الهاتف المحمول، مع إمكانية المشاركة الفورية من التطبيق.

**مكتبة وسائط مجانية، وتوليد أصوات، وحجم تثبيت صغير.** ابحث في وسائط الستوك مباشرة من سلة الوسائط. توليد تعليقات صوتية ومؤثرات صوتية. يتم تنزيل الخطوط والملصقات والنماذج الإضافية فقط عند استخدامها. واجهة المستخدم متوفرة باللغات: العربية، والبنغالية، والإسبانية (إسبانيا وكولومبيا)، والفرنسية (كندا)، والإيطالية، واليابانية، والبرتغالية (البرازيل والبرتغال)، والروسية، والسينهالية، والتاغالوغية، والفيتنامية، والصينية المبسطة.

## لماذا يفضل المستخدمون Drift؟

معظم محررات الفيديو "المجانية" تبدأ بطلب تسجيل حساب أو وضع علامات مائية أو فرض اشتراكات مدفوعة بمجرد أن يبدأ عملك بالظهور بشكل لائق. يقدم Drift النموذج النقي المقابل: **برنامجك بالكامل، على جهازك، مفتوح المصدر برخصة GPLv3، وبدون أي قيود أو شاشات تسجيل دخول.**

سريع بما يكفي لإنتاج مقطع مدته 30 ثانية لشبكات التواصل، وقوي بما يكفي لإنتاج مشاريع ضخمة — مساعد ذكاء اصطناعي على المخطط الزمني، 3D و Lottie، تفريغ الترجمة، مؤثرات بصرية وهندسة صوتية، وقص ذكي، ومونتاج كاميرات متعددة.

## ساعدنا في ترجمة Drift

[![حالة الترجمة](https://hosted.weblate.org/widget/cutwire-drift/drift-desktop/multi-auto.svg)](https://hosted.weblate.org/engage/cutwire-drift/)

## للمطورين

أدلة البناء والتجميع، والتحزيم، والهندسة البرمجية، وبروتوكول الوكلاء متوفرة داخل مجلد `docs/`:

- [البناء، والاختبار، والتحزيم، والبنية المعمارية](../docs/BUILDING.md)
- [تأثيرات معالج الرسوميات GPU](../docs/gpu-effects.md)
- [انتقالات معالج الرسوميات GPU](../docs/gpu-transitions.md)
- [واجهة تكامل الوكلاء / بروتوكول MCP](../docs/MCP.md)

## المساعدة والملاحظات

هل واجهت مشكلة برمجية أو لديك فكرة جديدة؟ يسعدنا فتح
[تذكرة عبر GitHub Issues](https://github.com/CutWire-Studios/Drift/issues).

## المساهمة في المشروع

ملف [CONTRIBUTING.md](../CONTRIBUTING.md) هو دليلك لتقديم طلبات السحب (Pull Requests): افتح تذكرة قبل التغييرات الكبيرة، واجعل كل طلب مقتصراً على تعديل واحد، وشغّل الاختبارات، مع ترخيص مساهمتك بموجب GPLv3.

التأثيرات، والانتقالات، والقوالب، وفلاتر الصوت تعتبر إضافات ملحقة. يمكنك إرسالها إلى مستودع [Drift-Addons](https://github.com/CutWire-Studios/Drift-Addons). التعديلات على المحرك الأساسي تبقى في هذا المستودع.

مجرد تثبيت [نسخة رسمية](https://github.com/CutWire-Studios/Drift/releases) والعمل على مشروع حقيقي يعد دعماً كبيراً؛ أبلغنا بما تعطل أو بما يمكن تحسينه. في مجتمعنا على [Discord](https://discord.gg/J5ANFz6Z3y)، يحق للمساهمين طلب رتبة `@Contributor`.

## المساهمون

<a href="https://github.com/CutWire-Studios/Drift/graphs/contributors">
  <img alt="المساهمون" src="https://contrib.rocks/image?repo=CutWire-Studios/Drift">
</a>

## الترخيص

GPLv3 — راجع [LICENSE](../LICENSE).

## سجل تفاعل النجوم (Star History)

<a href="https://www.star-history.com/?repos=CutWire-Studios%2FDrift&type=date&legend=top-left">
 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/chart?repos=CutWire-Studios/Drift&type=date&legend=top-left&sealed_token=zHW_d2jon9Wn-HYP2SWQWLC7qDRaY7qwsvHS0Cp0Ywk1Rf1UvyxhWsakIrx2c11OijPJQ9o52W99jdigV7MOz5RuvLsyWQmBiMvMdk99mcfbgb591WtzNXQO8_K2YhgdbiPD9by00lwl69ZgCZnThFKBwhRbK7IQzIeFkIFnb2o0r5GhJh0HAX6Q8yTM" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/chart?repos=CutWire-Studios/Drift&type=date&legend=top-left&sealed_token=zHW_d2jon9Wn-HYP2SWQWLC7qDRaY7qwsvHS0Cp0Ywk1Rf1UvyxhWsakIrx2c11OijPJQ9o52W99jdigV7MOz5RuvLsyWQmBiMvMdk99mcfbgb591WtzNXQO8_K2YhgdbiPD9by00lwl69ZgCZnThFKBwhRbK7IQzIeFkIFnb2o0r5GhJh0HAX6Q8yTM" />
   <img alt="رسم بياني لنجمات المستودع" src="https://api.star-history.com/chart?repos=CutWire-Studios/Drift&type=date&legend=top-left&sealed_token=zHW_d2jon9Wn-HYP2SWQWLC7qDRaY7qwsvHS0Cp0Ywk1Rf1UvyxhWsakIrx2c11OijPJQ9o52W99jdigV7MOz5RuvLsyWQmBiMvMdk99mcfbgb591WtzNXQO8_K2YhgdbiPD9by00lwl69ZgCZnThFKBwhRbK7IQzIeFkIFnb2o0r5GhJh0HAX6Q8yTM" />
 </picture>
</a>
