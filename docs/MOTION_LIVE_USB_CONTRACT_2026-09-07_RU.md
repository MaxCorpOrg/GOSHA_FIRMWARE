# Motion Live USB contract — 2026-09-07

Этот документ фиксирует source-level контракт USB Live для `gosha-v1`. Он не
является аппаратным тестом и не означает, что USB Live уже прошёл ESP-IDF build
или был установлен на робота.

## Включение сборки

USB Live закрыт по умолчанию и включается только явным build-флагом:

- `CONFIG_GOSHA_MOTION_LIVE_USB_LOCAL_OPT_IN=y`
- `CONFIG_GOSHA_MOTION_LIVE_LOCAL_OPT_IN=y`
- `CONFIG_GOSHA_MOTION_LIVE_RIGHT_ARM_LOCAL_OPT_IN=y`
- `CONFIG_GOSHA_NO_MOTION_SAFE_PROFILE=y`
- `CONFIG_GOSHA_SAFE_NEUTRAL_BOOT_PROFILE=y`
- явный owner-local `GOSHA_MOTION_LIVE_PROFILE_HEADER`

Checked-in `config.json` не включает USB Live в normal или safe-neutral build.

USB opt-in build обязан оставить firmware logs на UART и выключить USB console:

- `CONFIG_ESP_CONSOLE_UART_DEFAULT=y` или другой UART console
- `# CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG is not set`
- `# CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG is not set`
- `CONFIG_ESP_CONSOLE_SECONDARY_NONE=y`

CMake и source-level guards останавливают сборку USB Live, если primary или
secondary USB Serial/JTAG console включены. Runtime USB frames не должны
смешиваться с firmware logs. Boot ROM USB chatter находится вне runtime
контракта; клиент отбрасывает все строки без live prefix.

## Wire format

Клиент открывает USB Serial/JTAG как serial `115200`. Для USB CDC/JTAG firmware
не задаёт baudrate; значение нужно для обычного host serial API.

Каждая команда занимает одну строку:

```text
@GOSHA-LIVE:{compact-json}\n
```

`compact-json` — существующий JSON протокола `gosha.motion.live.v1`, тот же,
что используется в WebSocket Live. JSON команды ограничен `4096` байтами без
prefix и LF.

Ответ занимает одну строку того же вида:

```text
@GOSHA-LIVE:{compact-json}\n
```

JSON ответа ограничен `16384` байтами без prefix и LF. Prefix и LF входят только
в serial envelope.

## Parser и ошибки framing

USB parser bounded и line-based:

- строка должна начинаться с точного prefix `@GOSHA-LIVE:`;
- CR перед LF допускается и игнорируется;
- чужие строки, malformed prefix, NUL и malformed JSON отбрасываются без ответа;
- JSON parse строгий: используется null-terminated parse с проверкой parse-end,
  поэтому trailing garbage после JSON отклоняется;
- partial line timeout — `500` мс;
- command JSON больше `4096` байт вызывает overflow.

Payload, access key и command JSON не пишутся в firmware logs. Логи содержат
только общие причины transport/framing отказа.

## Сессия и владение

USB Live использует общий `MotionLiveAdapter`/`MotionLiveCore`, общий mutex,
общую авторизацию, общий watchdog и одну active session с WebSocket Live.
Отдельного core и отдельной auth ветки нет.

USB owner id — зарезервированное отрицательное значение
`kMotionLiveUsbOwnerId`; оно не пересекается с WebSocket socket fd. Если USB
armed, WebSocket `pose`, `keepalive` или `stop` с тем же `session_id` не владеют
сессией и получают обычный `session_not_owner`. Если WebSocket already armed,
USB `arm` получает обычный `session_busy`.

## Disconnect/reconnect

При физической потере USB connection, write failure, overflow, partial timeout
или попытке dispatch без текущего USB connection transport вызывает
`OnTransportClosed(kMotionLiveUsbOwnerId)`, сбрасывает partial framer и очищает
старый RX. Reconnect не продолжает старые цели: клиент должен заново пройти
`hello`, при необходимости `initialize_right_arm`, затем `arm` и новый seq.

Watchdog 300 мс остаётся независимым от USB reader loop и гасит active session
через общий core.

## Motion safety

USB transport не выполняет init/arm/pose на boot или connect. Он принимает
только framed JSON-команды. Правый канал не включается от открытия USB,
reconnect, `hello`, `arm` до init, `keepalive`, STOP или watchdog.

Новый source-only режим полного редактора — `motion_editor`. Он не является
verified calibration: capabilities возвращают `commissioning=false`,
`calibrated=false`, `right_arm_available=true`, а до отдельного authenticated
`initialize_right_arm` — `initialization_required=true`. В одной session можно
вести все доступные суставы; one-joint commissioning lock не применяется.
Текущий active set остаётся 5 суставов: ноги `[-35,+35]`, стопы `[-30,+30]`,
правая рука `arm_positive_x -> right_hand`; левая рука/`arm_negative_x` остаются
NC. Speed для editor profile — `1..10°/с`, UI default `5°/с` совместим.

Полный 3D-запрос правой руки `[-70,+55]` конфликтует с текущей production
нейтралью: `neutral=135`, `direction=+1` даёт servo `65..190`, а контракт servo
остаётся `0..180`. Firmware не clamp'ит и не меняет нейтраль. Full range
принимается только если explicit owner/mechanical profile задаёт достижимую
нейтраль, например synthetic `neutral=125` даёт servo `55..180`. Более узкий
правый range внутри `[-70,+55]` допустим только явно; тогда `joint_limits` в
caps отражают фактический range.

В `motion_editor` `keepalive` и timer `Tick` не двигают суставы и не вызывают
`StepTowardTarget`; они обновляют motion clock, чтобы idle не накапливал
`dt` для следующего POSE. Движение идёт только через POSE stream. STOP,
watchdog 300 мс, auth/session, explicit right init, no-motion/safe-neutral и
OTA/reboot/assets protections сохраняются.

Сохранён текущий right-arm commissioning contract. С 2026-09-08 он
backward-compatible: старые/default профили остаются на правой руке ±5°,
заметный ход ±15° доступен только через явно подготовленный symmetric 15°
профиль, а offline candidate для запроса «70° вверх» доступен только как
явный asymmetric range `[-70,+15]`. Автоматического расширения старых профилей
нет.

- левая рука недоступна/NC;
- правая рука: GPIO12, home/neutral 135°;
- `initialize_right_arm` отдельный, authenticated, запрещён при active session;
- ноги/ступни: ±1°;
- правая рука: ровно ±5° (`servo 130..140`), ровно ±15° (`servo 120..150`)
  или explicit 70-up candidate `[-70,+15]` (`servo 65..150`);
- скорость: 1°/с;
- watchdog: 300 мс;
- STOP удерживает последний commanded setpoint без Home/detach.

Core validator принимает только фактический prepared range правой руки:
`[-5,+5]/130..140`, `[-15,+15]/120..150` или `[-70,+15]/65..150`.
Ограничение delta внутри commissioning-сессии считается от фактического допуска
текущего range: 5°, 15° или 70° соответственно. Поэтому старый ±5° профиль не
может выполнить 10° sweep в одной сессии, явный ±15° может выполнить один
right-arm шаг до ±15° при 1°/с, а candidate `[-70,+15]` может идти из нейтрали
к −70° или +15°, но не может пройти −70°→+15° как 85° sweep внутри одной
сессии. UI/API mode остаётся `commissioning_right_arm`; `joint_limits` в
capabilities отражают фактические limits профиля.

Диапазон `[-70,+15]` является source/profile candidate, а не verified
механической калибровкой. Его нельзя помечать `calibrated=true` до отдельной
аппаратной проверки упора, гула, нагрева и видимого направления.

После reboot сохраняется прежнее поведение safe-neutral/right-arm build:
ноги/ступни 90°, правая рука не инициализирована до отдельного
`initialize_right_arm`.

## Проверки исходников

Покрытие без устройства:

- `motion_live_usb_framing_host_test.cc`: prefix, CRLF, malformed lines,
  command JSON boundary 4096, overflow, timeout, NUL reject, disconnect reset;
- `motion_live_core_host_test.cc`: USB owner против WebSocket owner, чужой STOP,
  explicit USB STOP, watchdog disarm USB-owned session, старый ±5 профиль не
  может выполнить 10° sweep в одной сессии, явный ±15 принимает один right-arm
  joint при 1°/с и отклоняет speed/multi-joint violations, candidate
  `[-70,+15]` идёт к servo65 и servo150 при сохранении one-joint/session gates,
  `motion_editor` принимает synthetic full `[-70,+55]` при neutral125,
  отвергает production neutral135/full servo190, разрешает multi-joint POSE
  и проверяет no-motion/no-burst keepalive;
- `check_gosha_v1_motion_live_profile.py`: opt-in dependencies, shared sender,
  USB parser/driver tokens, console separation, no payload/access-key logging,
  strict right-arm commissioning ranges ровно `[-5,+5]`, `[-15,+15]` или
  `[-70,+15]`, editor `commissioning=false/calibrated=false` и passive clock
  без тайного движения;
- `check_gosha_v1_safe_neutral_boot_profile.py`: checked-in configs не включают
  Live/right-arm/USB флаги по умолчанию.
