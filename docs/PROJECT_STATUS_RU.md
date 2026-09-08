# PROJECT STATUS

## 2026-09-08: профиль правой руки ±15° установлен

По свежему подтверждению владельца установлен app из `b8db96f624824821f49e9ca6a4f60a64286d12da`,
SHA-256 `1a31292cd5cbd67b8296b0d184188c5ef55ab50120fb73d388bcdf6b52915bc1`,
3 644 000 байт. В 06:33:57 UTC завершены свежий backup, app-only запись
по 0x20000, отдельный verify и restart — PASS. Старый app сохранён для отката;
NVS, otadata, bootloader, partition table и assets не записывались.
USB hello новой прошивки подтвердил профиль ±15°, init_required и нулевые
команды; ответ 10,9 мс. В 06:35 UTC выполнены отдельное включение правой руки
и цикл 0 → −15 → 0 при 1°/с. Оба STOP подтверждены, остальные суставы
сохраняли нулевые команды. Владелец сообщил, что отходил и не наблюдал цикл;
по его новому запросу выполнен повтор без initialize/reset, 06:37:29–06:38:09 UTC.
Оба STOP подтверждены, последняя команда руки0°, остальные0. Владелец
подтвердил видимое ровное движение: «да , все круто !».

Правая рука GPIO12, нейтраль135°, диапазон120…150°; ноги/стопы ±1°,
левая рука недоступна. Автоматические повторные движения/инициализация
не разрешаются этой записью. Точное последнее состояние и результаты повтора
смотреть в мета-репозитории, `docs/MOTION_STUDIO_ARM15_2026-09-08_RU.md`.


## Right-arm commissioning ±15 source — 2026-09-08

Статус: готово, не установлено. По свежей задаче владельца исходники USB/Motion
Live расширены без автоматического изменения старых профилей: `commissioning_right_arm`
принимает ровно два варианта правой руки — старый/default symmetric ±5°
(`servo 130..140`) и явно заданный symmetric ±15° (`servo 120..150`).
Правая рука остаётся только `arm_positive_x -> right_hand`, slot 5, GPIO12,
`neutral_degrees=135`, `direction=+1`, `max_speed_dps=1`; левая рука остаётся
NC, ноги/ступни остаются strict ±1° при 1°/с.

`prepare_live_profile.py` сохраняет default/sample ±5° и отвергает right-arm
варианты ±10°, больше ±15°, asymmetric limits и mismatch servo-bounds. Core
валидатор принимает только фактический extent профиля 5° или 15° и проверяет
физические bounds `neutral ± extent`; commissioning session baseline теперь
ограничивает right-hand delta фактическим extent профиля, поэтому старый ±5°
профиль не может выполнить sweep на 10° в одной сессии, а явный ±15° профиль
может выполнить один right-arm шаг до ±15° при 1°/с. Контракт one-joint,
init-once/failed latch, auth/session/watchdog 300 мс, PWM only POSE, STOP без
Home/detach и no-motion/OTA/reboot/assets protections не менялись. UI caps mode
остаётся прежним; `joint_limits` теперь отражают фактические limits профиля
±5° или ±15°.

Выполнены только host/source проверки без ESP-IDF build, flash, hardware,
network к роботу или чтения owner-local access key: `prepare_live_profile.py
--self-test`, `check_gosha_v1_motion_live_profile.py --self-test`,
`run_right_arm_pwm_route_host_test.py`, `run_motion_live_usb_framing_host_test.py`,
`motion_live_core_host_test` с ASan/UBSan, guards safe-neutral/no-motion/GPIO3
audio/pinmap/sensitive logging, `py_compile` и read-only `release.py --list-boards
--json`.

## USB Live: установленный образ и приёмка 2026-09-07

Root установил точный source b791edd app-only по 0x20000: SHA-256
4533a3d296fd2200c5bbc5497e0e25343406cb6850e78cae08db5993e70fe1c1,
3 643 760 байт; fresh backup, write, separate verify, restart PASS в 13:48 UTC.
NVS/otadata/bootloader/partition/assets не менялись. Первую USB-проверку
блокировал reset вокруг host PySerial open/close; Platform 5717693 исправил
host adapter без новой прошивки. Stable USB hello PASS, затем один explicit
initialize_right_arm и шаг 0°→−5° при 1°/с, STOP подтверждён в 14:10 UTC.
Владелец: «Да, движение было». Последняя команда root-теста: правая рука 130°,
четыре ноги/ступни 90°, левая NC. После закрытого root-теста в браузере
наблюдалась отдельная ручная сессия оператора, поэтому текущие команды нужно
читать заново. Не повторять init/шаг/возврат автоматически. Полная evidence
остаётся owner-only в meta workspace; source review PASS P0/P1/P2=0/0/0.


Уточнение владельца о начальном удержании: «Уже стояла так, движения не было».
Начальное перемещение также не подтверждено. Сейчас выясняется разъём
правой сервы: в старом манифесте есть перекрёстный тест через левый канал;
это не подтверждает нынешнее подключение. Новых команд движения нет.


## USB Live source implementation — 2026-09-07

В исходниках подготовлен отдельный USB Serial/JTAG transport для существующего
`gosha.motion.live.v1`. Он включается только явным build-флагом
`CONFIG_GOSHA_MOTION_LIVE_USB_LOCAL_OPT_IN=y` и использует тот же
`MotionLiveAdapter`, `MotionLiveCore`, mutex, сессию, авторизацию,
`initialize_right_arm`, single-owner lock, STOP и watchdog, что и WebSocket
Live. Отдельного core, отдельной авторизации и новых MCP/Home/OTA/routes нет.
WebSocket API сохранён как wrapper поверх общего sender abstraction.

USB wire contract: порт открывается клиентом как serial 115200; каждая команда —
одна строка `@GOSHA-LIVE:` + компактный JSON `gosha.motion.live.v1` + LF.
JSON команды ограничен 4096 байтами. Ответ — тот же prefix + compact JSON + LF;
JSON ответа ограничен 16384 байтами, prefix и LF входят только в serial envelope.
Parser bounded: чужие/malformed строки отбрасываются без payload-логов,
overflow и partial timeout сбрасывают USB-owned сессию. При физической потере USB,
ошибке write или попытке dispatch без текущего USB connection transport вызывает
`OnTransportClosed(kMotionLiveUsbOwnerId)`, очищает partial framer/RX и не
продолжает старые цели после reconnect. `kMotionLiveUsbOwnerId` отрицательный и
зарезервирован, поэтому не пересекается с WebSocket socket fd.

USB build требует сохранения UART log channel и выключенного USB console output:
`CONFIG_ESP_CONSOLE_UART_DEFAULT=y` или другой UART console,
`# CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG is not set`,
`# CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG is not set`,
`CONFIG_ESP_CONSOLE_SECONDARY_NONE=y`. CMake и source-level guards останавливают
USB opt-in build, если primary или secondary USB Serial/JTAG console включены,
поскольку runtime frames не должны смешиваться с логами. Boot ROM chatter на USB
остаётся вне firmware runtime contract; клиент должен игнорировать всё без prefix.

Безопасность правой руки сохранена: левая рука остаётся NC, правая рука GPIO12 с
home/neutral 135° доступна только через отдельный authenticated
`initialize_right_arm`; opening USB, connect/reconnect, hello, arm до init,
keepalive, STOP и watchdog не включают правый PWM и не отправляют pose. После
reboot остаются ноги/ступни 90°, правая рука NC до init; пределы остаются
ноги/ступни ±1°, правая рука ±5° при 1°/с, watchdog 300 мс.

Добавлены host/guard проверки: USB framer проверяет prefix, CRLF, malformed
строки, 4096-byte command JSON boundary, overflow, partial timeout, NUL reject и
сброс partial frame на disconnect. `motion_live_core_host_test` дополнительно
проверяет USB owner против WebSocket owner, чужой STOP, explicit USB STOP и
watchdog disarm USB-owned сессии. `check_gosha_v1_motion_live_profile.py` и
`check_gosha_v1_safe_neutral_boot_profile.py` обновлены под новый opt-in, shared
transport abstraction, strict parse, console separation и checked-in config
matrix. ESP-IDF build, serial, flash, reset, network и аппаратные команды этим
исходниковым изменением не выполнялись.

См. отдельный контракт: `docs/MOTION_LIVE_USB_CONTRACT_2026-09-07_RU.md`.

## Повторная проверка по свежему допуску владельца — 2026-09-07

После подтверждения «рука в нулевом положении, ничего не упирается,
запускай» в 12:31 UTC выполнены отдельное включение правой руки на 135°,
проверка связи без POSE и один шаг 0° → -5° при 1°/с. Цель подтверждена
через 5197 мс, 81 кадр POSE с одной абсолютной целью, максимум ответа 70 мс.
STOP подтверждён, сессия закрыта. Сейчас рука включена, её последняя команда
130°, четыре ноги/ступни 90°, левая рука недоступна. Владелец о новом шаге:
«Нет, рука не двигалась». Физическое движение не подтверждено. Flash/reset
не выполнялись.

Клиент теста `6f2bd9ff9f84acbc2a2661a005d2471ef98a3817`; неизменность
проверенных исходников Live и идентификатора калибровки проверена перед операциями.
Одноразовый сценарий проверен независимо; повторные запуски не разрешены
автоматически. Предыдущий сброс включения объяснён перезапуском владельца.

## Продолжение диагностики Motion Studio — 2026-09-07

После нового поручения владельца выполнено только чтение поддержки Live.
Робот теперь отвечает `right_arm_initialized=false` и требует отдельного
включения. Владелец подтвердил выключение питания и перезапуск робота
между проверками: это объясняет сброс включения руки, но не незаметность
предыдущего шага. Историческую
команду руки 130° из предыдущего теста не считать текущим PWM. Новых
initialize/ARM/POSE/STOP, прошивки или reset в этой фазе не выполняли.

Воспроизводимый тест цепочки `MotionLiveCore → Otto → Oscillator → LEDC`
добавлен в `firmware/scripts/run_right_arm_pwm_route_host_test.py` и прошёл с
ASan/UBSan. Компилируются настоящие три реализации; ESP-IDF/FreeRTOS и LEDC
заменены регистратором вызовов. Callback привязки задаётся тестом по контракту
контроллера; сам сетевой адаптер/контроллер в эту host-сборку не входит.
Правый канал GPIO12 отдельно включается с duty 819 (135°), серия POSE
доводит его до duty 796 (130°), после set проверен соответствующий update.
Ноги сохраняют duty 614 (90°), левая рука событий не получает; STOP и
watchdog не добавляют PWM-вызовов. Каналы пяти приводов различаются.
Это не измерение физического импульса или угла; подтверждённого объяснения
незаметного движения пока нет. Изменений установленной прошивки не было.

## Аппаратный результат правой руки — 2026-09-07

Эта запись заменяет состояние подготовки ниже. По отдельному поручению
владельца установлен app из Firmware
`beca6a022e4d9d8ddbebf6ac46e712f512160a0a`, SHA-256
`672016c53e1b153c293c15dce1560c3a55ae094cbf912f0ed990e1faf26e1675`,
3 635 840 байт. Свежий backup и проверка активного раздела выполнены;
записан только app по `0x20000`, отдельный verify прошёл, откат не потребовался.
NVS, otadata, загрузчик, таблица разделов и assets не записывались.

Правая рука включена отдельным `initialize_right_arm` на 135° (относительный
ноль). Владелец подтвердил положение как на 3D-модели. Проверка связи без POSE
дала 31 ответ, максимум 68 мс и подтверждённый STOP. Затем один раз выполнен
шаг `arm_positive_x` 0° → -5° при 1°/с: команда приводу 135° → 130°,
подтверждение цели через 5183 мс, завершение за 5241 мс, максимум ответа 69 мс.
Все 80 кадров POSE задавали одну абсолютную цель. STOP подтверждён, сессия
закрыта. Последняя команда руки 130°, четырёх ног/ступней 90°; левая рука
физически отключена и программно недоступна. За 25 секунд пассивного чтения
после шага panic/brownout/watchdog reset не обнаружены.

Владелец ответил «ytn» (прочитано как «нет»): заметного перемещения на 5°
не увидел. Сведения о гуле/упоре этим ответом не установлены. ACK является
подтверждением команды, не измерением угла, направления или равновесия.
Не повторять шаг, инициализацию, возврат или Home автоматически. Прочие
направления остаются на паузе. Ключи, резервные копии и журналы только локальны.

Проверки прошивки включали тесты с ASan/UBSan, статические проверки профилей,
генератора и матрицы выпуска; независимая проверка PASS без P0/P1/P2.
Точный образ собран ESP-IDF 5.5.2; 15 параметров платы сверены с каноническим
профилем. Рабочий `sdkconfig` после сборки восстановлен с закрытыми флагами;
конфигурация установленного образа сохранена отдельно локально. При перезапуске
правая рука снова требует отдельного включения; автоматического ARM нет.
Историческое описание разработки ниже не означает, что установка ещё ожидается.

## Motion Live правой руки — 2026-09-07

В worktree `firmware-motion-live` подготовлена отдельная разновидность
`mode=commissioning_right_arm`. Она не расширяет исторический
`mode=commissioning`: старый commissioning остаётся четырьмя каналами
ног/ступней с пределами ±1°, `verified` сохраняет прежнюю семантику
`calibrated=true`.

Новый профиль содержит пять активных суставов: четыре ноги/ступни с
пределами ±1° и `arm_positive_x -> right_hand`, физический slot `5`,
GPIO12, `neutral_degrees=135`, `direction=+1`, servo range `130..140`,
relative range ±5°, `max_speed_dps=1`. Левый `arm_negative_x` и servo slot
`4` (`left_hand`, GPIO8) недоступны и отдельно отвергаются, чтобы хранилище
servo slots не смешивало правую руку со старым индексом левой руки.
`calibration_id` вычисляется заново из `mode` и всех пяти joint bindings;
owner-local pending profile root уже провалидировал как
`6eaa6dcb3f22f05c55ed1f3c6890b4f670c0921bb4380f9641e74aabc809f3bc`.
Plaintext access key остаётся только в ignored owner-local JSON и не пишется
в generated header.

Правый канал включается отдельным compile opt-in
`CONFIG_GOSHA_MOTION_LIVE_RIGHT_ARM_LOCAL_OPT_IN`, по умолчанию `n`; он
зависит от `CONFIG_GOSHA_MOTION_LIVE_LOCAL_OPT_IN`, no-motion и
safe-neutral. Проверенная `config.json` release matrix не включает этот флаг
по умолчанию. Camera-вариант остаётся fail-closed: все servo pins маскируются
в `GPIO_NUM_NC`; GPIO12 правой руки передаётся только в non-camera branch.

Boot safe-neutral теперь attach'ит и удерживает только четыре канала
ног/ступней на 90°. Правый PWM не стартует на boot и не стартует от
`hello`, `arm`, `pose` до инициализации, `keepalive` или STOP. Перед
обычной Live-сессией нужен отдельный authenticated `initialize_right_arm` с
`request_id`, `calibration_id` и `access_key`; он сериализован тем же mutex,
запрещён при активной сессии, exact-once вызывает callback
`AttachRightHandAtHome(135)` и в случае успеха возвращает `capabilities` с тем
же `request_id`, `right_arm_initialized=true`, `motion_allowed=true`. До этого
`capabilities` сообщает `initialization_required=true`,
`right_arm_available=true`, `right_arm_initialized=false`,
`motion_allowed=false`, `reason=right_arm_initialization_required`. Ошибка
инициализации latch'ится как `right_arm_initialization_failed` без autoretry.

После успешного hold 135° движение остаётся session-gated и продвигается
только явными `pose` кадрами оператора: один владелец socket, строгий seq,
watchdog 300 мс, а `keepalive` и timer tick в этом режиме сами не меняют PWM.
STOP удерживает последнюю программную команду без Home/detach. В
`commissioning_right_arm` за сессию можно менять
только один физический канал; для ног/ступней отклонение от session baseline
≤1°, для правой руки ≤5°. `relative=-5` на `arm_positive_x` соответствует
servo `130` от rightHome135; это commanded setpoint, не измеренный угол.
`measured_pose` и `tilt` остаются `null`, `feedback.measured_position=false`,
`feedback.imu=false`; фактическое движение и калибровка аппаратно не
подтверждены. Все старые запреты MCP/Home/trim/sequence/OTA/reboot/assets
сохранены.

Локальные проверки без устройства: `motion_live_core_host_test` обычный,
тот же host test с `-Wall -Wextra -Werror -fsanitize=address,undefined`,
`prepare_live_profile.py --self-test`,
`check_gosha_v1_no_motion_profile.py --self-test`,
`check_gosha_v1_safe_neutral_boot_profile.py --self-test`,
`check_gosha_v1_motion_live_profile.py --self-test`, `py_compile` обновлённых
Python guards/generator, `git diff --check`, `scripts/release.py --list-boards --json`
с подтверждёнными `gosha-v1` и `gosha-v1-safe-neutral-boot`, а также
компиляция синтетического generated `commissioning_right_arm` header — PASS.
ESP-IDF build, serial, flash, reboot, network
и аппаратные команды этим агентом не выполнялись; свежий NVS backup и сборка
с right-arm opt-in остаются за root после source freeze.

## Аппаратный тест Live и исправление Wi-Fi — 2026-09-06

Владелец подтвердил питание от аккумулятора, USB, опору корпуса и возможность
снять питание; левая рука физически отключена. Полный 16-МБ backup прочитан.
Активный `ota_0` и исходный safe-neutral `baf0323d` доказаны по содержимому
backup и проверенной записи `otadata`. Подстройки приводов в NVS отсутствуют,
по исходникам используются нули. Обе руки остаются программно NC.

Из `c385ec0` установлен отдельный `mode=commissioning` app с SHA-256
`d4d1e70394e7d4dee94e04a0be13c7243e55f301095f30f565a315a11b43ab6a`:
только `ota_0` по `0x20000`, отдельный verify PASS. После запуска настоящие
capabilities подтверждают `calibrated=false`, четыре канала ног/ступней,
пределы ±1°, скорость 1°/с, watchdog 300 мс и отсутствие измерений.

Первый единственный запрос шага +1° не получил ACK за 300 мс. Клиент отправил
STOP и закрыл соединение, подтверждения STOP не получил. Повторный read-only
запрос показал прежние нулевые команды; владелец сообщил, что заметного
движения не было. Этот тест не считается успешным физическим движением.

Диагностика без ARM/POSE: из 15 запросов hello 10 отвечали дольше 300 мс,
максимум 955 мс. В idle приложение включает WIFI_PS_MAX_MODEM, а у станции
listen_interval=10. Исправление в `OttoRobot::SetPowerSaveLevel()` удерживает
Wi-Fi в PERFORMANCE только при `CONFIG_GOSHA_MOTION_LIVE_LOCAL_OPT_IN` и
применяет режим до запуска локального WebSocket. Остальные сборки сохраняют
прежнюю политику. В этом тестовом профиле Wi-Fi потребляет больше энергии;
watchdog и пределы приводов не расширены. Добавлены отрицательные проверки
снятия этой политики. Статические барьеры и ESP-IDF сборка прошли.

Wi-Fi app из `f82a730`, SHA-256
`3f0ee467e9d127d5f3ac2d809c41b9809831098c729debaa0ea81b1f884fd836`,
установлен app-only, отдельный verify PASS. Независимый review PASS,
P0/P1/P2=0/0/0. Пассивный запуск 25 с: без panic/brownout, PERFORMANCE применён
дважды, LOW_POWER не применялся. Сессия без POSE получила 30 ACK за 7–56 мс
и подтверждённый STOP. После этого согласованная цель +1° `leg_negative_x`
при 1°/с и STOP подтверждены. Последние команды: этот сустав +1°, остальные
0°; сессия закрыта, нейтраль/Home после теста не отправлялись.

Владелец и во втором тесте не заметил сдвига. Измерений угла/наклона нет,
поэтому физическое движение и калибровка остаются неподтверждёнными.
Диапазон не увеличивали. Профиль, ключ, backup и журналы сохранены только
локально; рабочий sdkconfig вновь закрывает Live. Source review и ответ
прошивки не заменяют физическую приёмку.


> Актуализация `2026-09-03`: аппаратная разработка разблокирована по
> `docs/HARDWARE_DEVELOPMENT_POLICY_RU.md`. Неисправная левая серва физически
> отключена; прежние запреты в исторических разделах ниже больше не действуют.

## Commissioning Live первичной проверки приводов — 2026-09-06

К Live adapter добавлен отдельный режим `mode=commissioning` для первого
онлайн-теста, когда механическая привязка и реальные диапазоны ещё не
проверены. Это не калибровка: в `capabilities` такой профиль сообщает
`commissioning=true` и `calibrated=false`, а обычный verified-профиль сохраняет
прежний смысл `calibrated=true`.

Commissioning остаётся закрытым по умолчанию и требует тех же барьеров, что и
verified Live: compile opt-in, локальный заголовок профиля вне Git, access key,
no-motion + safe-neutral boot, совпадение реальных pin/trim/neutral с runtime,
готовый watchdog, один владелец socket и строгие номера команд. Разрешены
только четыре текущих канала ног/ступней; обе руки остаются `GPIO_NUM_NC`.

Дополнительные ограничения commissioning: каждый lower-body joint обязан иметь
точные пределы `[-1,+1]` градус и `max_speed_dps <= 1`; за одну сессию можно
отклонять от начальной позы не более одного физического канала и не дальше чем
на один градус. Первый изменённый физический канал фиксируется до конца сессии:
даже после возврата в начальную позу переключение на другой канал требует
новой live-сессии. STOP/watchdog удерживают последнюю реально применённую
команду, не вызывают Home, detach, очереди движений, OTA, reboot или запись
ресурсов.

Генератор `firmware/scripts/prepare_live_profile.py` включает `mode` в
вычисляемый SHA-256 `calibration_id`; plaintext access key по-прежнему не
попадает в generated header. Старые generated initializers без поля `mode`
совместимы и трактуются как `verified`.

## Проверка сборки Live — 2026-09-06

Каноническая проверочная сборка через
`scripts/release.py gosha-v1 --name gosha-v1-safe-neutral-boot` на ESP-IDF 5.5.2
завершена успешно. Использован неразрешимый тестовый адрес `.invalid`.
Подтверждены `CONFIG_GOSHA_NO_MOTION_SAFE_PROFILE=y`,
`CONFIG_GOSHA_SAFE_NEUTRAL_BOOT_PROFILE=y` и выключенный параметр Live.
Это проверка кода, не образ для установки. Проверочные профили, ключи,
бинарные файлы и журналы остаются в игнорируемом `firmware/local_only/`.

Дополнительно прошла полная компиляция с включённым Live и синтетическим
профилем поверх параметров канонической сборки. Проверено включение нужного
заголовка в бинарный файл. Размеры приложения: закрытый Live — 3 631 232 байта,
синтетический Live — 3 632 064 байта; в разделе приложения свободно 12%.
Это не аппаратная приёмка и не готовый образ для установки. Проверочные
образы/архив перенесены под `local_only/` с пометкой `DO_NOT_INSTALL`;
обычный локальный `sdkconfig` возвращён к закрытому Live. Унаследованные
предупреждения о старых API ESP-IDF сохранены, новых ошибок компиляции нет.

## Live-настройка движений — 2026-09-06

На ветке `codex/motion-live-20260906` от установленного safe-neutral
`baf0323d17db9f0635cf9ae64d79eb6f3c19af2c` подготовлен обработчик
`gosha.motion.live.v1` для редактора Motion Studio из GOSHA_PLATFORM.
Вход — `firmware/main/boards/gosha-v1/README.md`.

- Сообщения Live обрабатываются локально на `/ws`, порт 8080, перед старым
  MCP-обработчиком. Они не преобразуются в старые команды движения.
- Обычная сборка не открывает Live. Нужны отдельный параметр
  `CONFIG_GOSHA_MOTION_LIVE_LOCAL_OPT_IN`, локальный проверенный профиль,
  оба прежних параметра no-motion/safe-neutral, совпадение выводов/подстроек,
  выполненное начальное удержание нейтрали и работающий контроль потери связи.
- Профиль явно связывает сустав модели с физическим каналом и задаёт знак,
  нейтраль, пределы и скорость. Его SHA-256 вычисляется генератором
  `firmware/scripts/prepare_live_profile.py`. Ключ доступа не попадает в заголовок:
  там хранится только его SHA-256. Профиль и ключ остаются вне Git.
- Разрешены только четыре канала ног/ступней; руки сохраняются `GPIO_NUM_NC`.
  Сессия принадлежит одному соединению, номера команд возрастают строго,
  устаревшие/чужие сообщения не продлевают срок управления. Последняя цель
  заменяет предыдущую; очереди макродвижений нет.
- Скорость ограничивает сама прошивка, с учётом дискретности PWM в 1°.
  Через 300 мс без допустимых команд изменение уставок прекращается.
  STOP завершает сессию и удерживает последнюю команду, не вызывает Home,
  не отключает опору и не меняет позу при повторном открытии сессии.
- `commanded_pose` в ACK/STOP означает применённые программные углы.
  `measured_pose` и `tilt` равны `null`: измерители углов и IMU не подтверждены.
  Автоматической стабилизации равновесия в этом изменении нет.
- Существующие ограничения MCP-движений, Home, trim, последовательностей,
  OTA, удалённого reboot и живой замены ресурсов сохранены.

Проверки без устройства: C++-тесты с `-Wall -Wextra -Werror`, ASan/UBSan,
генератор профиля, новые и прежние статические проверки — PASS.
Независимый review и повторная проверка исправления короткой строки SHA-256
завершены PASS, P0/P1/P2 = 0/0/0. В CMake также исправлена зависимость от
генерации языкового заголовка, обнаруженная при чистой сборке.

На робота ничего не установлено; USB/serial, соединение с роботом, motion,
flash/reset не выполнялись. Владелец ещё не подтвердил позу, гул/нагрев и
состояние рук после установленной безопасной нейтрали. Перед физическим
этапом нужны эти наблюдения, проверенная калибровка и отдельный аппаратный
допуск. Синтетические профили сборочных тестов не являются калибровкой.

## No-motion профиль `gosha-v1` 2026-09-04

- В отдельном firmware-worktree подготовлена ветка
  `codex/firmware-no-motion-safe-profile-20260904` от exact
  `4ce2cab34cc222d8624e97e7ddb62a6365e1c231`. Это продолжение принятого
  GPIO3/audio-contract кандидата без обращения к устройству.
- Цель правки — сделать текущий безопасный `gosha-v1` release-кандидат
  неподвижным на уровне кода: не полагаться только на физически отключённую
  левую руку и не оставлять boot Home или MCP motion gateway доступными по
  ошибке.
- В release-конфигурацию `gosha-v1` добавлен
  `CONFIG_GOSHA_NO_MOTION_SAFE_PROFILE=y`; сам Kconfig-символ зависит от
  `BOARD_TYPE_GOSHA_V1` и по умолчанию выключен. Чтобы вернуть движение позже,
  нужно отдельное аппаратное разрешение и отдельная осознанная смена этого
  параметра.
- В no-motion сборке `Otto::Init()` не вызывает `AttachServos()`, boot
  `ACTION_HOME` не ставится в очередь, а `QueueAction()` и
  `QueueServoSequence()` имеют отдельные проверки до `xQueueSend()`.
- MCP-инструменты движения и калибровки в no-motion сборке не регистрируются:
  `self.otto.action`, `self.otto.servo_sequences`, `self.otto.stop`,
  `self.otto.set_trim` и `self.otto.get_trims`. Read-only состояние остаётся:
  `self.otto.get_status`, `self.battery.get_level`, `self.otto.get_ip`. Голос,
  `Wi-Fi`, OTA/config, runtime events и локальный `WebSocket` не меняются.
- Дополнение `2026-09-04` по замечанию reviewer PR `#37`: общий инструмент с
  пометкой `user_only` `self.upgrade_firmware` теперь не регистрируется при
  `CONFIG_GOSHA_NO_MOTION_SAFE_PROFILE=y`, а прямой `tools/call` с этим именем
  отклоняется в `McpServer::DoToolCall()` до поиска инструмента и до вызова
  `Application::UpgradeFirmware()`. Это закрывает локальный `WebSocket`/MCP
  обход установки образа с включённым движением; контролируемая владельцем
  запись только app-раздела через serial с backup, verify и rollback этим кодом
  не затронута.
- Дополнение PR `#40` на `2026-09-04`: в
  `CONFIG_GOSHA_NO_MOTION_SAFE_PROFILE=y` автоматический `CheckNewVersion()`
  пропускает замену прошивки во время работы до вызова
  `Application::UpgradeFirmware()`, а сама `Application::UpgradeFirmware()`
  сразу возвращает отказ до доступа к `display`, закрытия `protocol`,
  остановки `audio`, смены `state`, загрузки образа, записи flash-памяти или
  reboot. Проверки версии, активации и конфигурации остаются рабочими.
- Удалённое MCP-действие `self.reboot` в no-motion профиле теперь не
  регистрируется, а прямой `tools/call` с этим именем отклоняется до поиска
  инструмента и планирования `Application::Reboot()`. Владелец по-прежнему
  может выполнить локальный serial app-only flash и последующий контролируемый
  reboot только после аппаратного preflight, backup, verify и rollback.
- Серверная команда `system.command == "reboot"` при активном no-motion профиле
  также отклоняется до `Schedule()` и `Application::Reboot()`.
- Follow-up Issue `#47` для PR `#44` закрывает оставшийся путь живой записи
  assets: `self.assets.set_download_url` не регистрируется при
  `CONFIG_GOSHA_NO_MOTION_SAFE_PROFILE=y`, прямой `tools/call` отклоняется до
  поиска инструмента и общего `Schedule()`, `CheckAssetsVersion()` выходит до
  открытия `assets/download_url` на запись, уведомлений, смены состояния,
  повышения питания, `assets.Download()` и планирования обновления прогресса
  через `Schedule()`, а
  `Assets::Download()` возвращает отказ до HTTP, `UnApplyPartition()`,
  стирания/записи flash-памяти и повторной инициализации раздела. Уже
  установленный локальный assets-раздел продолжает применяться.
- Добавлен исполняемый static guard
  `firmware/scripts/check_gosha_v1_no_motion_profile.py`; `release.py` запускает
  его для `gosha-v1` вместе с существующими static guards до owner-only
  `GOSHA_OTA_URL`. Guard расширен на раннее закрытие
  `Application::UpgradeFirmware()`, список разрешённых вызывающих мест,
  `self.upgrade_firmware`, `self.reboot` и `self.assets.set_download_url` в
  общем MCP-слое, раннее закрытие `CheckAssetsVersion()` и `Assets::Download()`,
  список разрешённых вызывающих мест `Assets::Download()`; отрицательные
  проверки ловят незащищённое вызывающее место, регистрацию и прямой вызов.
- Проверки без устройства прошли: новый no-motion guard с `--self-test`,
  существующие pin map, GPIO3/audio-contract и sensitive logging guards,
  прямой no-motion guard, `py_compile`, `git diff --check`,
  `scripts/release.py --list-boards --json` с подтверждённым `gosha-v1`.
  Канонический `scripts/release.py gosha-v1 --name gosha-v1` дошёл до всех
  static guards и остановился на отсутствующем owner-only `GOSHA_OTA_URL`,
  поэтому production release build не выполнялся.
- Non-canonical compile smoke на ESP-IDF `5.5.2` во временном `/tmp` каталоге
  прошёл до `Project build complete`: `CONFIG_GOSHA_NO_MOTION_SAFE_PROFILE=y`
  подтверждён во временном `sdkconfig`, `gosha.bin` — `3629088` байт, SHA-256
  `7595328ab0a12ed65a8e68f4d5ae08e521bf641cd88264cd5699bfdd7cde1af1`,
  свободно 12% app-раздела. Этот образ не является release-артефактом для
  установки.
- Эта точка пока статическая: USB/serial, flash, reboot, update, live
  `:8080/ws`, motion, `Home`, `set_trim`, servo sequence и operator gateway не
  выполнялись. Для установки нужен отдельный no-motion hardware-window с
  backup/verify/rollback. Любое будущее обслуживание assets требует отдельной
  внешней процедуры владельца с backup, verify и rollback, а не удалённой живой
  команды.

## Статическая правка GPIO3/audio contract 2026-09-04

- В отдельном firmware-worktree подготовлена локальная ветка
  `codex/firmware-gpio3-samplerate-20260904` от
  exact `0d7248ea08f17dad270fe6e5eee219a00f1f10d5`. Она не отправлялась и не
  коммитилась.
- По локальному ESP-IDF `5.5.2` проверен механизм предупреждения: LEDC
  резервирует GPIO при `ledc_channel_config()`, `ledc_stop()` reservation не
  снимает, а публичный `gpio_reset_pin()` снимает его через внутренний
  `esp_gpio_revoke()`. Поэтому для `gosha-v1` добавлен
  `ReleaseCameraProbePwm()`, который после неуспешного camera probe освобождает
  `CAMERA_XCLK` перед дальнейшим использованием `GPIO3` как подсветки
  non-camera дисплея.
- Голосовой `hello` больше не оставляет неоднозначным смысл `sample_rate`.
  Legacy поле `sample_rate=16000` сохранено для совместимости, но рядом
  добавлены явные `input_sample_rate=16000`, `uplink_sample_rate=16000` и
  `output_sample_rate`, взятый из фактического аудиокодека. Для текущей платы
  это документирует ожидаемую схему `16000` вход/исходящий Opus и `24000`
  локальный вывод.
- Runtime warning `16000/24000` заменён на информационный лог о контракте и
  ресемплинге. Это не скрывает настоящие ошибки: невозможность открыть output
  resampler остаётся `ESP_LOGE` в `AudioService`.
- Добавлен исполняемый static guard
  `firmware/scripts/check_gosha_v1_gpio3_audio_contract.py`; `release.py` для
  `gosha-v1` теперь запускает pin map, GPIO3/audio и sensitive logging guards
  до чтения owner-only `GOSHA_OTA_URL`.
- Проверки без устройства прошли: новый guard с `--self-test`, pin map guard с
  `--self-test`, sensitive logging guard, `py_compile` guard/release.py,
  `git diff --check`, `scripts/release.py --list-boards --json`,
  `idf.py --version` с ESP-IDF `v5.5.2`. Non-canonical compile smoke для
  `esp32s3`/`gosha-v1` в `/tmp` без owner-only `CONFIG_OTA_URL` прошёл до
  `Project build complete` (`gosha.bin` `0x37c4c0`, 11% свободно в app
  partition). Canonical `release.py gosha-v1 --name gosha-v1` подтвердил
  запуск static guards и дальше ожидаемо остановился на отсутствующем
  owner-only `GOSHA_OTA_URL`, поэтому release-образ не заявляется.
- Аппаратные действия не выполнялись: USB/serial, flash, reboot, update, live
  `:8080/ws`, motion, `Home`, `set_trim`, servo sequence и operator gateway не
  запускались. Для доказательства исчезновения warning на реальном роботе нужен
  отдельный no-motion hardware-window.

## Локальная pin map/LEDC правка 2026-09-03

- В ветке `codex/noncamera-pinmap-ledc-fix-20260903` от `70a9884` для
  `gosha-v1` убран программный конфликт non-camera pin map: правая рука
  остаётся на `GPIO12`, а `display_cs_pin` переведён в `GPIO_NUM_NC`.
- Из `ActionTask` удалён только повторный `AttachServos()`: первичный
  `Otto::Init()` и boot `ACTION_HOME` не изменены. Добавлен статический guard
  `firmware/scripts/check_gosha_v1_pinmap.py`, который ловит дубли активных
  non-NC GPIO и игнорирует намеренные `GPIO_NUM_NC`.
- `2026-09-03` эта ветка установлена на живой робот строго app-only из exact
  `c81d24c941be8cadd6a96c9bbddd2884bf5906ae`. Canonical build ESP-IDF
  `5.5.2`: `gosha.bin` — `3654400` байт, SHA-256
  `603b1609615a530ff9b138bcfad9d73cf3d01ddee352d1483e17044a2694dd41`;
  `merged-binary.bin` — `13790351` байт, SHA-256
  `51156563b3948e9c095e6af0843c13a80737fd5fc4f3a59859241fa31fe42a68`;
  release ZIP — `6390845` байт, SHA-256
  `1e6db03a397413f9660ab3179f17841c5ce21576ab37b0cf5acbc7c36a064394`.
- Preflight подтвердил существующий full-flash rollback backup `16 MB` mode
  `600`, предыдущий app rollback image, `ESP32-S3` rev `v0.2`, flash `16MB`,
  режим `dio/80m/16MB`, совпадение partition table и assets с устройством.
  Assets SHA-256:
  `12520722b9a56c0b687d072cb668e2f0ede0260b3a7fdef365abe81015231516`;
  partition table SHA-256:
  `4811619cacae08ef2e0e71b7220c6033a346ca5da7ca179082408c963ef530b5`.
- Записан только app-раздел `0x20000`; NVS, `otadata`, bootloader, partition
  table и assets не изменялись. `write_flash` подтвердил hash, отдельный
  `verify_flash` дал `verify OK (digest matched)`. Rollback не потребовался.
- Короткий serial smoke подтвердил boot `gosha 2.2.2`, ESP-IDF `5.5.2`, 8 MB
  PSRAM, Wi-Fi, OTA/config, runtime events, локальный `WebSocket` `8080`,
  threshold `0.380000` и переход в `idle`. Panic, watchdog, brownout и reset
  loop не наблюдались; был один ожидаемый reset от monitor. Servo LEDC
  warnings по `GPIO8`, `GPIO12`, `GPIO17`, `GPIO18`, `GPIO38`, `GPIO39`
  исчезли. Остались отдельные хвосты: warning по `GPIO3` backlight и
  sample-rate warning `16000/24000`. Motion, `Home`, `set_trim`, servo
  sequence и raw WS probe не выполнялись.

## Живая установка hardening-кандидата 2026-09-03

- Из `codex/hardware-development-enabled-20260903 @ e3fa25c0e55a`, чей
  продуктовый код соответствует `a8326d6818cb1ed72db8a5cc00c00b5366f270b8`,
  собран свежий canonical кандидат `gosha-v1` на ESP-IDF `5.5.2`.
- `gosha.bin` размером `3654384` байт и SHA-256
  `78fe6c115a44fa4e9f40b0e990c3e6162e1acfc8cdac5ff1b10ed1bf628d5764`
  прошёл host/static checks и сохранил 11% app-раздела.
- Перед записью снят полный 16-MB backup в owner-only `local_only`, mode `600`,
  SHA-256
  `c3dee211b4b66d49500447bfc9cf66d97e7ec65f9d631da76ecb0c13249d594a`.
  Таблица разделов совпала с ожидаемой.
- Assets на устройстве побайтно совпали с новым `generated_assets.bin`,
  поэтому записан только app-раздел `0x20000`. NVS, `otadata`, bootloader,
  partition table и assets сохранены; write hash и отдельный `verify_flash`
  успешны.
- Live smoke подтвердил boot, Wi-Fi, platform/OTA/runtime events, `:8080/ws`,
  read-only identity, threshold `0.380000`, wake word и голосовой диалог без
  panic/watchdog/reset loop. Внешние motion-команды не отправлялись.
- Тёплые интервалы ASR-to-first-TTS: `1.990 s` и `1.280 s`; холодный wake до
  WebSocket session — `2.620 s`, до первого TTS — `7.200 s`. Предупреждение
  server/device sample rate `16000/24000` остаётся хвостом. Найденные причины
  LEDC reservation warnings вынесены в отдельную локальную правку выше; этот
  установленный live-кандидат ещё содержит прежний код.

## Локальный hardening URL/AFSK 2026-08-28

- Ветка `codex/firmware-log-hardening-20260828` от `28eb7584aaeef0cb66aa3c967bf4a162f49b3d0b` закрывает дополнительную статическую защиту диагностических сообщений. Сетевые адреса продолжают использоваться в коде для подключения, но в логи, экранные сообщения и ошибки теперь отдаётся только обезличенное описание URL: схема и список скрытых частей без значений `userinfo`, host/IP, port, path, query, fragment и token.
- Добавлен общий helper `firmware/main/diagnostic_redaction.*` и static guard `firmware/scripts/check_sensitive_logging.py`. Guard — это локальная статическая проверка исходников: она не запускает устройство, а ищет опасные места, где полный URL, activation body или Wi-Fi payload могли бы попасть в диагностику.
- AFSK-настройка `Wi-Fi` больше не печатает и не показывает полный decoded text с SSID/password. В журнале остаются только длины SSID/password как безопасная диагностика, а на экране остаётся понятное сообщение о получении данных.
- Статические доказательства до commit: host test `diagnostic_redaction_host_test.cc` прошёл; `./scripts/check_sensitive_logging.py` прошёл; `git diff --check` прошёл; каноническая сборка `GOSHA_OTA_URL='<owner-only production endpoint>' python3 scripts/release.py gosha-v1 --name gosha-v1` прошла, значение endpoint не печаталось и не сохранялось.
- Размеры итоговой статической сборки: `build/gosha.bin` — `3654368` байт; `build/merged-binary.bin` — `13790351` байт; `releases/v2.2.2_gosha-v1.zip` — `6390834` байт. SHA-256: `gosha.bin` — `94473bb2864e73c0897bf7b5116941508641089691de4f60baef826fbbe245cb`; `merged-binary.bin` — `0b43aa4d988427bafbec366b7b666984c8bdaf9b408946a19496994f33ab0187`; ZIP — `9084ec24e10e61fe096ae303a8c4f65315d80f8553581484eb3986b5c1e072fb`.
- Артефакты этой проверки подтверждают статическую сборку. Для установки
  собрать новый кандидат из отдельной ветки, подготовить rollback и выполнить
  USB/serial, flash и reboot по аппаратной политике. Motion и trim требуют
  отдельной команды.
- Follow-up review wave2 нашёл и закрыл дополнительный NO-GO: `self.screen.snapshot` больше не печатает raw response body после upload. Ответ HTTP всё ещё дочитывается для корректного закрытия соединения, но в журнале остаётся только факт успешной загрузки и пометка, что тело скрыто. Static guard расширен на схему `ReadAll()` плюс последующий `ESP_LOG*`.

## Статический firmware remediation-gate 2026-08-27

- База remediation — опубликованный `feature/firmware-orange-eyes @ 8ac1e3f`; итоговый кандидат опубликован в Draft PR `#24` на `7751a3ca326174d217536f6a8de7c09433c3e955`. Исходный аудит GPT-5.5/xhigh вернул `NO-GO`: сетевой адрес временного relay находился в OTA-default и README, vendored `78__esp-ml307` не проходил `git diff --check`, а runtime-логи раскрывали Wi-Fi-пароль и activation payload.
- OTA/config endpoint вынесен в обязательную owner-only переменную `GOSHA_OTA_URL`. Скрипт сборки валидирует абсолютный HTTP(S)-URL, запрещает встроенные учётные данные и не печатает значение. `TEMP_NL_RELAY` не является допустимым production-default.
- Пустой endpoint обрабатывается fail-fast, без экспоненциального цикла повторов. `self.otto.stop` теперь останавливает текущую задачу без постановки `ACTION_HOME`.
- Чувствительные Wi-Fi/activation-логи обезличены; HTTP-клиент не печатает `Authorization`. `runtime_events` применяется replace-whole и очищает старый endpoint/token при отсутствии полной секции. Vendored whitespace очищен, `git diff --check origin/main` проходит.
- Неиспользуемый upstream symlink `_codeql_detected_source_root` удалён, поскольку secure workspace-export AI Office отклоняет symlink как `unsupported_git_entry` до запуска модели.
- Каноническая сборка ESP-IDF 5.5.2 с неразрешимым тестовым `.invalid` endpoint прошла без flash: `gosha.bin` — `3652384` байт, свободно 12% app-раздела. Собранный ZIP — только статическое доказательство компиляции, устанавливать его нельзя.
- Immutable AI Office task `task-20260827T104756Z-immutable-terminal-firmware-pr-24-gate-at-7751a3c` на фактическом `GPT-5.5 / xhigh` завершилась terminal `PASS` без P0/P1/P2 и подтвердила точный remote/PR head. Статический gate закрыт; PR остаётся Draft/Open без merge. На `2026-09-03` владелец подтвердил, что левая серва физически отключена, поэтому flash, USB/serial и перезагрузка разрешены по аппаратной политике; motion и trim остаются отдельным механическим допуском.

## Контрольная точка нового робота 2026-08-25

- Временный сетевой канал принят как рабочий обход: `TEMP_NL_RELAY` временно прокидывает роботу путь к `PRIMARY_PLATFORM_SERVER`. Основной серверный контур остаётся на `PRIMARY_PLATFORM_SERVER`; relay не считается новым production-сервером и должен быть заменён на `FUTURE_PRODUCTION_SERVER` в конце месяца.
- Роли портов фиксируются логическими именами без IP-адресов, секретов и хардкода в прошивке: `18876` — HTTP-контур панели, OTA и config; `18080` — голосовой `WebSocket` и совместимый `MCP`.
- Новый физический робот подтверждён в живом контуре как `gosha-main`: Android показывает `Гоша Main`, робот разговаривает, голосовой сценарий принят.
- Firmware quality gate для `feature/firmware-orange-eyes @ 07d5f6658b6c70c81626ebcc3fbee930ced94fc6`: на `2026-08-25` Draft PR был допустим, а merge и установка были остановлены по конкретным причинам.
- Причины: старая документация называла `self.otto.stop` безопасной проверкой, хотя этот инструмент ставит `ACTION_HOME` и запускает движение; `git diff --check` падал на хвостовых пробелах в vendored-компоненте `firmware/local_components/78__esp-ml307`; существующий на тот момент `firmware/releases/v2.2.2_gosha-v1.zip` содержал устаревший merged-образ и должен был быть пересобран перед публикацией артефакта.
- В этой исторической точке аппаратные действия ещё не выполнялись. С
  `2026-09-03` прошивка, перезагрузка и неподвижная проверка `:8080` разрешены
  по актуальной аппаратной политике; движения, `self.otto.set_trim` и servo
  sequence требуют отдельной команды.

## Контрольная точка нового робота 2026-08-24

- Для безопасного различения физических роботов в актуальную оранжевую ветку добавлен локальный read-only контракт `gosha.identity.get/result` на существующем WebSocket `/ws`.
- Ответ формируется непосредственно локальным сервером и не уходит в облачный канал; он содержит аппаратный `device_id` для сравнения приложением. Реальные значения личности не журналируются и не фиксируются в документации.
- Каноническая полная сборка `gosha-v1` после переноса завершилась успешно; образ не записывался на устройство.
- Неисправный левый сервопривод теперь физически отключён от платы; прошивка,
  перезагрузка и неподвижные проверки разрешены. Команды остальным приводам
  остаются отдельным механическим этапом.
- Сетевой блокер нового робота был отсутствием прямого маршрута до публичного узла платформы после успешной настройки домашнего Wi-Fi; код прошивки его не маскировал, а на 2026-08-25 обход сделан внешним relay.

## Живая установка на новый робот `2026-08-23`

- Новый робот до любой записи определён как `ESP32-S3` ревизии `v0.2` с `16 MB` flash-памяти и `8 MB PSRAM`; защищённая загрузка `Secure Boot` и шифрование flash-памяти отключены.
- Перед изменением снята полная резервная копия всех `16 MB` flash-памяти. Копия хранится вне Git с правами доступа только владельцу; SHA-256: `eafd78a41bc0fff36cdf6a41307c7393db621bec68ef1934c8da1fbc9adbc289`.
- Заводская таблица разделов оказалась несовместима с `gosha-v1`: она использовала разделы `model`, `ota_0`, `ota_1` и `playlist` с другими адресами, тогда как профиль `gosha-v1` ожидает `ota_0` по `0x20000`, `ota_1` по `0x410000` и `assets` по `0x800000`.
- Поэтому безопасная для уже совместимого робота точечная запись `gosha.bin` и `generated_assets.bin` для этого нового устройства была неприменима. После полного резервного копирования выполнена первичная установка объединённого образа по `0x0`.
- Установлен `firmware/build/merged-binary.bin` из ветки `feature/firmware-orange-eyes` на коммите `80310104e895d02d648364460e82d0c2b31e8ba8`; SHA-256 исходного артефакта: `7c1d9a52b69acae30a838502218a3751481484e1585a99b2137fae8df3200b70`.
- Запись завершилась встроенной проверкой хэша, отдельная команда `verify_flash` повторно подтвердила совпадение образа с flash-памятью.
- Первый контролируемый запуск подтвердил:
  - проект `gosha`, версия `2.2.2`;
  - `ESP-IDF 5.5.2`;
  - вариант платы `non-camera`;
  - `8 MB PSRAM` на `80 MHz`;
  - выводы сервоприводов `LL=17`, `RL=39`, `LF=18`, `RF=38`, `LH=8`, `RH=12`;
  - нулевые подстройки всех шести сервоприводов;
  - запуск локального `WebSocket` на `8080`;
  - отсутствие паники, сторожевого таймера, сбросов из-за просадки питания и цикла перезагрузок в контрольном окне.
- Предупреждения `ledc` по выводам сервоприводов и подсветки повторились. Они не являются безусловным отказом драйвера: исправный правый сервопривод отработал и на штатном правом, и на левом канале.
- Перекрёстный аппаратный тест локализовал дефект левой руки:
  - исправный правый сервопривод двигался после подключения к левому каналу, поэтому `GPIO8`, левый разъём и программный путь `Home` выдают управление;
  - подозрительный сервопривод левой руки не запускался на заведомо рабочем правом канале и начинал движение только после ручного подталкивания;
  - дефект следует за физическим сервоприводом или его кабелем, а не за прошивкой или `GPIO8`.
- Левую серву необходимо держать отключённой и не подталкивать под питанием.
  Настройку `trim` и движения исправных приводов не выполнять без отдельной
  явной команды владельца.
- После подбора совместимой сервы её нужно сначала проверить без качалки и нагрузки, выполнить `Home`, затем установить качалку в правильном положении и только после этого провести малый тест левой руки штатным управляющим путём.
- Ближайшая безопасная работа без движения: экран и оранжевые глаза, звук, микрофон, настройка Wi-Fi, подключение к платформе, статус, батарея и события общего контура.

## Текущая работа: тёмно-оранжевая приборная панель `gosha-v1`

- Отдельное рабочее дерево: `/home/max/worktrees/gosha/firmware-orange-eyes`.
- Ветка: `feature/firmware-orange-eyes`, база — стабильный прошивочный контур `af4de9c`.
- Для `gosha-v1` задан единый тёмно-оранжевый оттенок `#E06F00` через параметр `GOSHA_UI_ACCENT_COLOR_HEX`.
- Один параметр одновременно передаётся сборщику GIF-глаз и коду интерфейса платы, поэтому оттенок не дублируется в разных местах.
- Этим цветом оформлены анимированные глаза, Wi-Fi, батарея, беззвучный режим, центральный статус, уведомления и резервный значок эмоции.
- Фон `gosha-v1` закреплён как чистый чёрный при каждом применении темы; фоновый цвет или изображение из будущего пакета ресурсов не переопределят экранный профиль платы.
- Критическое предупреждение о низком заряде намеренно остаётся красным, чтобы аварийный смысл не потерялся.
- Сборщик ресурсов теперь умеет параметрически перекрашивать серые записи глобальных и локальных палитр GIF с сохранением яркости и сглаживания.
- Другие платы и путь сборки без `--emoji_tint` не изменены.
- Проверены все 21 GIF-эмоции и 692 кадра: итоговые пиксели, прозрачность и геометрия соответствуют преобразованию в `#E06F00`.
- Каноническая сборка `python3 scripts/release.py gosha-v1 --name gosha-v1` завершилась успешно.
- SHA-256 `firmware/build/gosha.bin`: `6d466b9a2a6299fc1cd73048b30452f051163026b199268fc3e92638d5dd481d`.
- SHA-256 `firmware/build/generated_assets.bin`: `12520722b9a56c0b687d072cb668e2f0ede0260b3a7fdef365abe81015231516`.
- SHA-256 `firmware/releases/v2.2.2_gosha-v1.zip`: `da43c5836f3fdadd595317c311c03538d3155ed3d8194a7513d31fdaaa185682`.
- Приложение занимает `0x37b2e0` байт и имеет 12% запаса в разделе; ресурсный образ укладывается в выделенные `8 MB`.
- ИИ-офис `task-20260723T090714Z-read-only-review-firmware-dark-orange-instrument-panel` не нашёл кодовых P0/P1/P2; найденный P2 относился только к устаревшим контрольным документам и исправлен этой записью.
- Финальная проверка ИИ-офиса `task-20260723T091854Z-final-read-only-review-dark-orange-instrument-profile` также не нашла P0/P1/P2 и подтвердила единый источник цвета, чёрный фон, сохранение красного аварийного сигнала и ограничение изменений платой `gosha-v1`.
- Текущий пакет меняет и код интерфейса, и GIF-ресурсы. Для аппаратной проверки допустима только явная согласованная запись `gosha.bin` в раздел приложения по `0x20000` и `generated_assets.bin` в раздел ресурсов по `0x800000`.
- Нельзя использовать полный набор `flash_args` или объединённый образ: NVS, `otadata`, таблица разделов и загрузчик должны остаться без изменений.
- Раздел ресурсов занимает `5401743` байта и укладывается в выделенные `8 MB`.
- Перед установкой сняты точные резервные копии действовавшей пары: приложение `/tmp/gosha-app-before-dark-orange-20260723.bin`, SHA-256 `8046e3804b3d1f5f741e81c7e821894dffd618941cad6792742da4918ef31fd4`; ресурсы `/tmp/gosha-assets-before-dark-orange-20260723.bin`, SHA-256 `871b1b74b98741c48c34d178588ae816fdba2e8197af1a36c46a3667acf70729`.
- Новый приборный профиль `#E06F00` установлен на тестовый робот согласованной парой: приложение записано только по `0x20000`, ресурсы — только по `0x800000`. NVS, `otadata`, таблица разделов и загрузчик не записывались.
- Повторная проверка памяти через `verify_flash` подтвердила совпадение обоих установленных разделов с собранными файлами.
- Повторное контрольное окно UART `2026-07-23` продлено до 600 секунд: свободная память оставалась стабильной, минимальное наблюдённое значение `66999` байт; паники, сторожевого таймера и повторной загрузки не было.
- На этой точной установленной паре робот продолжал отправлять свежие `robot.runtime.heartbeat`, пережил повторный цикл отключения и восстановления Wi-Fi телефона и сохранил штатные локальные проверочные обмены.
- OTA для привязанного устройства вернул рабочую конфигурацию голоса и событий; обновление прошивки не предлагалось.
- Обезличенное доказательство общего прогона: `/home/max/AI_OFFICE/local_only/ai-office/logs/task-20260723T111058Z-read-only/live-validation/acceptance-evidence.json`, SHA-256 `600c3d526dddf95a39c38c1cf126a952d02a2e6b7b506f8a591b6f94d7690f0e`.
- Следующий ручной шаг — визуально подтвердить оттенок на матрице и проверить Wi-Fi, батарею, центральный статус и основные эмоции, затем повторить слово пробуждения и один голосовой диалог.
- Ресурсный образ содержит не только GIF, но и текущую модель слова пробуждения и её метаданные. Поэтому такая точечная установка допустима только на совместимый образ того же профиля, а не на произвольную старую прошивку.

## Текущая работа: прошивочная сторона общего контура

- Отдельная ветка: `feature/firmware-triangle-runtime` от `origin/main` (`0980c2a`).
- Первый живой образ общего контура выявил P1-дефект времени жизни сетевого клиента: после пассивного закрытия HTTP задача приёма ещё завершала обратный вызов, а `HttpClient` и `EspTcp` уже могли быть уничтожены. Это проявлялось как `Interrupt WDT` и цикл перезапуска.
- После первого сбоя устройство сразу возвращено на стабильный образ из `main`; NVS, ресурсы и загрузчик не изменялись.
- Исправление закреплено как воспроизводимое локальное переопределение компонента `78/esp-ml307` версии `3.6.5`:
  - `HttpClient::Close()` выполняет завершение транспорта и после пассивного разрыва;
  - `EspTcp` и `EspSsl` будят задачу приёма через `shutdown()` и не освобождают транспорт до её фактического выхода;
  - для служебного HTTP событий выделен свободный `connect_id=4`;
  - ответ сервера полностью дочитывается перед закрытием соединения.
- OTA-ответ платформы может передать закрытую конфигурацию `runtime_events`; прошивка сохраняет её в отдельном пространстве NVS.
- Добавлен неблокирующий отправитель событий с ограниченной очередью FreeRTOS и отдельной низкоприоритетной задачей.
- События не открывают дополнительный голосовой или локальный WebSocket и не выполняют HTTP в сетевых или автоматных обратных вызовах.
- Прошивка публикует:
  - переходы автомата состояний;
  - переходы сети и безопасные коды ошибок без SSID;
  - периодическое состояние с памятью и батареей.
- Доставка использует привязку `Device-Id` и ключ устройства из OTA; ключ не выводится в журнал.
- Текущая работа не меняет OTA-адрес, голосовой путь `/xiaozhi/v1/`, MCP-ответы или локальный `WSControl :8080`.
- После исправления каноническая полная сборка `gosha-v1` через `scripts/release.py` прошла с локальным переопределением.
- SHA-256 `firmware/build/gosha.bin`: `8046e3804b3d1f5f741e81c7e821894dffd618941cad6792742da4918ef31fd4`.
- SHA-256 `firmware/build/merged-binary.bin`: `515ea9af5613a0f82f7ab64afc6d6acd8d328c156d3dccae3f429d9ea0052c43`.
- SHA-256 `firmware/releases/v2.2.2_gosha-v1.zip`: `e9e84701056c5732075ea7c90a1e7f7e38de8b618fc7e88c23217fe7b2ee752e`.
- Исправленный `gosha.bin` установлен только в раздел приложения по адресу `0x20000`; NVS, ресурсы, таблица разделов и загрузчик сохранены.
- Пассивное наблюдение длилось более 210 секунд и подтвердило:
  - отсутствие `Guru Meditation`, паник, сторожевого таймера, аварийного сброса и повторной загрузки;
  - семь последовательных HTTP-сеансов событий с корректным закрытием;
  - восемнадцать проверочных обменов `WSControl`;
  - стабильную свободную память без нарастающего падения.
- Живая база платформы приняла свежие `robot.runtime.heartbeat` от робота; параллельно продолжали поступать события мобильного клиента.
- Это повторно подтверждено после установки тёмно-оранжевой пары `gosha.bin`/`generated_assets.bin`: runtime-код из `af4de9c` присутствует и продолжает работать в образе `6d466b9a...`.
- Проверки через ИИ-офис:
  - `task-20260722T111402Z-read-only-review-firmware-http-teardown-fix` остановила недостаточное первое исправление;
  - `task-20260722T112159Z-fix-firmware-esptcp-passive-disconnect-lifetime` подготовила локальный компонент;
  - `task-20260722T113915Z-re-review-firmware-tcp-lifetime-override` потребовала распространить защиту на TLS;
  - финальная `task-20260722T115308Z-final-review-firmware-tcp-tls-lifetime-fix` не нашла P0/P1/P2 и разрешила осторожную установку только раздела приложения.

## Сделано

- В `GOSHA_FIRMWARE` унифицированы правила общения агента:
  - корневой и локальные `AGENTS.md` теперь явно требуют понятный русский технический язык;
  - запрещён русско-английский суржик в обычном тексте;
  - для типовых слов зафиксированы русские формы и пояснения.
- Для новых чатов зафиксирован обязательный протокол входа:
  - агент обязан проверять checkpoint-документы, текущую ветку и верхний коммит;
  - в первом содержательном ответе обязан явно назвать ветку, стадию и следующий приоритетный шаг.
- Для параллельной ручной разработки зафиксирована отдельная политика `git worktree`:
  - ручные рабочие деревья выносятся в `/home/max/worktrees/gosha/<контур>-<задача>`;
  - новые прошивочные ветки по умолчанию ответвляются от `main`;
  - длительные аппаратные и акустические исследования выносятся в отдельные ветки `lab/*`.
- Создан отдельный локальный репозиторий `/home/max/GOSHA_FIRMWARE`.
- Для `GOSHA_FIRMWARE` уже настроен удалённый `origin`:
  - `git@github.com:MaxCorpOrg/GOSHA_FIRMWARE.git`
- Выполнен первый `push` в GitHub:
  - ветка `main` уже отслеживает `origin/main`
- Публичный репозиторий прошивки теперь доступен по адресу:
  - `https://github.com/MaxCorpOrg/GOSHA_FIRMWARE`
- Зафиксировано, что `GOSHA_FIRMWARE` живёт отдельно от `GOSHA_PLATFORM` и отдельно от `AI_ROBOT`.
- Подготовлен стартовый комплект правил агента и контрольных документов для будущих прошивочных работ.
- Зафиксирована каноническая исходная база для первого импорта:
  - `/home/max/MAX_CORP_CORE/AI_ROBOT/xiaozhi-esp32`
- Зафиксирована версия исходной базы:
  - `PROJECT_VER "2.2.2"`
- Зафиксирована целевая аппаратная отправная точка первого этапа:
  - профиль `otto-robot`
  - цель нового профиля `gosha-v1`
  - `esp32s3`
  - `partitions/v2/16m.csv`
  - дисплей `ST7789` `240x240`
- Зафиксирован эталон сравнения и отката:
  - `/home/max/MAX_CORP_CORE/AI_ROBOT/new/v2.0.5_otto-robot/merged-binary.bin`
- Общая карта всех связанных контуров теперь зафиксирована в:
  - `/home/max/GOSHA_PLATFORM/docs/GOSHA_PROJECT_MAP_RU.md`
- Созданы отдельные документы:
  - аппаратный манифест
  - pin map, то есть карта аппаратных выводов
  - контрольная точка одноразового импорта
- Выполнен одноразовый импорт исходной базы в подпапку:
  - `/home/max/GOSHA_FIRMWARE/firmware`
- Добавлен локальный `firmware/AGENTS.md` для сборочного корня прошивки.
- Создан собственный профиль платы:
  - `firmware/main/boards/gosha-v1`
- Новый профиль уже подключён в:
  - `firmware/main/CMakeLists.txt`
  - `firmware/main/Kconfig.projbuild`
  - `firmware/main/boards/gosha-v1/config.json`
- Проверено, что `gosha-v1` уже виден сборочному скрипту:
  - `python3 scripts/release.py --list-boards`
- Поднято локальное окружение сборки:
  - `ESP-IDF 5.5.2`
  - путь: `/home/max/esp/esp-idf-v5.5.2`
  - пользовательские инструменты сборки: `cmake`, `ninja` в `/home/max/.local/bin`
- Выполнена первая полная сборка профиля:
  - `gosha-v1`
- Получены первые артефакты сборки:
  - `/home/max/GOSHA_FIRMWARE/firmware/build/merged-binary.bin`
  - `/home/max/GOSHA_FIRMWARE/firmware/build/gosha.bin`
  - `/home/max/GOSHA_FIRMWARE/firmware/releases/v2.2.2_gosha-v1.zip`
- Подтверждено, что merged-образ собирается для:
  - `esp32s3`
  - `16MB flash`
  - режим записи `dio`
  - частота `80MHz`
- Первое тестовое устройство уже прошито образом:
  - `/home/max/GOSHA_FIRMWARE/firmware/build/merged-binary.bin`
- На реальном устройстве подтверждён ранний запуск прошивки:
  - определяется плата `gosha-v1`
  - обнаруживается вариант `non-camera`
  - поднимаются аудио, MCP и `Wi‑Fi`
  - устройство уходит в режим настройки сети и поднимает точку доступа с веб-страницей настройки
  - локальный сервер управления `WebSocket` поднимается на `8080`
- Исправлен адрес OTA по умолчанию для собственной платформы `Гоша`:
  - прошивка больше не указывает на старый OTA/config-маршрут на порту `8876`, который отдавал `404`
  - внешний маршрут OTA переведён на собственный путь:
    - `PRIMARY_PLATFORM_SERVER:18876/gosha/ota/`
  - серверный маршрут `/gosha/ota/` уже поднят и отвечает
  - исправленный merged-образ уже собран и повторно прошит на тестовое устройство
- Исправлен первый реальный сбой инициализации экрана:
  - настройка `preview_image_`
  - применение темы
  - начальная установка эмоции
  Теперь эти шаги выполняются в `SetupUI()`, когда объекты интерфейса уже созданы, а не в конструкторе.
- Выполнен первый видимый продуктовый перевод прошивки на бренд `GOSHA`:
  - язык по умолчанию переключён на русский
  - русская локализация обновлена для экранных подсказок
  - имя точки доступа переведено на семейство `GOSHA-A`
  - имя настройки по Bluetooth переведено на `GOSHA-Setup`
  - внешнее имя платы для пользователя переведено на `GOSHA`
  - внутреннее имя сборки переведено с `xiaozhi` на `gosha`
  - тестовое устройство уже повторно прошито этим образом
- Зафиксирована схема имён точки доступа:
  - используем `GOSHA-A-<хвост MAC>`
  - буква `A` означает первую продуктовую серию
  - хвост `MAC`-адреса сохраняем для уникальности каждого робота
- Для профиля `gosha-v1` включено собственное слово пробуждения:
  - режим `Custom Wake Word`
  - внутренний токен `gosha`
  - пользовательское отображение `Гоша`
- Для профиля `gosha-v1` подготовлено уменьшение ложных срабатываний:
  - `CONFIG_CUSTOM_WAKE_WORD_THRESHOLD`
  - было `20`
  - затем стало `35`
  - затем стало `45`
  - после живой проверки порог снижен до `40`
  - текущий прошитый шаг — `38`
  - основание:
    - в `Kconfig` проекта прямо указано, что меньшее число делает кастомное слово пробуждения более чувствительным
    - в текущей конфигурации `gosha-v1` недоступен `Device AEC`, поэтому часть ложных пробуждений может идти от фоновых звуков и акустического эха
  - промежуточный живой вывод:
    - переход `20 -> 35` уже уменьшил число ложных срабатываний
    - на `45` отклик на имя стал слишком слабым для текущей акустики
    - на `40` отклик вернулся, но всё ещё оставался слабым
    - при громкой речи без имени `Гоша` самопроизвольные пробуждения всё ещё встречаются
- Текущее рабочее решение по слову пробуждения:
  - порог `45` признан слишком жёстким для текущего устройства и окружения
  - порог `40` вернул отклик, но ещё не дал комфортной чувствительности
  - текущая живая рабочая точка — `38`
  - переход на `50` пока не нужен
- Новый образ с порогом `38` уже канонически собран через `scripts/release.py`, прошит на устройство и подтверждён по живому boot log:
  - `set det threshold to 0.380000`
  - это важно, потому что обычный `idf.py build` сам по себе не подтягивает `sdkconfig_append` из `boards/gosha-v1/config.json`
  - для новых production-сборок профильных параметров канонический способ — `GOSHA_OTA_URL='<owner-only production endpoint>' python3 scripts/release.py gosha-v1 --name gosha-v1`; исторический образ был собран до введения обязательной owner-конфигурации
- Собран и прошит новый образ с собственным словом пробуждения.
- На реальном устройстве подтверждён полный живой цикл:
  - устройство вышло в домашний `Wi‑Fi`
  - успешно прошло привязку к панели
  - слово пробуждения `Гоша` сработало без ручного нажатия
  - устройство открыло `WebSocket`-сеанс с голосовым сервером
  - русская фраза `Привет` распознана по смыслу
  - робот ответил голосом по-русски:
    - `Я Гоша, ваш голосовой помощник. Слушаю вас, чем могу помочь?`
- Зафиксировано текущее ограничение голоса:
  - тембр и разнообразие русских голосов сейчас ограничены серверным `EdgeTTS`
  - это не только прошивочная задача; для новых тембров потребуется отдельный следующий пакет в `GOSHA_PLATFORM`
- Выполнен отдельный аудит пользовательских китайских строк в активной плате `gosha-v1`.
- Найденные реальные пользовательские источники:
  - описания `MCP`-инструментов в `otto_controller.cc`
  - описание режима `press-to-talk` в `press_to_talk_mcp_tool.cc`
  - runtime-логи и сообщения ошибок в `otto_controller.cc`, `otto_robot.cc`, `otto_emoji_display.cc`, `power_manager.h`
- Эти пользовательские строки переведены на русский.
- Дополнительно переведён общий живой тайм-аут сетевого подключения в `firmware/main/boards/common/nt26_board.cc`.
- После этого в активном пользовательском слое `gosha-v1` больше не должны появляться китайские подсказки, ошибки и описания инструментов.
- Оставшиеся CJK-символы в репозитории относятся в основном к комментариям исходников, неактивным платам и справочным `zh-*` ресурсам.
- Локализован и исправлен новый дефект локального звука в режиме настройки `Wi‑Fi`:
  - при голосовой подсказке `Подключите телефон к сети ...` фраза начиналась нормально, затем уходила в резкий шум и обрывалась;
  - по живому журналу подтверждено, что проигрывание стартовало один раз, то есть это был не двойной запуск одной и той же фразы;
  - локальные файлы `*.ogg` в `assets/locales/ru-RU` оказались `Opus` `16000 Hz` с пакетами по `80 ms`;
  - в `firmware/main/audio/audio_service.cc` локальные пакеты ошибочно всегда отправлялись в декодер как `60 ms`;
  - в `AudioService` добавлен разбор реальной длительности `Opus`-пакета по `TOC` и подстановка правильного значения в декодер;
  - исправленный `gosha.bin` уже собран через `ninja` и прошит на тестовое устройство через `/dev/ttyACM0`

## На чем остановились

- По языковым правилам работа уже выровнена:
  - агент должен писать понятным русским техническим языком;
  - английский оставляется только в командах, путях, названиях файлов, функций, переменных, веток, коммитов и в коде.
- Аппаратный манифест и pin map уже частично подтверждены на реальном устройстве, но ещё не закрыты полностью.
- Базовый живой голосовой цикл уже подтверждён, поэтому следующая точка остановки — доводка качества и устойчивости:
  - нужно проверить несколько повторных пробуждений словом `Гоша`
  - нужно понаблюдать на уже прошитом пороге `38`, насколько он даёт лучший баланс по сравнению с `20`, `35`, `40` и `45`
  - нужно проверить несколько подряд русских вопросов и ответов
  - нужно оценить влияние предупреждения о частоте:
    - серверный поток `16000`
    - локальный звуковой вывод устройства `24000`
- Для `gosha-v1` отдельно зафиксировано ограничение аудиотракта:
  - эта плата не входит в список конфигураций, где разрешён `CONFIG_USE_DEVICE_AEC`
  - поэтому повышение порога сейчас является первой безопасной мерой, а не окончательным решением всех ложных пробуждений
- Локальный портал настройки в сети точки доступа устройства в одном из прогонов открывался пустой страницей, хотя итоговое подключение робота к домашней сети всё же удалось.
  Это означает, что основной путь уже работает, но сценарий настройки через телефон ещё не отполирован полностью.
- Совместимый путь `WebSocket` пока оставлен как `/xiaozhi/v1/`.
  Это сознательное решение совместимости, а не забытый продуктовый хвост.
- В ходе сборки есть предупреждения компилятора, но не ошибки:
  - повторное определение макросов `_IO`, `_IOR`, `_IOW` в сочетании `esp_video` и `lwip`;
  - устаревший драйвер `adc`;
  - несколько неиспользуемых переменных в `gosha-v1`.
  Эти предупреждения не блокируют работу, но должны быть разобраны после стабилизации живого контура.
- Для качества тембра остаётся связанный внешний хвост:
  - даже при исправной прошивке текущий голос всё ещё ограничен выбранным серверным `TTS`-движком
  - значит жалобы на “ненастоящий детский голос” нельзя закрыть только перепрошивкой
- Русификация активной платы ещё не равна полной очистке всего дерева:
  - в исходниках остаются китайские комментарии
  - в дереве остаются неактивные `zh-*` локали и унаследованные платы
  Это уже отдельная санитарная задача, а не живой пользовательский дефект.
- Текущая ближайшая живая проверка после нового прошитого образа:
  - нужно ушами подтвердить, что фраза настройки `Wi‑Fi` больше не уходит в шипение и не обрывается
  - если остаточный дефект останется, следующим кандидатом на разбор станет путь ресемплинга `16000 -> 24000` или выходной тракт `NoAudioCodecSimplex`

## Что делать дальше

- На новом устройстве держать неисправную левую серву отключённой; прошивка,
  update, перезагрузка и неподвижные проверки разрешены только по аппаратной
  политике: no-motion preflight, backup, verify и rollback. Движения остальных
  приводов требуют отдельной команды.
- Пока сервопривод отключён, проверить безопасные функции без движения: экран, оранжевые глаза, звук, микрофон, настройку Wi-Fi, подключение к платформе, статус, батарею и события.
- После замены сначала проверить серву без качалки и нагрузки, затем только по
  отдельной команде владельца выполнить `Home`, установить качалку в правильном
  положении и провести малый тест штатным управляющим путём.
- Повторно перевести устройство в режим настройки `Wi‑Fi` и живой проверкой подтвердить, что локальная голосовая подсказка теперь произносится полностью без шума и без обрыва.
- Если шум после исправления длительности пакета всё ещё останется, отдельно снять журналы и проверить:
  - влияет ли ресемплинг `16000 -> 24000`
  - не даёт ли выходной тракт `NoAudioCodecSimplex` перегрузку или искажение
- Прогнать несколько повторных проверок слова пробуждения `Гоша` и живого русского ответа.
- На уже прошитом пороге `38` проверить, стал ли отклик на имя достаточно уверенным без возвращения большого числа ложных пробуждений.
- Отдельно оценить качество звука и при необходимости согласовать частоты между сервером и устройством.
- Если отклик на `38` станет хорошим, но ложные срабатывания всё ещё будут заметны, следующим шагом отдельно сравнить:
  - порог `40` как более строгий компромисс
  - влияние собственного динамика на микрофон
  - возможность дополнительного подавления эха на стороне устройства или сервера
- Живые проверки локального `:8080` разрешены при свободном `WSControl`.
  Для базовой проверки доступны команды:
  - `gosha.identity.get`
  - `self.otto.get_status`
  - `self.battery.get_level`
  - `self.otto.get_ip`
- Разобрать локальный портал настройки в сети точки доступа устройства, который в одном из прогонов давал пустую страницу.
- Проверить реальную пригодность выводов сервоприводов и подсветки, потому что журнал даёт предупреждения `ledc` по:
  - `GPIO17`
  - `GPIO39`
  - `GPIO18`
  - `GPIO38`
  - `GPIO8`
  - `GPIO12`
  - `GPIO3`
- Уточнить и подтвердить аппаратный манифест и pin map на реальном устройстве с учётом уже полученных журналов.
- После подключения к рабочей сети подтвердить по журналу и по поведению устройства, что обращение идёт на логический HTTP OTA/config-контур:
  - `PRIMARY_PLATFORM_SERVER:18876/gosha/ota/`
  и ошибка `404` больше не возникает
- Подтвердить по журналу и по поведению, что устройство стабильно использует слово пробуждения без ручного режима прослушивания.
- На следующем этапе совместимости отдельно решить судьбу голосового пути:
  - либо оставить `/xiaozhi/v1/` как внутренний совместимый маршрут
  - либо добавить новый псевдоним `/gosha/v1/` и переводить сервер постепенно, не ломая уже собранный контур
- Отдельным следующим пакетом решить, стоит ли переводить оставшиеся комментарии и вспомогательные `zh-*` ресурсы, не затрагивая рабочий пользовательский слой.
- До замены левой сервы по аппаратной политике можно прогонять только
  no-motion цикл: голос, OTA, MCP и read-only `status`/`battery`. После замены
  левой сервы, проверки локального `WebSocket`, настройки сети и отдельной
  механической приёмки можно отдельно разрешить одно безопасное действие без
  риска для железа.
- После стабилизации первого запуска разобрать и, где нужно, убрать текущие предупреждения сборки.
- Если позже появятся новые локальные `AGENTS.md`, переносить в них тот же блок правил русского технического языка.
