# NEW CHAT CHECKPOINT

## Точность времени голосовых событий: 2026-09-05

- Кандидат продолжает опубликованный Draft PR #54 на `c6d01266bb9f87e8bd2f44576eb50fe967972759`. Исходная база голосовых событий — PR #48 `f1fdb89d508217134ccc91c03358e43c9809137c`; сами события добавлены PR #51 `502678170e5525774f5663b1533b4d4fa915ba17`, исправление ожидания аудио — PR #54. Эти версии не следует смешивать с установленным образом.
- `occurred_at` теперь сохраняет миллисекунды из `gettimeofday()` через общий `FormatRuntimeEventTime()`. Прежнее округление до секунды искажало задержку: реальные 1900 мс могли выглядеть как 2000 мс. Поведенческий тест вызывает производственный форматировщик, проверяет доли секунды, переход секунды и отсутствие установленного времени.
- Если часы устройства не установлены, время возникновения события не выдумывается. Подстановка сервером времени получения не считается доказательством точной задержки: при живой приёмке нужны установленные часы, отсутствие скачка времени и внешняя сверка начала звука. Открытый аудиоканал также не доказывает прогрев серверной модели; условия cold/warm записываются отдельно.
- CI запускается для PR с любой базовой веткой, проверяет host-тесты и сохраняет проверочный app-образ с SHA-256. Его `example.invalid` предназначен только для сборочной проверки; этот образ не является настроенной прошивкой для устройства.
- Изменений аппаратного профиля нет. На физическом роботе остаётся отдельный safe-neutral образ от `baf0323d17db9f0635cf9ae64d79eb6f3c19af2c`; обе руки программно `NC`, левая физически отключена. Поза, гул и нагрев ещё требуют подтверждения владельца. До него flash/reset/motion не выполняются.


> Актуализация `2026-09-03`: аппаратная разработка разблокирована по
> `docs/HARDWARE_DEVELOPMENT_POLICY_RU.md`; прежние запреты ниже являются
> историческими.

## Voice drain follow-up PR51 2026-09-05

- Текущий fixer-task `task-20260905-gosha-firmware-voice-drain-ci` выполнен в
  worker поверх exact base
  `502678170e5525774f5663b1533b4d4fa915ba17` ветки
  `ai-office/coder/issue-50-firmware-pr48-privacy-safe-voice-turn-phase-emission`.
  Это не новый опубликованный `commit`: worker не выполнял `commit`, `push`,
  `rebase` или PR. Центральный workflow должен принять незакоммиченный scoped diff.
- Исходная PR48-точка остаётся отдельным фактом:
  `f1fdb89d508217134ccc91c03358e43c9809137c` предшествует добавлению
  `voice.turn.phase` в PR51. PR51 review нашёл P1 в drain-координации и P2 в
  документации, где нельзя было смешивать исходный PR48/PR51 и текущий результат
  как будто у него уже есть будущий самоссылочный `commit`.
- P1 закрыт в `AudioService`: введён `AudioPlaybackDrainTracker`, который под
  тем же `audio_queue_mutex_` учитывает in-flight decode и output. При
  извлечении последнего decode/output элемента очередь может стать пустой, но
  `WaitForPlaybackQueueEmpty()` теперь ждёт завершения самой работы и
  `on_remote_audio_output` до сброса voice turn. `ResetDecoder()` и `Stop()`
  отменяют поколение queued playback, а `ResetDecoder()` дополнительно ждёт
  завершения in-flight работ перед сбросом декодера. Это закрывает старую работу
  после сброса.
- Добавлены `firmware/scripts/audio_playback_drain_host_test.cc` и
  `firmware/scripts/run_host_behavior_tests.py`. Host test проверяет реальные
  `std::mutex` и `std::condition_variable`: пустую очередь при in-flight decode,
  пустую очередь при in-flight output, `on_remote_audio_output` до drain,
  reset старых decode/output и teardown через stop.
- Добавлен активный workflow `.github/workflows/firmware-stacked-ci.yml` для
  промежуточного stacked PR, то есть PR поверх предыдущей ветки. CI запускает
  no-motion guard, который удерживает OTA/reboot/assets запреты, voice guard,
  pin map, GPIO3/audio, sensitive logging, host behavioral tests и
  каноническую сборку `gosha-v1` в `espressif/idf:v5.5.2` с
  `GOSHA_OTA_URL=https://example.invalid/gosha-v1/ota`. Существующий imported
  workflow `firmware/.github/workflows/build.yml` синхронизирован с теми же
  guard-ами, stacked PR base и reserved `example.invalid`.
- Проверки worker без устройства прошли: `python3 scripts/check_gosha_v1_pinmap.py
  --self-test`, `python3 scripts/check_gosha_v1_gpio3_audio_contract.py
  --self-test`, `python3 scripts/check_gosha_v1_no_motion_profile.py
  --self-test`, `python3 scripts/check_voice_turn_phase_events.py --self-test`,
  `python3 scripts/check_sensitive_logging.py`, `python3 -m py_compile ...`,
  `python3 scripts/release.py --list-boards --json`. Команда
  `GOSHA_OTA_URL=https://example.invalid/gosha-v1/ota python3 scripts/release.py
  gosha-v1 --name gosha-v1` прошла guards и остановилась на отсутствии `idf.py`;
  `python3 scripts/run_host_behavior_tests.py` остановился на отсутствии
  компилятора C++. Worker не объявляет локальный исполняемый host-тест или
  firmware build успешными.
- Аппаратные действия не выполнялись: USB/serial, flash, reboot, update,
  motion, `Home`, `set_trim`, servo sequence, deployment, iOS и safe-neutral
  профиль вне этой задачи.

## Voice turn phase emission PR48 2026-09-04

- Статическая правка prepared head
  `f1fdb89d508217134ccc91c03358e43c9809137c` добавляет прошивочную часть
  контракта платформы `gosha.runtime.event.v1` с
  `event_type="voice.turn.phase"`.
- Реальный голосовой путь зафиксирован так: wake word, то есть слово
  пробуждения, приходит из `AudioService` в `Application`;
  `user_speech_start` и `user_speech_end` берутся только из VAD, то есть
  обнаружения речи аудиопроцессором; серверный `tts start` лишь переводит
  устройство в `kDeviceStateSpeaking`; фактический звук подтверждается только
  в `AudioService::AudioOutputTask()` после первого `codec_->OutputData(...)`
  для удалённого аудиопакета.
- Прошивка публикует только доказанные фазы: `wake_detected`,
  `user_speech_start`, `user_speech_end`, `robot_first_audio_out` и
  `turn_failed` при отказе открытия/сети голосового канала с активным turn.
  `turn_complete` не добавлен, потому что текущий протокол не даёт прошивке
  физически точной границы полного завершения серверного turn.
- На один turn создаются стабильные ограниченные по длине `correlation_id` и `task.id`.
  Источник — `esp_random()` и локальный счётчик, а не `MAC`, `IP`, raw device
  id, серверный `session_id`, URL, token, SSID, transcript, prompt или raw
  audio. В объекте `voice` остаются только `phase` и `warm_state`;
  `robot_first_audio_out` отправляется с `source.kind="robot"`.
- Локальные звуки `PlaySound()` и тестовое аудио помечены
  `AudioStreamSource::kLocal`, поэтому они не могут вызвать
  `robot_first_audio_out`; аудиопакеты протокола остаются
  `AudioStreamSource::kRemote`.
- На закрытии аудиоканала reset turn идёт после
  `audio_service_.WaitForPlaybackQueueEmpty()`, чтобы уже поставленное в очередь
  удалённое аудио не потеряло первый фактический `codec_->OutputData(...)`.
- Добавлен `firmware/scripts/check_voice_turn_phase_events.py --self-test` и
  подключён к `scripts/release.py` для `gosha-v1` до чтения owner-only
  `GOSHA_OTA_URL`. Проверка покрывает одноразовый первый аудиовыход, сброс turn,
  отсутствие чувствительных полей и сохранение no-motion guard.
- Проверки без устройства: `python3 scripts/check_voice_turn_phase_events.py
  --self-test`, `python3 scripts/check_gosha_v1_no_motion_profile.py
  --self-test`, `python3 scripts/check_gosha_v1_pinmap.py --self-test`,
  `python3 scripts/check_gosha_v1_gpio3_audio_contract.py --self-test`,
  `python3 scripts/check_sensitive_logging.py`, `python3 -m py_compile ...`,
  `python3 scripts/release.py --list-boards --json`, `git diff --check`.
  `python3 scripts/release.py gosha-v1 --name gosha-v1` прошёл все статические
  проверки и ожидаемо остановился на отсутствующем `GOSHA_OTA_URL`.
- Compile smoke, то есть пробная сборка, не выполнялся: в контейнере и
  доступном host-пути нет `idf.py` и `ESP-IDF export.sh`. Аппаратные действия
  не выполнялись: USB/serial,
  flash, reboot, update, raw `:8080/ws`, motion, `Home`, `set_trim`, servo
  sequence, OTA/assets writes, live endpoint и credentials не использовались.

## No-motion профиль `gosha-v1` 2026-09-04

- Ветка `codex/firmware-no-motion-safe-profile-20260904` от exact
  `4ce2cab34cc222d8624e97e7ddb62a6365e1c231` добавляет fail-closed профиль
  безопасной неподвижной работы для текущего release-кандидата `gosha-v1`.
- В `config.json` профиль явно включает
  `CONFIG_GOSHA_NO_MOTION_SAFE_PROFILE=y`; в `Kconfig.projbuild` этот символ
  разрешён только для `BOARD_TYPE_GOSHA_V1` и по умолчанию выключен, чтобы
  возврат движения требовал отдельного осознанного изменения после аппаратного
  допуска.
- В активном no-motion профиле `Otto::Init()` сохраняет карту выводов и
  подстройки, но не вызывает `AttachServos()`, boot `ACTION_HOME` не ставится в
  очередь, а очередь `QueueAction()` и `QueueServoSequence()` имеет отдельный
  fail-closed guard.
- В no-motion сборке не регистрируются MCP-инструменты движения:
  `self.otto.action`, `self.otto.servo_sequences`, `self.otto.stop`,
  `self.otto.set_trim` и `self.otto.get_trims`. Снаружи остаются безопасные
  read-only инструменты `self.otto.get_status`, `self.battery.get_level` и
  `self.otto.get_ip`; голос, `Wi-Fi`, OTA/config, runtime events и локальный
  `WebSocket` остаются доступны.
- Дополнение PR `#37` `2026-09-04` закрывает найденное замечание reviewer P1:
  локальный `WebSocket` может передать MCP-сообщение, но при
  `CONFIG_GOSHA_NO_MOTION_SAFE_PROFILE=y` общий инструмент с пометкой `user_only`
  `self.upgrade_firmware` больше не регистрируется и прямой `tools/call` с этим
  именем отклоняется в `McpServer::DoToolCall()` до любых побочных эффектов
  OTA. Контролируемая владельцем запись только app-раздела через serial остаётся
  отдельной аппаратной процедурой с backup, verify и rollback и этим запретом не
  ломается.
- Дополнение PR `#40` `2026-09-04` закрывает замену прошивки во время работы:
  `CheckNewVersion()` при активном no-motion профиле не входит в
  `Application::UpgradeFirmware()`, а сама `Application::UpgradeFirmware()`
  немедленно возвращает отказ до действий с `display`, `protocol` и `audio`,
  смены `state`, загрузки образа, записи flash-памяти и reboot. Проверки
  версии, активации и конфигурации остаются доступными.
- Удалённое MCP-действие `self.reboot` при no-motion профиле не регистрируется и
  прямой `tools/call` с этим именем отклоняется до поиска инструмента,
  `Schedule()` и `Application::Reboot()`. Локальный reboot, выполненный
  владельцем после serial app-only flash, остаётся аппаратной процедурой, а не
  MCP-командой.
- Серверная команда `system.command == "reboot"` также отклоняется до
  `Schedule()` и `Application::Reboot()`.
- Дополнение Issue `#47` для PR `#44` закрывает живую замену assets-раздела в
  no-motion профиле: `self.assets.set_download_url` не регистрируется, прямой
  `tools/call` отклоняется до поиска инструмента и `Schedule()`,
  `CheckAssetsVersion()` возвращается до открытия `assets/download_url` на
  запись, уведомлений, смены состояния, повышения питания, `assets.Download()`
  и планирования обновления прогресса через `Schedule()`, а
  `Assets::Download()` возвращает отказ до HTTP, `UnApplyPartition()`,
  стирания/записи flash-памяти и повторной инициализации раздела. Локально
  установленный assets-раздел всё ещё применяется.
- Добавлен static guard
  `firmware/scripts/check_gosha_v1_no_motion_profile.py --self-test`; он
  проверяет включение профиля в release-конфигурации, Kconfig-зависимость,
  отсутствие servo attach на boot, защиту boot Home, закрытые MCP-инструменты,
  ранний отказ `Application::UpgradeFirmware()`, список разрешённых вызывающих мест,
  закрытие `self.upgrade_firmware`, `self.reboot` и
  `self.assets.set_download_url` в общем MCP-слое, раннее закрытие
  `CheckAssetsVersion()` и `Assets::Download()`, список разрешённых вызывающих
  мест `Assets::Download()` и release hook.
  `release.py` запускает guard вместе с pin map,
  GPIO3/audio-contract и sensitive logging проверками до owner-only
  `GOSHA_OTA_URL`.
- Проверки без устройства прошли: новый no-motion guard с `--self-test`,
  pin map guard с `--self-test`, GPIO3/audio-contract guard с `--self-test`,
  sensitive logging guard, `py_compile`, прямой no-motion guard,
  `git diff --check`,
  `scripts/release.py --list-boards --json` с подтверждённым `gosha-v1`.
  Канонический `scripts/release.py gosha-v1 --name gosha-v1` дошёл до всех
  static guards и ожидаемо остановился на отсутствующем owner-only
  `GOSHA_OTA_URL`.
- Non-canonical compile smoke на ESP-IDF `5.5.2` в `/tmp` прошёл до
  `Project build complete`: временный `sdkconfig` содержал
  `CONFIG_GOSHA_NO_MOTION_SAFE_PROFILE=y`, `gosha.bin` — `3629088` байт,
  SHA-256 `7595328ab0a12ed65a8e68f4d5ae08e521bf641cd88264cd5699bfdd7cde1af1`,
  свободно 12% app-раздела. Этот образ не является release-артефактом для
  установки.
- Это статическая firmware-правка без устройства: USB/serial, flash, reboot,
  update, raw `:8080/ws`, motion, `Home`, `set_trim`, servo sequence и operator
  gateway не выполнялись. Перед установкой нужен отдельный no-motion
  hardware-window с backup/verify/rollback. Любое будущее обслуживание assets
  остаётся такой же внешней процедурой владельца, а не удалённой живой командой.

## Статическая правка GPIO3/audio contract 2026-09-04

- Ветка `codex/firmware-gpio3-samplerate-20260904` от exact
  `0d7248ea08f17dad270fe6e5eee219a00f1f10d5` закрывает два остатка smoke без
  обращения к устройству: `GPIO3` warning подсветки после неуспешного camera
  probe и неоднозначный sample-rate contract `16000/24000`.
- По локальному ESP-IDF `5.5.2` подтверждено: `ledc_channel_config()` через
  `_ledc_set_pin()` резервирует GPIO, а `ledc_stop()` только гасит LEDC-сигнал
  и не снимает reservation. Публичный `gpio_reset_pin()` внутри ESP-IDF
  вызывает `esp_gpio_revoke(BIT64(gpio_num))`, поэтому `gosha-v1` теперь
  освобождает `CAMERA_XCLK` через `ReleaseCameraProbePwm()`:
  `ledc_stop()` плюс `gpio_reset_pin(CAMERA_XCLK)`. Приватный
  `esp_gpio_revoke()` в код платы не добавлен.
- `hello.audio_params` для WebSocket и MQTT теперь строится общим helper-ом:
  legacy `sample_rate` остаётся `16000`, дополнительно явно передаются
  `input_sample_rate=16000`, `uplink_sample_rate=16000` и
  `output_sample_rate=<codec output>`. Для `gosha-v1` non-camera это сохраняет
  вход/исходящий Opus на `16000` и сообщает фактический вывод кодека `24000`.
- Предупреждение `Server sample rate ... does not match device output...`
  заменено на информационный лог контракта, потому что `16000 -> 24000` является
  ожидаемым ресемплингом, а не самостоятельной причиной отказа. Ошибки создания
  ресемплера по-прежнему логируются в `AudioService`.
- Добавлен static guard
  `firmware/scripts/check_gosha_v1_gpio3_audio_contract.py --self-test`; он
  проверяет освобождение `GPIO3`, явные audio fields, сохранение legacy
  `sample_rate=16000` и negative-regression cases. `release.py` для
  `gosha-v1` запускает этот guard вместе с pin map и sensitive logging guard до
  owner-only сборочных параметров.
- Проверки: новый guard и self-test прошли; `check_gosha_v1_pinmap.py
  --self-test` прошёл; `check_sensitive_logging.py` прошёл; `py_compile`
  нового guard и `release.py` прошёл; `git diff --check` прошёл; ESP-IDF
  окружение подтверждено как `v5.5.2`; non-canonical compile smoke для
  `esp32s3`/`gosha-v1` в `/tmp` прошёл до `Project build complete`
  (`gosha.bin` `0x37c4c0`, 11% свободно в app partition). Canonical
  `scripts/release.py gosha-v1 --name gosha-v1` дошёл через static guards и
  ожидаемо остановился на отсутствующем owner-only `GOSHA_OTA_URL`, поэтому
  release-образ не заявляется.
- USB/serial, flash, reboot, update, raw `:8080/ws`, motion, `Home`,
  `set_trim`, servo sequence и operator-command-gateway не выполнялись.
  Исчезновение live warning по `GPIO3` ещё требует отдельного no-motion
  hardware-window.

## Локальная pin map/LEDC правка 2026-09-03

- Ветка `codex/noncamera-pinmap-ledc-fix-20260903` от `70a9884` убирает
  конфликт `GPIO12`: `right_hand_pin` остаётся `GPIO_NUM_12`, а
  `display_cs_pin` в `NON_CAMERA_VERSION_CONFIG` становится `GPIO_NUM_NC`.
- Повторный `AttachServos()` удалён только из `ActionTask`; `Otto::Init()` и
  boot `ACTION_HOME` сохранены. Новый статический guard
  `firmware/scripts/check_gosha_v1_pinmap.py` ловит дубли активных non-NC GPIO.
- Live rollout этой же ветки выполнен app-only на точном
  `c81d24c941be8cadd6a96c9bbddd2884bf5906ae`. Fresh canonical build ESP-IDF
  `5.5.2`: `gosha.bin` — `3654400` байт, SHA-256
  `603b1609615a530ff9b138bcfad9d73cf3d01ddee352d1483e17044a2694dd41`;
  `generated_assets.bin` и partition table совпали с устройством, поэтому
  записан только `0x20000`. `write_flash` и отдельный `verify_flash` прошли;
  NVS, `otadata`, bootloader, partition table и assets не менялись.
- Serial smoke подтвердил boot `gosha 2.2.2`, ESP-IDF `5.5.2`, 8 MB PSRAM,
  Wi-Fi, OTA/config, runtime events, локальный `WebSocket` `8080`, threshold
  `0.380000` и `idle` без panic/watchdog/brownout/reset loop. Servo LEDC
  warnings исчезли; warning по `GPIO3` backlight и sample-rate warning
  `16000/24000` остались отдельными хвостами. Motion-команды и raw WS probe не
  выполнялись; rollback не потребовался.

## Живая точка 2026-09-03

- Ветка `codex/hardware-development-enabled-20260903 @ e3fa25c0e55a`
  фиксирует аппаратную политику поверх продуктового кода
  `a8326d6818cb1ed72db8a5cc00c00b5366f270b8`.
- Свежая canonical сборка `gosha-v1` прошла: `gosha.bin` — `3654384` байт,
  свободно 11% app-раздела. Host test redaction, static logging guard и
  `git diff --check` прошли.
- До записи сохранён полный 16-MB backup вне Git с mode `600`. Приложение
  записано только по `0x20000`; assets уже совпадали с кандидатом, поэтому NVS,
  `otadata`, bootloader, partition table и assets не менялись. Write hash и
  отдельный `verify_flash` успешны.
- Live smoke подтвердил `gosha 2.2.2`, ESP-IDF `5.5.2`, 8 MB PSRAM,
  non-camera, Wi-Fi, OTA/config, runtime events, локальный WebSocket,
  read-only identity, wake word и голосовой диалог. Panic, watchdog и reset
  loop не наблюдались; motion-capable MCP tools не вызывались.
- После прогрева ASR-to-first-TTS составил `1.990 s` и `1.280 s`. Холодный
  WebSocket setup после wake занял `2.620 s`, первый холодный turn до TTS —
  `7.200 s`. Предупреждение `16000/24000` sample-rate остаётся хвостом. LEDC
  warnings и конфликт `GPIO12` закрываются локальной статической правкой выше;
  live-кандидат из этого раздела ещё был собран до неё.

## Локальная точка 2026-08-28

- Ветка `codex/firmware-log-hardening-20260828` от `28eb7584aaeef0cb66aa3c967bf4a162f49b3d0b` добавляет защиту URL/AFSK-диагностики: в логи, экранные сообщения и ошибки не должны попадать `userinfo`, host/IP, port, path, query, fragment, token и полный SSID/password text.
- Добавлен `diagnostic_redaction::RedactUrlForDiagnostics` и исполняемый static guard `firmware/scripts/check_sensitive_logging.py`. Host test, static guard, `git diff --check` и каноническая сборка `gosha-v1` прошли без устройства.
- Follow-up fix после review wave2 дополнительно убрал raw response body из `self.screen.snapshot`; ответ теперь дочитывается, но не печатается.
- Статическая сборка дала `gosha.bin` размером `3654368` байт, свободно 11% app-раздела; для живой проверки нужно собрать новый кандидат из отдельной ветки.
- Неисправная левая серва физически отключена. USB/serial, flash, update,
  power-cycle, перезагрузка и неподвижная проверка `:8080` разрешены только по
  аппаратной политике: no-motion preflight, backup, verify и rollback.
  Motion, `Home`, `trim` и servo sequence требуют отдельной команды.

## Самая свежая точка 2026-08-27

- В Draft PR `#24` опубликована статическая remediation-правка `feature/firmware-orange-eyes @ 7751a3ca326174d217536f6a8de7c09433c3e955`: OTA-default больше не содержит временный relay, production-сборка требует owner-only `GOSHA_OTA_URL`, чувствительные Wi-Fi/activation/Authorization-логи удалены, неполная `runtime_events` конфигурация очищает старый маршрут и token, `self.otto.stop` не запускает `Home`, а `git diff --check origin/main` чист.
- Полная сборка ESP-IDF 5.5.2 с тестовым `.invalid` endpoint прошла без доступа к устройству: `gosha.bin` — `3652384` байт, 12% app-раздела свободно. Полученный ZIP не является deployable-артефактом.
- Upstream symlink `_codeql_detected_source_root`, который блокировал защищённую упаковку workspace AI Office, удалён как неиспользуемый служебный marker.
- Immutable terminal review `task-20260827T104756Z-immutable-terminal-firmware-pr-24-gate-at-7751a3c` на фактическом `GPT-5.5 / xhigh` дал `PASS`, P0/P1/P2 нет. PR оставить Draft/Open; новый аппаратный кандидат готовить в отдельной ветке по актуальной политике.

## Предыдущая точка 2026-08-25

- Временный сетевой канал принят как рабочий обход: `TEMP_NL_RELAY` временно прокидывает роботу путь к `PRIMARY_PLATFORM_SERVER`. Это не новый production-сервер; в конце месяца канал нужно перенести на `FUTURE_PRODUCTION_SERVER`.
- Роли портов фиксируются без IP-адресов, секретов и хардкода в прошивке: `18876` — HTTP-контур панели, OTA и config; `18080` — голосовой `WebSocket` и совместимый `MCP`.
- Новый физический робот привязан к `gosha-main`; Android показывает `Гоша Main`, робот разговаривает, голосовой сценарий принят.
- Firmware quality gate для `feature/firmware-orange-eyes @ 07d5f6658b6c70c81626ebcc3fbee930ced94fc6`: на `2026-08-25` Draft PR был допустим, а merge и установка были остановлены по конкретным причинам.
- Причины: `self.otto.stop` не является безопасной неподвижной проверкой, потому что запускает возврат в `Home`; `git diff --check` падал на хвостовых пробелах в vendored-компоненте; текущий на тот момент release ZIP содержал устаревший merged-образ.
- В этой исторической точке аппаратные действия были приостановлены. С
  `2026-09-03` прошивка, перезагрузка и неподвижная проверка `:8080` разрешены;
  движения, `set_trim` и servo sequence требуют отдельной команды.

## Предыдущая точка 2026-08-24

- В актуальную оранжевую ветку добавлен локальный read-only контракт `gosha.identity.get/result`, который возвращает Android аппаратный `device_id` по существующему `/ws`, не пересылая ответ в облако.
- Каноническая полная сборка `gosha-v1` прошла; новый образ не прошивался.
- Проблемный левый сервопривод теперь подтверждённо отключён от платы; прошивка,
  перезагрузка и неподвижные проверки разрешены. Движение остальных приводов
  остаётся отдельным механическим этапом.
- Настройка домашнего Wi-Fi завершилась, однако прямой маршрут до публичного узла платформы оказался внешним сетевым блокером; на 2026-08-25 он временно обойдён через `TEMP_NL_RELAY`.

## Самая свежая живая точка `2026-08-23`

- Новый робот прошит полным образом `gosha-v1` из `feature/firmware-orange-eyes @ 80310104e895d02d648364460e82d0c2b31e8ba8` после обязательной полной резервной копии `16 MB` вне Git.
- Полная запись потребовалась из-за несовместимой заводской таблицы разделов; для уже совместимого робота по-прежнему нельзя автоматически заменять точечную запись приложения и ресурсов полным образом.
- Запись и `verify_flash` успешны. Первый запуск стабилен: `gosha 2.2.2`, `ESP32-S3`, `16 MB` flash-памяти, `8 MB PSRAM`, `non-camera`, подстройки всех сервоприводов равны `0`, `LH=GPIO8`, `RH=GPIO12`, локальный `WebSocket` запущен.
- Левая рука не работает из-за физического сервопривода или его кабеля: исправный правый сервопривод двигался на левом канале, а левый не запускался на правом канале без ручного подталкивания.
- Неисправный сервопривод левой руки должен оставаться отключённым. Движения
  остальных приводов и `set_trim` не выполнять без отдельной команды.

## Самая свежая точка 2026-07-23

- Экранная ветка: `feature/firmware-orange-eyes` в `/home/max/worktrees/gosha/firmware-orange-eyes`, база `af4de9c`.
- Для `gosha-v1` подготовлен единый тёмно-оранжевый приборный профиль `#E06F00`: глаза, Wi-Fi, батарея, беззвучный режим, центральный статус, уведомления и резервный значок эмоции.
- Цвет имеет один источник `GOSHA_UI_ACCENT_COLOR_HEX`; фон платы принудительно остаётся чистым чёрным даже при будущих ресурсных темах, а критическое предупреждение низкого заряда остаётся красным.
- Все 21 GIF и 692 кадра проверены пиксельно; остальные платы не затронуты.
- Каноническая сборка прошла. SHA-256: `gosha.bin` — `6d466b9a2a6299fc1cd73048b30452f051163026b199268fc3e92638d5dd481d`, `generated_assets.bin` — `12520722b9a56c0b687d072cb668e2f0ede0260b3a7fdef365abe81015231516`.
- ИИ-офис `task-20260723T090714Z-read-only-review-firmware-dark-orange-instrument-panel` не нашёл кодовых P0/P1/P2; найденное замечание к старым документам исправлено. Финальная проверка `task-20260723T091854Z-final-read-only-review-dark-orange-instrument-profile` также завершилась без P0/P1/P2.
- Перед установкой сохранены точные резервные копии прежнего приложения и ресурсов: `/tmp/gosha-app-before-dark-orange-20260723.bin`, SHA-256 `8046e3804b3d1f5f741e81c7e821894dffd618941cad6792742da4918ef31fd4`; `/tmp/gosha-assets-before-dark-orange-20260723.bin`, SHA-256 `871b1b74b98741c48c34d178588ae816fdba2e8197af1a36c46a3667acf70729`.
- Новый профиль установлен согласованной парой: приложение записано только по `0x20000`, ресурсы — только по `0x800000`. NVS, `otadata`, таблица разделов и загрузчик не изменялись.
- `verify_flash` подтвердил совпадение обоих разделов с собранными файлами; повторное наблюдение по UART продлено до 600 секунд без паники, сторожевого таймера и повторной загрузки, минимальная свободная память `66999` байт.
- На этой точной паре подтверждены свежие события робота, живой OTA-контракт и полный цикл восстановления Android/панели.
- Доказательство: `/home/max/AI_OFFICE/local_only/ai-office/logs/task-20260723T111058Z-read-only/live-validation/acceptance-evidence.json`, SHA-256 `600c3d526dddf95a39c38c1cf126a952d02a2e6b7b506f8a591b6f94d7690f0e`.
- Следующий шаг: визуально проверить приборную строку и глаза, затем повторить слово пробуждения и один голосовой диалог.
- Этот ресурс содержит также текущую модель слова пробуждения, поэтому устанавливать его можно только поверх совместимого `gosha-v1` того же контура.

## Предыдущая точка 2026-07-22

- Профильная ветка: `feature/firmware-triangle-runtime` в `/home/max/worktrees/gosha/firmware-triangle-runtime`.
- База: `origin/main`, коммит `0980c2a`.
- Стадия: отдельный неблокирующий канал событий прошивки для общего контура собран, проверен и установлен на тестового робота.
- Первый живой образ перезапускался из-за гонки завершения фоновой TCP-задачи; робот сразу возвращён на стабильный образ, после чего исправлены оба пути `EspTcp` и `EspSsl`.
- Исправление закреплено локальным переопределением `firmware/local_components/78__esp-ml307` версии `3.6.5`; служебный HTTP событий использует `connect_id=4`.
- Финальная проверка ИИ-офиса `task-20260722T115308Z-final-review-firmware-tcp-tls-lifetime-fix` не нашла P0/P1/P2 и разрешила app-only установку.
- Текущий образ `gosha.bin` с SHA-256 `8046e3804b3d1f5f741e81c7e821894dffd618941cad6792742da4918ef31fd4` записан только по `0x20000` с сохранением NVS, ресурсов и загрузчика.
- Контрольное окно более 210 секунд прошло без паники, сторожевого таймера и повторной загрузки; события робота подтверждены в живой базе платформы.
- Следующий шаг: сквозной Android P1 — потеря и возврат домашнего Wi-Fi без перезапуска приложения с одновременной проверкой трёх сторон в панели; отдельно повторить ручной голосовой диалог.
- Не менять в этой ветке голосовой протокол, локальный `WSControl`, OTA-адрес или аппаратный профиль платы.

Короткая контрольная точка для следующего агента в `GOSHA_FIRMWARE`.

## Сначала прочитать

1. `../AGENTS.md`
2. `/home/max/GOSHA_PLATFORM/docs/GOSHA_PROJECT_MAP_RU.md`
3. `NEW_CHAT_CHECKPOINT_RU.md`
4. `AGENT_CHECKPOINT_RU.md`
5. `PROJECT_STATUS_RU.md`
6. `FIRMWARE_IMPORT_CHECKPOINT_RU.md`
7. `HARDWARE_MANIFEST_RU.md`
8. `PIN_MAP_RU.md`

## Последняя зафиксированная точка

- Локальный репозиторий `GOSHA_FIRMWARE` уже создан.
- У репозитория уже настроен `origin`:
  - `git@github.com:MaxCorpOrg/GOSHA_FIRMWARE.git`
- Ветка `main` уже отправлена в GitHub и отслеживает:
  - `origin/main`
- Публичный URL репозитория:
  - `https://github.com/MaxCorpOrg/GOSHA_FIRMWARE`
- Дерево исходников прошивки уже импортировано в:
  - `/home/max/GOSHA_FIRMWARE/firmware`
- Канонический источник импорта уже зафиксирован:
  - `/home/max/MAX_CORP_CORE/AI_ROBOT/xiaozhi-esp32`
- Канонический эталон сравнения и отката уже зафиксирован:
  - `/home/max/MAX_CORP_CORE/AI_ROBOT/new/v2.0.5_otto-robot/merged-binary.bin`
- Профиль `gosha-v1` уже создан как копия `otto-robot`.
- Профиль `gosha-v1` уже виден в `scripts/release.py --list-boards`.
- Текущий реально прошитый порог слова пробуждения:
  - `38`
- Порог `50` пока не нужен.
- Рабочий `ESP-IDF` уже поднят:
  - `/home/max/esp/esp-idf-v5.5.2`
- Первый полный build уже выполнен успешно.
- Первый merged-образ уже получен:
  - `/home/max/GOSHA_FIRMWARE/firmware/build/merged-binary.bin`
- Первое тестовое устройство уже прошито.
- Экранный сбой на ранней инициализации уже исправлен.
- Ошибка `404` при обращении к серверу уже локализована:
  - причиной был старый адрес OTA в прошивке на порту `8876`
  - адрес по умолчанию уже переключён на логический маршрут `PRIMARY_PLATFORM_SERVER:18876/gosha/ota/`
  - исправленный образ уже повторно прошит на тестовое устройство
- Выполнен первый пользовательский перевод прошивки на бренд `GOSHA`:
  - русский язык по умолчанию
  - имя точки доступа `GOSHA-A-<хвост MAC>`
  - имя настройки по Bluetooth `GOSHA-Setup`
  - внешнее имя платы `GOSHA`
  - имя приложения в сборке `gosha`
- Для `gosha-v1` уже включено собственное слово пробуждения:
  - `Custom Wake Word`
  - внутренний токен `gosha`
  - отображаемое имя `Гоша`
- Для следующей аппаратной проверки уже подготовлено ослабление чувствительности:
  - `CONFIG_CUSTOM_WAKE_WORD_THRESHOLD`
  - было `20`
  - затем стало `35`
  - затем стало `45`
  - затем стало `40`
  - текущий подтверждённый живой шаг: `38`
- Текущее аппаратное ограничение:
  - `gosha-v1` не поддерживает `Device AEC` в текущей конфигурации платы
  - поэтому ложные срабатывания могут быть связаны и с эхом собственного динамика
- Уже подтверждён промежуточный эффект:
  - на `35` ложных срабатываний стало меньше
  - на `45` отклик на имя стал слишком слабым
  - на `40` отклик вернулся, но всё ещё был слабым
  - при громкой речи без имени `Гоша` лишние срабатывания ещё не исключены полностью
  - образ с порогом `38` уже собран канонически через `scripts/release.py`, прошит и подтверждён по boot log строкой `set det threshold to 0.380000`
  - для профильных параметров платы нельзя полагаться только на обычный `idf.py build`, потому что он не всегда подтягивает `sdkconfig_append`
- Совместимый голосовой путь `WebSocket` пока остаётся `/xiaozhi/v1/`.
- Текущий реальный статус устройства:
  - загрузка проходит
  - определяется вариант `non-camera`
  - устройство подключается к домашнему `Wi‑Fi`
  - слово пробуждения `Гоша` уже срабатывает
  - русская фраза `Привет` уже распознаётся по смыслу
  - робот уже отвечает голосом по-русски
  - остаются доводка качества, устойчивости и пользовательского сценария настройки
- Для активной платы `gosha-v1` уже выполнен отдельный аудит пользовательских китайских строк:
  - описания `MCP`-инструментов, ошибки и runtime-логи переведены на русский
  - общий живой тайм-аут сетевого подключения в `boards/common/nt26_board.cc` тоже переведён
  - оставшиеся CJK-символы относятся в основном к комментариям и неактивным частям дерева
- Последний зафиксированный живой дефект и текущий фикс:
  - при фразе настройки `Wi‑Fi` локальный звук сначала говорил нормально, затем уходил в резкий шум и обрывался;
  - по журналу подтверждено, что это был не двойной запуск, а сбой локального декодирования;
  - локальные `Opus`-пакеты в `assets/locales/ru-RU` идут по `80 ms`, а `AudioService` ошибочно помечал их как `60 ms`;
  - в `firmware/main/audio/audio_service.cc` уже добавлен расчёт реальной длительности пакета;
  - новый `gosha.bin` уже собран и прошит на тестовое устройство через `/dev/ttyACM0`
- Общая карта связанных контуров теперь зафиксирована здесь:
  - `/home/max/GOSHA_PLATFORM/docs/GOSHA_PROJECT_MAP_RU.md`

## Что делать следующим

- На новом устройстве держать неисправную левую серву отключённой; прошивка,
  update, перезагрузка и неподвижные функциональные проверки разрешены только
  по аппаратной политике: no-motion preflight, backup, verify и rollback.
  Движения остальных приводов требуют отдельной команды.
- После замены сначала проверить новую серву без качалки и нагрузки, затем
  только по отдельной команде владельца выполнить `Home`, правильно установить
  механику и провести малый тест.
- В `GOSHA_FIRMWARE` уже унифицированы правила общения агента:
  - корневой и локальные `AGENTS.md` требуют понятный русский технический язык;
  - для типовых терминов уже закреплены русские формы и пояснения.
- Подтвердить аппаратный манифест и pin map по реальным журналам устройства.
- Сразу первым живым действием повторно включить режим настройки `Wi‑Fi` и ушами проверить, исчезли ли шум и обрыв голосовой подсказки после нового образа.
- Если дефект по звуку сохранится, следующим шагом разбирать ресемплинг `16000 -> 24000` и выходной тракт `NoAudioCodecSimplex`.
- Прогнать несколько подряд срабатываний слова пробуждения `Гоша`.
- На уже прошитом пороге `38` сравнить число ложных срабатываний и уверенность отклика с шагами `20`, `35`, `40` и `45`.
- Прогнать несколько подряд русских вопросов и ответов.
- Живые проверки локального `:8080` разрешены при свободном `WSControl`;
  соединение закрывать сразу после проверки.
- Разобрать локальный портал настройки в сети точки доступа устройства, который в одном из прогонов давал пустую страницу.
- Отдельно учитывать, что текущий серверный голос пока ограничен `EdgeTTS`, поэтому следующие шаги по качеству тембра частично будут зависеть не от прошивки, а от платформы.
- Если следующий чат будет про полную очистку дерева, помнить:
  - активный пользовательский слой уже русифицирован
  - оставшийся хвост — комментарии, неактивные платы и `zh-*` ресурсы
- До замены левой сервы можно проверять только no-motion функции по
  аппаратной политике: голос, OTA, MCP и read-only `status`/`battery`. После
  замены и отдельной механической приёмки допускается отдельное безопасное
  действие, если владелец явно разрешит motion.
- Если позже появятся новые локальные `AGENTS.md`, переносить в них тот же блок правил русского технического языка.
