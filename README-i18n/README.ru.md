<p align="center">
  <img src="../Drift_icon.png" alt="Иконка Drift" width="128" height="128">
</p>

<h1 align="center">Drift</h1>

<p align="center">
  <strong>Бесплатный видеоредактор для ПК, делающий ваши видео профессиональными, а не просто «сойдет».</strong>
</p>

<p align="center">
  <a href="https://github.com/CutWire-Studios/Drift/releases/latest"><img src="https://img.shields.io/github/v/release/CutWire-Studios/Drift?label=release&style=flat&labelColor=28272f&color=feb504" alt="Последний релиз" height="24"></a>
  <a href="https://github.com/CutWire-Studios/Drift/releases"><img src="https://img.shields.io/github/downloads/CutWire-Studios/Drift/total?label=GitHub%20downloads&style=flat&labelColor=28272f&color=feb504" alt="Количество скачиваний на GitHub" height="24"></a>
  <a href="https://flathub.org/apps/org.cutwire.Drift"><img src="https://img.shields.io/flathub/downloads/org.cutwire.Drift?label=Flathub%20installs&style=flat&labelColor=28272f&color=feb504" alt="Установки Flathub" height="24"></a>
  <a href="https://discord.gg/J5ANFz6Z3y"><img src="https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Fdiscord.com%2Fapi%2Finvites%2FJ5ANFz6Z3y%3Fwith_counts%3Dtrue&query=%24.approximate_member_count&suffix=%20members&logo=discord&logoColor=F8F8F8&label=Discord&color=feb504&labelColor=28272f&style=flat" alt="Присоединиться к Drift в Discord" height="24"></a>
  <a href="../LICENSE"><img src="https://img.shields.io/github/license/CutWire-Studios/Drift?style=flat&labelColor=28272f&color=feb504" alt="Лицензия: GPL-3.0" height="24"></a>
  <img src="https://img.shields.io/badge/platform-Linux%20%7C%20Windows%20%7C%20macOS%20%7C%20Android-feb504?style=flat&labelColor=28272f" alt="Платформы: Linux | Windows | macOS | Android" height="24">
</p>

<p align="center">
  <a href="https://cutwire.org/drift"><strong>cutwire.org/drift</strong></a>
</p>

Drift — это настольный видеоредактор от CutWire Studios. Добавляйте клипы, применяйте эффекты, субтитры, стикеры
и музыку, а затем экспортируйте идеальное видео — **без подписок, водяных знаков и обязательной регистрации**.

Он создан для задач реальных создателей контента: Reels и Shorts, игровых нарезок, учебных проектов,
обучающих роликов, демонстраций продуктов, мемов и любых видео, которые должны выглядеть безупречно без браузерных ограничений
и ежемесячных платежей.

То, что вы видите в окне предпросмотра, в точности попадает в финальный экспорт. Единый композитор, единый стиль, никаких сюрпризов.

## Загрузка

<p align="center">
  <a href="https://flathub.org/apps/org.cutwire.Drift">
    <img src="https://flathub.org/api/badge?locale=ru" alt="Доступно на Flathub" height="80">
  </a>
  <a href="https://apps.microsoft.com/detail/9PHHBZ07FRSZ">
    <img src="https://get.microsoft.com/images/en-us%20dark.svg" alt="Загрузить из Microsoft Store" width="293">
  </a>
  <a href="../docs/play-testing.md">
    <img src="../docs/btn-gplay-en.png" alt="Доступно в Google Play (закрытое тестирование)" height="80">
  </a>
</p>

<p align="center">В Google Play проходит закрытое тестирование — <a href="../docs/play-testing.md">как присоединиться</a>.</p>

**Linux** — установка через Flathub:

```bash
flatpak install flathub org.cutwire.Drift
flatpak run org.cutwire.Drift
```
**macOS** — установка через Homebrew:

```bash
brew install --cask cutwire-studios/tap/drift
```

> **Первый запуск на macOS:** Drift имеет ad-hoc подпись (без нотаризации Apple). Атрибут карантина снимается автоматически при установке через Homebrew. Если вы устанавливаете вручную из `.dmg` или Gatekeeper блокирует запуск, выполните:
> ```bash
> xattr -dr com.apple.quarantine /Applications/Drift.app
> ```

Или загрузите готовую сборку для вашей платформы из
[последнего релиза](https://github.com/CutWire-Studios/Drift/releases/latest):

| Платформа | Пакет |
|-----------|-------|
| Linux | [Flathub](https://flathub.org/apps/org.cutwire.Drift) · [AppImage](https://github.com/CutWire-Studios/Drift/releases/latest) |
| Windows | [Microsoft Store](https://apps.microsoft.com/detail/9PHHBZ07FRSZ) · [Инсталлятор (.exe)](https://github.com/CutWire-Studios/Drift/releases/latest) · [Портативный zip](https://github.com/CutWire-Studios/Drift/releases/latest) |
| macOS | [Homebrew Tap](https://github.com/CutWire-Studios/homebrew-tap) · [Образ диска (.dmg, Apple Silicon)](https://github.com/CutWire-Studios/Drift/releases/latest) |
| Android | [Google Play (закрытый тест)](../docs/play-testing.md) · [APK (arm64-v8a)](https://github.com/CutWire-Studios/Drift/releases/latest) · [APK (armeabi-v7a)](https://github.com/CutWire-Studios/Drift/releases/latest) · [APK (x86_64)](https://github.com/CutWire-Studios/Drift/releases/latest) |

На смартфонах присоединяйтесь к [закрытому тестированию в Google Play](../docs/play-testing.md), либо скачайте файл `Drift-*-arm64-v8a.apk` из последнего релиза и установите его (или выполните команду `adb install Drift-*-arm64-v8a.apk`). Используйте сборку `x86_64` для эмуляторов.

Ознакомьтесь со [всеми релизами](https://github.com/CutWire-Studios/Drift/releases), чтобы просмотреть предыдущие версии и полные списки изменений.

## Скриншот

<p align="center">
  <img src="../docs/screenshots/main-window.png" alt="Редактор Drift: корзина медиа слева, 3D-камера в окне предпросмотра, инспектор текста справа, многодорожечный таймлайн внизу" width="900">
</p>

## Возможности

**ИИ-агент может редактировать открытый проект.** Импортируйте, обрезайте, делайте цветокоррекцию и экспортируйте прямо в активном проекте.
Редактор также умеет работать в фоновом режиме без открытия графического окна.

**3D прямо на таймлайне.** Наклоняйте клипы в пространстве, расставляйте источники света и размещайте титры позади объекта съемки. Перетащите
3D-модель, и она будет воспроизводиться прямо в таймлайне.

**Анимации Lottie.** Векторная графика и анимации сохраняют максимальную четкость в любом масштабе. Меняйте цвета
или текст прямо в проекте.

**Продвинутая система ключевых кадров.** Анимируйте позицию, масштаб, вращение, прозрачность и параметры эффектов во
времени. Рисуйте кривые интерполяции вручную.

**Более 150 переходов и 40+ эффектов.** Мгновенный предпросмотр любого эффекта на ваших исходниках. Одним кликом можно применить
целые связки: Beat Drop, Glitch Cut, Neon Cutout. Шлейфы предыдущих кадров и цветокоррекция, привязанная к конкретному клипу.

**Локальная обработка на вашем компьютере.** Кликните по объекту, чтобы мгновенно отделить его от фона. Добавляйте глубину резкости и световые эффекты,
привязанные к клипу. Автоматические субтитры, апскейлинг видео и ретушь лиц в реальном времени — полностью силами вашего ПК.

**Монтаж по тексту речи.** Речь на видео автоматически расшифровывается в текст. Удаляйте фразы, вырезайте слова-паразиты и тишину, выбирайте
лучшие дубли, помечайте спикеров и создавайте субтитры на основе этих таймкодов.

**Титры с индивидуальным стилем.** 33 готовых пакета: караоке, стиль Hormozi, неон, хром, голография,
рукописный шрифт. Копируйте понравившийся стиль на все субтитры дорожки одним действием.

**Управление связанными слоями.** Один слой трансформации может перемещать, масштабировать, вращать, наклонять и плавно затемнять всё содержимое
под ним. Группируйте дорожки или открывайте композицию, если для блока нужна отдельная временная шкала.

**Монтаж в такт музыке.** Таймлайн автоматически привязывается к ритму. Клипы делятся точно по долям. Музыка
плавно приглушается под голос (audio ducking) и возвращается на свой уровень. Автоматически перекадрируйте горизонтальное видео в вертикальный формат
с отслеживанием лица.

**Таймлайн, работающий предсказуемо.** Многодорожечная лента, монтаж со сдвигом (ripple), магнитное прилипание (snap), маски, стоп-кадр, закладки,
встроенный микшер. При разделении клипа цветокоррекция не сбивается. При сбое программы последнее автосохранение остается в целости.

**Звук студийного качества.** Очищайте речь от шумов, контролируйте громкость (loudness), настраивайте эквалайзер и компрессию, сохраняйте высоту тона
при изменении скорости воспроизведения, записывайте закадровый голос.

**Быстрый поиск сцен. Мультикамерный монтаж.** Ищите кадры по тому, что происходит на экране. Наблюдайте за всеми ракурсами одновременно
и переключайте камеру в нужную секунду.

**Стабилизация, экспорт и переносимость проектов.** Устраняйте тряску камеры, повышайте четкость и плавно воспроизводите тяжелые файлы.
Экспорт в MP4, GIF, отдельную аудиодорожку или заданный диапазон. Упакуйте все медиафайлы вместе с проектом, чтобы пути никогда не терялись.

**Полноценный редактор на Android.** Таймлайн, эффекты и экспорт на смартфоне. Публикуйте готовые ролики прямо из приложения.

**Стоковые медиа, синтез голоса и компактный дистрибутив.** Ищите стоковые кадры прямо из медиа-корзины. Создавайте закадровый голос и звуковые
эффекты. Шрифты, стикеры и дополнительные нейросетевые модели подгружаются по мере использования. Интерфейс переведен на арабский, бенгальский, испанский (Испания
и Колумбия), французский (Канада), итальянский, японский, португальский (Бразилия и Португалия), русский,
сингальский, тагальский, вьетнамский и упрощенный китайский.

## Почему выбирают Drift

Большинство «бесплатных» видеоредакторов навязывают регистрацию, добавляют водяные знаки или требуют платную подписку, как только проект
начинает выглядеть профессионально. Drift устроен иначе: **ваш инструмент на вашем компьютере, открытая лицензия GPLv3, никаких регистраций.**

Он молниеносен для 30-секундных роликов в соцсети и достаточно мощен для сложных проектов — ИИ-ассистент на
таймлайне, 3D и Lottie, генерация субтитров, визуальные эффекты, сведение звука, умный вырез объектов и мультикам.

## Помогите нам перевести Drift

[![Статус перевода](https://hosted.weblate.org/widget/cutwire-drift/drift-desktop/multi-auto.svg)](https://hosted.weblate.org/engage/cutwire-drift/)

## Для разработчиков

Сборка, подготовка пакетов, архитектура и протокол взаимодействия с агентами описаны в папке `docs/`:

- [Сборка, тестирование, пакетирование и архитектура](../docs/BUILDING.md)
- [GPU-эффекты](../docs/gpu-effects.md)
- [GPU-переходы](../docs/gpu-transitions.md)
- [Интеграция с агентами / MCP](../docs/MCP.md)

## Поддержка и обратная связь

Нашли ошибку или есть идея для улучшения? Создайте
[тикет на GitHub](https://github.com/CutWire-Studios/Drift/issues).

## Участие в разработке

Руководство [CONTRIBUTING.md](../CONTRIBUTING.md) описывает процесс создания pull request: открывайте issue перед крупными изменениями, формулируйте одну задачу на один запрос, запускайте тесты и передавайте код под лицензией GPLv3.

Эффекты, переходы, шаблоны и звуковые плагины являются дополнениями. Отправляйте их в репозиторий [Drift-Addons](https://github.com/CutWire-Studios/Drift-Addons). Изменения базового движка размещаются в текущем репозитории.

Установка [официального релиза](https://github.com/CutWire-Studios/Drift/releases) и тестирование на реальных видео — это уже огромный вклад: сообщайте обо всех сбоях. В нашем сообществе [Discord](https://discord.gg/J5ANFz6Z3y) авторы правок могут получить роль `@Contributor`.

## Участники проекта

<a href="https://github.com/CutWire-Studios/Drift/graphs/contributors">
  <img alt="Участники проекта" src="https://contrib.rocks/image?repo=CutWire-Studios/Drift">
</a>

## Лицензия

GPLv3 — см. [LICENSE](../LICENSE).

## История популярности (Star History)

<a href="https://www.star-history.com/?repos=CutWire-Studios%2FDrift&type=date&legend=top-left">
 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/chart?repos=CutWire-Studios/Drift&type=date&legend=top-left&sealed_token=zHW_d2jon9Wn-HYP2SWQWLC7qDRaY7qwsvHS0Cp0Ywk1Rf1UvyxhWsakIrx2c11OijPJQ9o52W99jdigV7MOz5RuvLsyWQmBiMvMdk99mcfbgb591WtzNXQO8_K2YhgdbiPD9by00lwl69ZgCZnThFKBwhRbK7IQzIeFkIFnb2o0r5GhJh0HAX6Q8yTM" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/chart?repos=CutWire-Studios/Drift&type=date&legend=top-left&sealed_token=zHW_d2jon9Wn-HYP2SWQWLC7qDRaY7qwsvHS0Cp0Ywk1Rf1UvyxhWsakIrx2c11OijPJQ9o52W99jdigV7MOz5RuvLsyWQmBiMvMdk99mcfbgb591WtzNXQO8_K2YhgdbiPD9by00lwl69ZgCZnThFKBwhRbK7IQzIeFkIFnb2o0r5GhJh0HAX6Q8yTM" />
   <img alt="График звезд Star History" src="https://api.star-history.com/chart?repos=CutWire-Studios/Drift&type=date&legend=top-left&sealed_token=zHW_d2jon9Wn-HYP2SWQWLC7qDRaY7qwsvHS0Cp0Ywk1Rf1UvyxhWsakIrx2c11OijPJQ9o52W99jdigV7MOz5RuvLsyWQmBiMvMdk99mcfbgb591WtzNXQO8_K2YhgdbiPD9by00lwl69ZgCZnThFKBwhRbK7IQzIeFkIFnb2o0r5GhJh0HAX6Q8yTM" />
 </picture>
</a>
