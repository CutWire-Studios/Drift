<p align="center">
  <img src="../Drift_icon.png" alt="Drift আইকন" width="128" height="128">
</p>

<h1 align="center">Drift</h1>

<p align="center">
  <strong>বিনামূল্যে ডেস্কটপ ভিডিও এডিটর যা আপনার ভিডিওকে শুধুমাত্র "মোটামুটি" নয়, সম্পূর্ণ পেশাদার রূপ দেয়।</strong>
</p>

<p align="center">
  <a href="https://github.com/CutWire-Studios/Drift/releases/latest"><img src="https://img.shields.io/github/v/release/CutWire-Studios/Drift?label=release&style=flat&labelColor=28272f&color=feb504" alt="সর্বশেষ রিলিজ" height="24"></a>
  <a href="https://github.com/CutWire-Studios/Drift/releases"><img src="https://img.shields.io/github/downloads/CutWire-Studios/Drift/total?label=GitHub%20downloads&style=flat&labelColor=28272f&color=feb504" alt="গিটহাব ডাউনলোড" height="24"></a>
  <a href="https://flathub.org/apps/org.cutwire.Drift"><img src="https://img.shields.io/flathub/downloads/org.cutwire.Drift?label=Flathub%20installs&style=flat&labelColor=28272f&color=feb504" alt="ফ্ল্যাটহাব ইনস্টল" height="24"></a>
  <a href="https://discord.gg/J5ANFz6Z3y"><img src="https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Fdiscord.com%2Fapi%2Finvites%2FJ5ANFz6Z3y%3Fwith_counts%3Dtrue&query=%24.approximate_member_count&suffix=%20members&logo=discord&logoColor=F8F8F8&label=Discord&color=feb504&labelColor=28272f&style=flat" alt="Drift ডিসকর্ড কমিউনিটিতে যোগ দিন" height="24"></a>
  <a href="../LICENSE"><img src="https://img.shields.io/github/license/CutWire-Studios/Drift?style=flat&labelColor=28272f&color=feb504" alt="লাইসেন্স: GPL-3.0" height="24"></a>
  <img src="https://img.shields.io/badge/platform-Linux%20%7C%20Windows%20%7C%20macOS%20%7C%20Android-feb504?style=flat&labelColor=28272f" alt="প্ল্যাটফর্ম: Linux | Windows | macOS | Android" height="24">
</p>

<p align="center">
  <a href="https://cutwire.org/drift"><strong>cutwire.org/drift</strong></a>
</p>

Drift হলো CutWire Studios-এর একটি ডেস্কটপ ভিডিও এডিটর। ক্লিপ যুক্ত করুন, ইফেক্ট, ক্যাপশন, স্টিকার
ও সঙ্গীত যোগ করুন, আর নিখুঁত ও চমৎকার ভিডিও এক্সপোর্ট করুন — **কোনো সাবস্ক্রিপশন ছাড়া, ওয়াটারমার্ক ছাড়া এবং কোনো অ্যাকাউন্ট খোলার ঝামেলা ছাড়াই**।

এটি তৈরি করা হয়েছে কনটেন্ট ক্রিয়েটরদের আসল কাজের কথা মাথায় রেখে: Reels ও Shorts, গেম ক্লিপ, স্কুলের প্রজেক্ট,
টিউটোরিয়াল, প্রডাক্ট ডেমো, মিম এবং যেকোনো ভিডিও যা আপনি ব্রাউজারে আটকে না থেকে বা মাসিক ফি না দিয়ে দারুণভাবে তৈরি করতে চান।

প্রিভিউতে যা দেখবেন, এক্সপোর্টেও হুবহু তাই পাবেন। একক কম্পোজিটর, একই ভিজ্যুয়াল স্টাইল, কোনো অপ্রত্যাশিত ত্রুটি নেই।

## ডাউনলোড

<p align="center">
  <a href="https://flathub.org/apps/org.cutwire.Drift">
    <img src="https://flathub.org/api/badge?locale=bn" alt="Flathub থেকে সংগ্রহ করুন" height="80">
  </a>
  <a href="https://apps.microsoft.com/detail/9PHHBZ07FRSZ">
    <img src="https://get.microsoft.com/images/en-us%20dark.svg" alt="Microsoft Store থেকে সংগ্রহ করুন" width="293">
  </a>
  <a href="../docs/play-testing.md">
    <img src="../docs/btn-gplay-en.png" alt="Google Play থেকে সংগ্রহ করুন (ক্লোজড টেস্টিং)" height="80">
  </a>
</p>

<p align="center">গুগল প্লে স্টোরে ক্লোজড টেস্টিং চলছে — <a href="../docs/play-testing.md">কীভাবে যোগ দেবেন</a>।</p>

**Linux** — Flathub থেকে ইনস্টল করুন:

```bash
flatpak install flathub org.cutwire.Drift
flatpak run org.cutwire.Drift
```
**macOS** — Homebrew-এর মাধ্যমে ইনস্টল করুন:

```bash
brew install --cask cutwire-studios/tap/drift
```

> **macOS-এ প্রথমবার চালুর নির্দেশিকা:** Drift অ্যাড-হক সাইন করা (অ্যপল কর্তৃক নোটারাইজড নয়)। Homebrew-এর মাধ্যমে ইনস্টল করার সময় কোয়ারেন্টিন অ্যাট্রিবিউট স্বয়ংক্রিয়ভাবে অপসারিত হয়। আপনি যদি `.dmg` দিয়ে ম্যানুয়ালি ইনস্টল করেন বা Gatekeeper চালু করতে বাধা দেয়, তবে টার্মিনালে চালান:
> ```bash
> xattr -dr com.apple.quarantine /Applications/Drift.app
> ```

অথবা আপনার প্ল্যাটফর্মের জন্য সরাসরি তৈরি বিল্ড ডাউনলোড করুন
[সর্বশেষ রিলিজ](https://github.com/CutWire-Studios/Drift/releases/latest) পাতা থেকে:

| প্ল্যাটফর্ম | প্যাকেজ |
|-------------|---------|
| Linux | [Flathub](https://flathub.org/apps/org.cutwire.Drift) · [AppImage](https://github.com/CutWire-Studios/Drift/releases/latest) |
| Windows | [Microsoft Store](https://apps.microsoft.com/detail/9PHHBZ07FRSZ) · [ইনস্টলার (.exe)](https://github.com/CutWire-Studios/Drift/releases/latest) · [পোর্টেবল জিপ (.zip)](https://github.com/CutWire-Studios/Drift/releases/latest) |
| macOS | [Homebrew Tap](https://github.com/CutWire-Studios/homebrew-tap) · [ডিস্ক ইমেজ (.dmg, Apple Silicon)](https://github.com/CutWire-Studios/Drift/releases/latest) |
| Android | [Google Play (ক্লোজড টেস্ট)](../docs/play-testing.md) · [APK (arm64-v8a)](https://github.com/CutWire-Studios/Drift/releases/latest) · [APK (armeabi-v7a)](https://github.com/CutWire-Studios/Drift/releases/latest) · [APK (x86_64)](https://github.com/CutWire-Studios/Drift/releases/latest) |

ফোনে [Google Play ক্লোজড টেস্টিং-এ](../docs/play-testing.md) যোগ দিন, অথবা সর্বশেষ রিলিজ থেকে `Drift-*-arm64-v8a.apk` ফাইলটি ডাউনলোড করে ইনস্টল করুন (অথবা `adb install Drift-*-arm64-v8a.apk` ব্যবহার করুন)। এমুলেটরের জন্য `x86_64` ব্যবহার করুন।

পূর্ববর্তী সংস্করণ ও পূর্ণ চেঞ্জলগ দেখতে [সব রিলিজ](https://github.com/CutWire-Studios/Drift/releases) তালিকাটি দেখুন।

## স্ক্রিনশট

<p align="center">
  <img src="../docs/screenshots/main-window.png" alt="Drift এডিটর: বামে মিডিয়া বিন, প্রিভিউতে থ্রিডি ক্যামেরা, ডানে টেক্সট ইন্সপেক্টর, নিচে মাল্টি-ট্র্যাক টাইমলাইন" width="900">
</p>

## বৈশিষ্ট্যসমূহ

**এআই এজেন্ট খোলা প্রজেক্ট সরাসরি সম্পাদনা করতে পারে।** আপনার খোলা প্রজেক্টেই ইমপোর্ট, কাট, কালার গ্রেডিং ও এক্সপোর্ট সম্পন্ন করুন। এডিটরটি উইন্ডো ছাড়াই ব্যাকগ্রাউন্ডে (headless) চলতে পারে।

**টাইমলাইনে থ্রিডি (3D)।** ত্রিমাত্রিক স্পেসে ক্লিপগুলো বাঁকান, আলো ফেলুন এবং বিষয়ের পেছনে শিরোনাম বসান। একটি থ্রিডি মডেল টেনে আনলে একই দৃশ্যে তা রেন্ডার হবে।

**Lottie অ্যানিমেশন সাপোর্ট।** মোশন গ্রাফিক্স ও ভেক্টর আর্ট যেকোনো স্কেলেই সম্পূর্ণ পরিষ্কার থাকে। ক্লিপ থেকেই রঙ বা টেক্সট সরাসরি বদলে নেওয়া যায়।

**উন্নত কীফ্রেমিং (Keyframing)।** সময়ের সাথে সাথে পজিশন, স্কেল, রোটেশন, অপাসিটি এবং ইফেক্ট প্যারামিটার নিয়ন্ত্রণ করুন। অ্যানিমেশন কার্ভ নিজে আঁকুন।

**১৫০টিরও বেশি ট্রানজিশন এবং ৪০টিরও বেশি ইফেক্ট।** প্রতিটি লুক আপনার আসল ভিডিওতেই প্রিভিউ হয়। এক ক্লিকেই পুরো ইফেক্ট স্ট্যাক প্রয়োগ করুন — Beat Drop, Glitch Cut, Neon Cutout। পূর্ববর্তী ফ্রেমের ট্রেইল এবং নির্দিষ্ট ক্লিপের সাথে সংযুক্ত গ্রেডিং।

**সম্পূর্ণ লোকাল মেশিনে প্রসেসিং।** যেকোনো সাবজেক্টে ক্লিক করে দৃশ্য থেকে আলাদা করে কাটআউট করুন। ক্লিপের নিজস্ব ডেপথ অফ ফিল্ড ও লাইটিং যোগ করুন। স্বয়ংক্রিয় সাবটাইটেল, ভিডিও আপস্কেল ও লাইভ ফেস রিটাচ — সবই আপনার কম্পিউটারের শক্তিতে।

**টেক্সটের মাধ্যমে ভিডিও কাটছাঁট।** কথা ভিডিও ক্লিপে স্বয়ংক্রিয়ভাবে টেক্সটে পরিণত হয়। অপ্রয়োজনীয় বাক্য ও পজ বাদ দিন, সেরা টেক বেছে নিন, স্পিকার চিহ্নিত করুন এবং সেখান থেকেই নির্ভুল ক্যাপশন তৈরি করুন।

**দৃষ্টি আকর্ষণকারী আকর্ষণীয় টাইটেল।** ৩৩টি রেডিমেড স্টাইল প্যাক — কারাওকে, হরমুজি-স্টাইল, নিয়ন, ক্রোম, হোলোগ্রাফিক, হস্তাক্ষর। পুরো ট্র্যাকের সব সাবটাইটেলে এক ক্লিকেই স্টাইল কপি করুন।

**একসাথে পুরো স্ট্যাক মুভ করুন।** একটি ট্রান্সফর্ম লেয়ার তার নিচের সব উপাদান ড্র্যাগ, স্কেল, রোটেট, টিল্ট এবং ফেইড করতে পারে। লেয়ারগুলো নেস্ট করুন, অথবা আলাদা টাইমলাইনের প্রয়োজন হলে কম্পোজিট ওপেন করুন।

**সঙ্গীতের তালে কাট করুন।** টাইমলাইন নিজে থেকেই মিউজিক বিটের সাথে স্ন্যাপ করে। ক্লিপগুলো তালের নিখুঁত মাপে বিভক্ত হয়। কথার সময় ব্যাকগ্রাউন্ড মিউজিকের ভলিউম স্বয়ংক্রিয়ভাবে কমে যায় (audio ducking) এবং পরে স্বাভাবিক হয়। অনুভূমিক ভিডিও স্বয়ংক্রিয়ভাবে ফেস ট্র্যাক করে উল্লম্ব শর্টসে রূপান্তর করুন।

**নির্ভরযোগ্য টাইমলাইন অভিজ্ঞতা।** মাল্টি-ট্র্যাক ফিল্মস্ট্রিপ, রিপল এডিটিং, স্ন্যাপিং, মাস্ক, ফ্রিজ ফ্রেম, বুকমার্ক ও অডিও মিক্সার। ক্লিপ বিভক্ত করলেও কালার গ্রেড অপরিবর্তিত থাকে। অ্যাপ ক্র্যাশ হলেও সর্বশেষ সেভ নিরাপদে অক্ষত থাকে।

**স্টুডিও মানের নিখুঁত অডিও।** কথার নয়েজ দূর করুন, লাউডনেস মিটারিং, ইকিউ (EQ) ও কম্প্রেসার, গতি পরিবর্তনের সময় পিচ ঠিক রাখা এবং সরাসরি ভয়েসওভার রেকর্ডিং।

**শট খুঁজুন ও মাল্টিক্যাম সুইচ করুন।** পর্দায় যা দেখা যাচ্ছে তা দিয়ে ক্লিপ সার্চ করুন। সব ক্যামেরার দৃশ্য একসাথে দেখুন এবং সঠিক মুহূর্তে ক্যামেরা বদলান।

**স্টেবিলাইজেশন, এক্সপোর্ট ও সম্পূর্ণ প্রজেক্ট প্যাকেজিং।** ক্যামেরার ঝাঁকুনি দূর করুন, কোয়ালিটি আপস্কেল করুন এবং ভারী ফাইল মসৃণভাবে চালান। MP4, GIF, অডিও কিংবা নির্দিষ্ট অংশের এক্সপোর্ট। মিডিয়া ফাইলগুলো প্রজেক্টের সাথে প্যাক করুন যাতে পাথ নষ্ট না হয়।

**Android-এ একই সম্পাদনার ক্ষমতা।** ফোনেও পাওয়া যাবে পুরো টাইমলাইন, ইফেক্ট ও এক্সপোর্ট সুবিধা। অ্যাপ থেকে সরাসরি শেয়ার করুন।

**স্টক লাইব্রেরি, ভয়েস সিন্থেসিস ও হালকা সাইজ।** মিডিয়া বিন থেকেই স্টক মিডিয়া সার্চ করুন। ভয়েসওভার ও সাউন্ড ইফেক্ট তৈরি করুন। ফন্ট, স্টিকার ও অতিরিক্ত মডেলগুলো প্রয়োজন অনুযায়ী ডাউনলোড হয়। ইউজার ইন্টারফেস আরবি, বাংলা, স্প্যানিশ (স্পেন ও কলম্বিয়া), ফরাসি (কানাডা), ইতালীয়, জাপানি, পর্তুগিজ (ব্রাজিল ও পর্তুগাল), রুশ, সিংহলি, তাগালগ, ভিয়েতনামী ও সরলীকৃত চীনা ভাষায় অনূদিত।

## মানুষ কেন Drift বেছে নেয়?

বেশিরভাগ "ফ্রি" এডিটর ভিডিও একটু সুন্দর হতেই অ্যাকাউন্ট খোলা, ওয়াটারমার্ক চাপিয়ে দেওয়া বা মাসিক সাবস্ক্রিপশন নেওয়ার দাবি তোলে। Drift এর বিপরীত: **সম্পূর্ণ আপনার, আপনার কম্পিউটারে চলে, GPLv3 ওপেন সোর্স, লগইন করার কোনো বাধা নেই।**

সামাজিক মাধ্যমের ৩০ সেকেন্ডের দ্রুত ভিডিওর জন্য যেমন দ্রুতগতির, তেমনি পূর্ণাঙ্গ প্রজেক্টের জন্য শক্তিশালী — টাইমলাইনে এআই এজেন্ট, 3D ও Lottie, ক্যাপশন, ভিজ্যুয়াল ইফেক্ট, অডিও নিয়ন্ত্রণ, স্মার্ট কাটআউট এবং মাল্টিক্যাম।

## Drift অনুবাদে আমাদের সাহায্য করুন

[![অনুবাদ অগ্রগতি](https://hosted.weblate.org/widget/cutwire-drift/drift-desktop/multi-auto.svg)](https://hosted.weblate.org/engage/cutwire-drift/)

## ডেভেলপারদের জন্য

বিল্ড, প্যাকেজিং, আর্কিটেকচার এবং এজেন্ট প্রোটোকল গাইড `docs/` ফোল্ডারে পাওয়া যাবে:

- [বিল্ডিং, টেস্টিং, প্যাকেজিং ও আর্কিটেকচার](../docs/BUILDING.md)
- [GPU ইফেক্ট](../docs/gpu-effects.md)
- [GPU ট্রানজিশন](../docs/gpu-transitions.md)
- [এজেন্ট অ্যাক্সেস / MCP](../docs/MCP.md)

## সহায়তা ও মতামত

কোনো বাগ পেয়েছেন বা নতুন আইডিয়া আছে? একটি
[GitHub Issue খুলুন](https://github.com/CutWire-Studios/Drift/issues)।

## অবদানে অংশ নিন

[CONTRIBUTING.md](../CONTRIBUTING.md) ফাইলে পুল রিকোয়েস্ট নির্দেশিকা রয়েছে: বড় পরিবর্তনের আগে ইস্যু খুলুন, প্রতি রিকোয়েস্টে একটি বিষয় রাখুন, টেস্ট চালান এবং কাজটি GPLv3-এর আওতায় রাখুন।

ইফেক্ট, ট্রানজিশন, টেমপ্লেট ও অডিও ফিল্টার অ্যাড-অন হিসেবে গণ্য হয়। সেগুলো পাঠান [Drift-Addons](https://github.com/CutWire-Studios/Drift-Addons) রিপোজিটরিতে। ইঞ্জিন পরিবর্তনের কাজ এই মূল রিপোজিটরিতেই থাকে।

একটি [অফিসিয়াল রিলিজ](https://github.com/CutWire-Studios/Drift/releases) ইনস্টল করে বাস্তব প্রজেক্ট এডিট করাই প্রজেক্টকে এগিয়ে নেওয়ার বড় সহায়তা; ত্রুটি পেলে তা জানান। আমাদের [Discord](https://discord.gg/J5ANFz6Z3y) সার্ভারে অবদানকারীরা `@Contributor` রোলের আবেদন করতে পারেন।

## অবদানকারীগণ

<a href="https://github.com/CutWire-Studios/Drift/graphs/contributors">
  <img alt="অবদানকারীগণ" src="https://contrib.rocks/image?repo=CutWire-Studios/Drift">
</a>

## লাইসেন্স

GPLv3 — বিস্তারিত জানতে [LICENSE](../LICENSE) দেখুন।

## স্টার ইতিহাস (Star History)

<a href="https://www.star-history.com/?repos=CutWire-Studios%2FDrift&type=date&legend=top-left">
 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/chart?repos=CutWire-Studios/Drift&type=date&legend=top-left&sealed_token=zHW_d2jon9Wn-HYP2SWQWLC7qDRaY7qwsvHS0Cp0Ywk1Rf1UvyxhWsakIrx2c11OijPJQ9o52W99jdigV7MOz5RuvLsyWQmBiMvMdk99mcfbgb591WtzNXQO8_K2YhgdbiPD9by00lwl69ZgCZnThFKBwhRbK7IQzIeFkIFnb2o0r5GhJh0HAX6Q8yTM" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/chart?repos=CutWire-Studios/Drift&type=date&legend=top-left&sealed_token=zHW_d2jon9Wn-HYP2SWQWLC7qDRaY7qwsvHS0Cp0Ywk1Rf1UvyxhWsakIrx2c11OijPJQ9o52W99jdigV7MOz5RuvLsyWQmBiMvMdk99mcfbgb591WtzNXQO8_K2YhgdbiPD9by00lwl69ZgCZnThFKBwhRbK7IQzIeFkIFnb2o0r5GhJh0HAX6Q8yTM" />
   <img alt="Star History Chart" src="https://api.star-history.com/chart?repos=CutWire-Studios/Drift&type=date&legend=top-left&sealed_token=zHW_d2jon9Wn-HYP2SWQWLC7qDRaY7qwsvHS0Cp0Ywk1Rf1UvyxhWsakIrx2c11OijPJQ9o52W99jdigV7MOz5RuvLsyWQmBiMvMdk99mcfbgb591WtzNXQO8_K2YhgdbiPD9by00lwl69ZgCZnThFKBwhRbK7IQzIeFkIFnb2o0r5GhJh0HAX6Q8yTM" />
 </picture>
</a>
