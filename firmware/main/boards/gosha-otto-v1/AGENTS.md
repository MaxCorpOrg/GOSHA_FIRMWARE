# AGENTS.md

Эта папка отвечает за конкретную плату `gosha-otto-v1`.

## Перед работой прочитать

1. `/home/max/GOSHA_FIRMWARE/AGENTS.md`
2. `/home/max/GOSHA_FIRMWARE/docs/PROJECT_STATUS_RU.md`
3. `/home/max/GOSHA_FIRMWARE/docs/HARDWARE_MANIFEST_RU.md`
4. `/home/max/GOSHA_FIRMWARE/docs/PIN_MAP_RU.md`

## Что здесь менять

- `config.h`
  - аппаратные и функциональные параметры платы
- `config.json`
  - описание платы для сборочного слоя
- `otto_robot.cc`
  - основная логика профиля платы
- `otto_emoji_display.*`
  - экран, эмоции, локальный интерфейс
- `websocket_control_server.*`
  - локальный управляющий `WebSocket`
- `oscillator.*`, `otto_movements.*`, `otto_controller.cc`
  - движения и поведение робота

## Главные правила

- Не менять выводы, питание, аудио и приводы без сверки с аппаратным манифестом и pin map.
- Если правка влияет на слово пробуждения, звук, экран или локальный портал, это обязательно фиксировать в документации.
- После значимой правки обновлять:
  - `/home/max/GOSHA_FIRMWARE/docs/PROJECT_STATUS_RU.md`
  - `/home/max/GOSHA_FIRMWARE/docs/AGENT_CHECKPOINT_RU.md`
  - `/home/max/GOSHA_FIRMWARE/docs/HARDWARE_MANIFEST_RU.md`, если изменилась реальная аппаратная конфигурация
  - `/home/max/GOSHA_FIRMWARE/docs/PIN_MAP_RU.md`, если изменились или подтвердились выводы
