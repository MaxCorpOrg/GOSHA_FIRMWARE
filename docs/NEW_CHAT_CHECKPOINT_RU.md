# NEW CHAT CHECKPOINT

Короткая контрольная точка для следующего агента в `GOSHA_FIRMWARE`.

## Сначала прочитать

1. `../AGENTS.md`
2. `NEW_CHAT_CHECKPOINT_RU.md`
3. `AGENT_CHECKPOINT_RU.md`
4. `PROJECT_STATUS_RU.md`
5. `FIRMWARE_IMPORT_CHECKPOINT_RU.md`
6. `HARDWARE_MANIFEST_RU.md`
7. `PIN_MAP_RU.md`

## Последняя зафиксированная точка

- Локальный репозиторий `GOSHA_FIRMWARE` уже создан.
- Дерево исходников прошивки уже импортировано в:
  - `/home/max/GOSHA_FIRMWARE/firmware`
- Канонический источник импорта уже зафиксирован:
  - `/home/max/MAX_CORP_CORE/AI_ROBOT/xiaozhi-esp32`
- Канонический эталон сравнения и отката уже зафиксирован:
  - `/home/max/MAX_CORP_CORE/AI_ROBOT/new/v2.0.5_otto-robot/merged-binary.bin`
- Профиль `gosha-otto-v1` уже создан как копия `otto-robot`.
- Профиль `gosha-otto-v1` уже виден в `scripts/release.py --list-boards`.
- Полный build ещё не запускался, потому что на машине пока не найден установленный `ESP-IDF`.

## Что делать следующим

- Подтвердить аппаратный манифест и pin map.
- Поднять или указать рабочий `ESP-IDF`.
- Собрать первый `merged-binary.bin`.
