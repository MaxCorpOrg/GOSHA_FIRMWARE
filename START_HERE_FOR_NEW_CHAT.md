# START HERE FOR NEW CHAT

Короткий вход для следующего агента в `GOSHA_FIRMWARE`.

## Самая свежая статическая точка

- `2026-09-05` follow-up по принятому review PR51 выполнен в worker поверх
  exact base `502678170e5525774f5663b1533b4d4fa915ba17` ветки
  `ai-office/coder/issue-50-firmware-pr48-privacy-safe-voice-turn-phase-emission`.
  Исходная точка PR48 остаётся отдельной: prepared head
  `f1fdb89d508217134ccc91c03358e43c9809137c`. Worker не создавал `commit`,
  `push`, `rebase` или PR; текущий результат — незакоммиченный scoped diff для
  центрального workflow.
- Закрыт P1 review PR51 по drain, то есть ожиданию полного опустошения
  аудиовывода: `AudioOutputTask` и `OpusCodecTask` теперь учитывают in-flight
  decode/output через общий `AudioPlaybackDrainTracker`. `WaitForPlaybackQueueEmpty()`
  больше не считает playback завершённым, если очереди уже пустые, но decode или
  `codec_->OutputData(...)` ещё выполняются. `ResetDecoder()` и `Stop()`
  отменяют поколение queued playback, а `ResetDecoder()` дополнительно ждёт
  завершения in-flight работ перед сбросом декодера. Это не даёт старым
  decode/output после reset поставить звук обратно в очередь или вызвать
  `on_remote_audio_output`.
- Добавлен поведенческий host regression test
  `firmware/scripts/audio_playback_drain_host_test.cc` и runner
  `firmware/scripts/run_host_behavior_tests.py`. Тест проверяет empty queue при
  in-flight decode/output, порядок `on_remote_audio_output` до drain и teardown
  при stop на реальных `std::mutex` и `std::condition_variable`, без grep и без копии
  production-реализации.
- Добавлен активный CI workflow
  `.github/workflows/firmware-stacked-ci.yml` для промежуточного stacked PR, то
  есть PR поверх предыдущей ветки. Он запускает no-motion/OTA/reboot/assets
  guard, voice guard, остальные firmware guards, host behavioral tests и
  каноническую сборку `gosha-v1` в контейнере ESP-IDF `5.5.2` с
  `GOSHA_OTA_URL=https://example.invalid/gosha-v1/ota`; production endpoint и
  secrets не нужны и не печатаются. Существующий imported workflow
  `firmware/.github/workflows/build.yml` синхронизирован с теми же guard-ами,
  stacked PR base и reserved `example.invalid`.
- Проверки на worker без устройства: pin map guard, GPIO3/audio guard,
  no-motion guard, voice guard, sensitive logging guard, `py_compile`,
  `scripts/release.py --list-boards --json`. Каноническая команда с
  `example.invalid` дошла до static guards и остановилась на `idf.py: not found`;
  C++ host runner остановился на отсутствии компилятора. Поэтому worker не
  объявляет локальную host binary или firmware build успешными. USB/serial,
  flash, reboot, update, motion, `Home`, `set_trim`, servo sequence, deployment,
  iOS и safe-neutral профиль не трогались.

- `2026-09-04` PR48 voice turn phase emission подготовлен статически поверх
  prepared head `f1fdb89d508217134ccc91c03358e43c9809137c`: прошивка
  публикует `event_type="voice.turn.phase"` в схеме
  `gosha.runtime.event.v1` только на доказанных локальных границах голосового
  turn, то есть одного цикла речи пользователя и ответа робота.
- Фазы: `wake_detected` при обработке обнаруженного слова пробуждения,
  `user_speech_start` и `user_speech_end` из VAD, то есть обнаружения речи в
  аудиопроцессоре, `robot_first_audio_out` только после первого фактического
  `codec_->OutputData(...)` для аудио от сервера. Локальные `OGG`-сигналы и
  тестовое аудио помечены как `AudioStreamSource::kLocal` и не запускают
  `robot_first_audio_out`.
- На один turn генерируются ограниченные по длине,
  не секретные `correlation_id` и `task.id` из `esp_random()` и локального
  счётчика; они не используют `MAC`, `IP`, raw device id, серверный
  `session_id`, URL, token, SSID, transcript, prompt или raw audio. В объекте
  `voice` есть только `phase` и `warm_state`; для первого аудиовыхода источник
  события имеет `source.kind="robot"`. `turn_failed` публикуется только при
  активном turn и отказе открытия/сети голосового канала; `turn_complete`
  намеренно не добавлен, потому что прошивка без нового протокольного
  подтверждения не доказывает полный конец серверного turn.
- Добавлена статическая проверка
  `firmware/scripts/check_voice_turn_phase_events.py --self-test`; она
  проверяет одноразовый первый аудиовыход, сброс turn, отсутствие
  чувствительных данных и сохранение no-motion инвариантов. `release.py`
  запускает эту проверку для `gosha-v1` до owner-only `GOSHA_OTA_URL`.
- Проверки без устройства прошли: новая проверка voice turn с `--self-test`, no-motion
  guard, pin map guard, GPIO3/audio guard, sensitive logging guard,
  `py_compile`, `scripts/release.py --list-boards --json`, `git diff --check`.
  `scripts/release.py gosha-v1 --name gosha-v1` дошёл через все статические проверки и
  ожидаемо остановился на отсутствующем owner-only `GOSHA_OTA_URL`. Compile
  smoke, то есть пробная сборка, не выполнялся: в контейнере и доступном
  host-пути не найден `idf.py` или `ESP-IDF export.sh`. USB/serial, flash,
  reboot, update, raw
  `:8080/ws`, motion, `Home`, `set_trim`, servo sequence, OTA/assets writes,
  live endpoint и credentials не использовались.
- При закрытии аудиоканала reset голосового turn выполняется только после
  `audio_service_.WaitForPlaybackQueueEmpty()`, чтобы уже поставленное в очередь
  удалённое аудио не потеряло свой первый фактический `codec_->OutputData(...)`.
- `2026-09-04` дополнение Issue `#47` для Draft PR `#44` закрывает оставшийся
  риск живой записи ресурсов в no-motion профиле. При
  `CONFIG_GOSHA_NO_MOTION_SAFE_PROFILE=y` MCP-инструмент
  `self.assets.set_download_url` не регистрируется, прямой `tools/call`
  отклоняется до поиска инструмента и `Schedule()`, `CheckAssetsVersion()` не
  открывает `assets/download_url` на запись и не входит в ветку загрузки, а
  `Assets::Download()` возвращает отказ до HTTP, `UnApplyPartition()`,
  `esp_partition_erase_range()`, `esp_partition_write()` и повторной
  инициализации раздела. Уже установленный локальный assets-раздел по-прежнему
  применяется.
- Обслуживание assets остаётся внешней процедурой владельца: отдельный
  аппаратный preflight, то есть предварительная проверка, backup, verify и
  rollback. Удалённой живой заменой ресурсов это больше не делается.

## Самая свежая аппаратная точка

- Фактический текущий head Draft PR `#33` не фиксируется в этом файле:
  перед работой проверяй его через
  `gh pr view 33 --repo MaxCorpOrg/GOSHA_FIRMWARE --json headRefOid`.
- Первый docs-policy follow-up этой ветки:
  `565c4213a1a5dea16193faca0458daea250103f7`; это lineage документации поверх
  уже установленного app-only кандидата. Новый flash для docs-only head не
  выполнялся и не требуется.
- `2026-09-03` из `codex/noncamera-pinmap-ledc-fix-20260903 @
  c81d24c941be8cadd6a96c9bbddd2884bf5906ae` выполнена свежая canonical
  сборка `gosha-v1` на ESP-IDF `5.5.2` с owner-only `GOSHA_OTA_URL` без
  вывода значения. `gosha.bin` — `3654400` байт, SHA-256
  `603b1609615a530ff9b138bcfad9d73cf3d01ddee352d1483e17044a2694dd41`,
  свободно 11% app-раздела.
- Перед записью подтверждены `ESP32-S3` rev `v0.2`, flash `16MB`, `3.3V`,
  rollback: полный backup `16 MB` mode `600` с SHA-256
  `c3dee211b4b66d49500447bfc9cf66d97e7ec65f9d631da76ecb0c13249d594a` и
  предыдущий app rollback image `3654384` байта с SHA-256
  `78fe6c115a44fa4e9f40b0e990c3e6162e1acfc8cdac5ff1b10ed1bf628d5764`.
- Partition table на устройстве совпала с build
  (`4811619cacae08ef2e0e71b7220c6033a346ca5da7ca179082408c963ef530b5`),
  assets тоже совпали
  (`12520722b9a56c0b687d072cb668e2f0ede0260b3a7fdef365abe81015231516`).
  Записан только app-раздел `0x20000`; NVS, `otadata`, bootloader, partition
  table и assets не изменялись. `write_flash` и отдельный `verify_flash`
  прошли, rollback не потребовался.
- Короткий serial smoke после установки подтвердил `gosha 2.2.2`, ESP-IDF
  `5.5.2`, 8 MB PSRAM, Wi-Fi, OTA/config, runtime events, локальный
  `WebSocket` `8080`, threshold `0.380000` и переход в `idle`. Panic,
  watchdog, brownout и reset loop не наблюдались; был один ожидаемый reset от
  monitor. Внешние motion, `Home`, `set_trim`, servo sequence и raw WS-команды
  не отправлялись. Servo LEDC warnings по `GPIO8/12/17/18/38/39` исчезли;
  отдельно остались warning по `GPIO3` backlight и sample-rate warning
  `16000/24000`. Wake/voice не форсировались и в пассивном окне не
  наблюдались.
- `2026-09-03` из `codex/hardware-development-enabled-20260903 @ e3fa25c0e55a`
  выполнена свежая canonical сборка `gosha-v1`; продуктовый код соответствует
  статически принятому `a8326d6818cb1ed72db8a5cc00c00b5366f270b8`.
- Перед записью снят полный 16-MB backup вне Git с правами `600`. Новый
  `gosha.bin` (`3654384` байт) записан только по `0x20000`; NVS, `otadata`,
  bootloader, partition table и совпадающие assets не изменялись. Write hash и
  отдельный `verify_flash` прошли.
- Live smoke подтвердил загрузку `gosha 2.2.2` / ESP-IDF `5.5.2`, 8 MB PSRAM,
  non-camera, Wi-Fi, platform/OTA/runtime events, `:8080/ws`, read-only
  `gosha.identity.get/result`, threshold `0.380000`, wake word и голосовой
  диалог без panic/watchdog/reset loop.
- После прогрева измерено `1.990 s` и `1.280 s` от готового ASR-текста до
  первого TTS-текста. Холодный старт WebSocket после wake занял `2.620 s`, а
  первый полный turn до TTS — `7.200 s`. Осталось предупреждение о
  `16000/24000` sample-rate. LEDC warnings объясняются повторным Attach и не
  требуют отката, но non-camera config делит `GPIO12` между правой рукой и
  `display_cs`; pin map требует отдельного физического подтверждения или
  исправления без motion-команд.
- `2026-08-27` статический remediation-gate ветки `feature/firmware-orange-eyes` опубликован в Draft PR `#24` на `7751a3ca326174d217536f6a8de7c09433c3e955`: hardcoded relay удалён из OTA-default и документации, production-сборка требует owner-only `GOSHA_OTA_URL`, а вывод значения редактируется.
- Удалены чувствительные значения из Wi-Fi/activation-логов; `self.otto.stop` теперь только останавливает текущую задачу и очищает очередь, не ставя новый `ACTION_HOME`; vendored-компонент приведён к чистому `git diff --check`.
- Каноническая статическая сборка с неразрешимым тестовым endpoint `.invalid` прошла: `gosha.bin` — `3652384` байт, свободно 12% app-раздела. Этот артефакт не предназначен для установки.
- Immutable AI Office task `task-20260827T104756Z-immutable-terminal-firmware-pr-24-gate-at-7751a3c` на фактическом профиле `GPT-5.5 / xhigh` дала terminal `PASS`: P0/P1/P2 нет, точный remote/PR head подтверждён. Служебный upstream symlink удалён, а snapshot с исходниками и документацией принят secure exporter.
- Статический firmware gate закрыт. Неисправная левая серва физически
  отключена; с `2026-09-03` USB/serial, flash, update, power-cycle и
  перезагрузка разрешены только по `docs/HARDWARE_DEVELOPMENT_POLICY_RU.md`:
  no-motion preflight, backup, verify и rollback обязательны. Motion, `Home`,
  `set_trim` и servo sequence остаются закрыты до отдельной команды. Draft PR
  не сливать без общей проверки.

## Предыдущая аппаратная точка 2026-08-25

- `2026-08-25` временный сетевой канал принят как рабочий обход: `TEMP_NL_RELAY` временно прокидывает роботу путь к `PRIMARY_PLATFORM_SERVER`, а не заменяет основной сервер.
- Роли портов фиксируются логическими именами без IP-адресов, секретов и хардкода в прошивке: `18876` — HTTP-контур панели, OTA и config; `18080` — голосовой `WebSocket` и совместимый `MCP`.
- В конце месяца канал нужно перенести с `TEMP_NL_RELAY` на `FUTURE_PRODUCTION_SERVER`, после чего повторить короткую сетевую проверку.
- Новый физический робот подтверждён как `gosha-main`: Android показывает `Гоша Main`, робот разговаривает, голосовой сценарий принят.
- Firmware quality gate на `feature/firmware-orange-eyes @ 07d5f6658b6c70c81626ebcc3fbee930ced94fc6`: на `2026-08-25` Draft PR был допустим, а merge и установка были остановлены по конкретным причинам: в старых документах `self.otto.stop` был указан как безопасная проверка, хотя он запускает возврат в `Home`; `git diff --check` падал на хвостовых пробелах в vendored-компоненте; существующий release ZIP содержал устаревший merged-образ.
- `2026-08-23` новый робот после полной резервной копии переведён с несовместимой заводской разметки на полный образ `gosha-v1` из `feature/firmware-orange-eyes @ 80310104e895d02d648364460e82d0c2b31e8ba8`.
- Запись, отдельный `verify_flash` и первый контролируемый запуск успешны.
- Прошивка стабильна; левый канал `GPIO8` подтверждён перекрёстным тестом.
- Физический сервопривод левой руки отключён от платы и должен оставаться
  отключённым до ремонта или замены. Это не является общим блокером для
  flash, reboot, update и неподвижных проверок, если выполнены условия
  аппаратной политики: no-motion preflight, backup, verify и rollback.
  Движения, `Home`, `set_trim` и servo sequence требуют отдельной команды.
- Исторический ближайший шаг из этой точки уже закрыт PR `#33`: кандидат
  собран, установлен app-only и проверен коротким serial smoke. Следующий
  firmware-шаг без нового hardware-window — разбирать оставшиеся warning
  `GPIO3` backlight и sample-rate `16000/24000` без команд движения.

## Сначала прочитать

1. `AGENTS.md`
2. `/home/max/GOSHA_PLATFORM/docs/GOSHA_PROJECT_MAP_RU.md`
3. `docs/NEW_CHAT_CHECKPOINT_RU.md`
4. `docs/AGENT_CHECKPOINT_RU.md`
5. `docs/PROJECT_STATUS_RU.md`
6. `docs/FIRMWARE_IMPORT_CHECKPOINT_RU.md`
7. `docs/HARDWARE_MANIFEST_RU.md`
8. `docs/PIN_MAP_RU.md`
9. `docs/HARDWARE_DEVELOPMENT_POLICY_RU.md`

## Как писать новые записи

- Все новые контрольные точки, планы, отчёты и пояснения оформляй русским техническим языком по правилам из `AGENTS.md`.
- Английский оставляй только для команд, путей, имён файлов, веток, коммитов и кода.
- Если встречается технический термин, который может быть непонятен оператору, сразу поясняй его смысл по-русски.

## Обязательный старт нового чата

Перед тем как предлагать работу, агент обязан:

1. Прочитать этот файл и актуальные checkpoint-документы.
2. Выполнить `git status --short --branch`.
3. Выполнить `git log --oneline -1`.
4. Если задача затрагивает общий контур `Гоша`, свериться со свежим входным файлом на рабочем столе.
5. В первом содержательном ответе явно сообщить:
   - текущую ветку;
   - текущую стадию;
   - следующий приоритетный шаг.

## Текущая идея проекта

- `GOSHA_FIRMWARE` — отдельный репозиторий собственной прошивки `Гоша`.
- Репозиторий не должен становиться продолжением `AI_ROBOT`; `AI_ROBOT` используется только как источник одноразового импорта и справки.
- Исходная база уже импортирована в:
  - `/home/max/GOSHA_FIRMWARE/firmware`
- Первая целевая плата:
  - `gosha-v1`
  - на основе `otto-robot`
- Рабочая локальная среда сборки уже поднята:
  - `/home/max/esp/esp-idf-v5.5.2`
- Первый merged-образ уже собран:
  - `/home/max/GOSHA_FIRMWARE/firmware/build/merged-binary.bin`
- Прошивка проектируется как часть масштабируемой платформы `Гоша`:
  - много роботов;
  - много профилей ИИ-агентов;
  - много OpenAI-совместимых провайдеров на стороне платформы;
  - без жёсткой привязки к одному облаку или одной модели.
- Если нужно быстро понять, где серверный голос, панель и мобильный клиент, смотри:
  - `/home/max/GOSHA_PLATFORM/docs/GOSHA_PROJECT_MAP_RU.md`
