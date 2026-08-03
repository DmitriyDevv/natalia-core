# Algorithm

Описание уровня `Algorithm` — верхнего уровня прошивки NATALIA Core, который
реализует автомат состояний прибора, режимы работы, разбор протокола CAN и
хранение конфигурации. Здесь же описана хостовая заглушка `board_api_stub`,
через которую этот уровень тестируется без платы.

Аппаратный контракт описан отдельно: [`Board_API.md`](Board_API.md).
Сборка, флаги и общий обзор проекта — в [`README.md`](README.md).

Документ описывает **текущее состояние кода**. Незавершённые места отмечены
явно; там, где поведение запланировано, но не реализовано, это сказано.

---

## 1. Место уровня в проекте

```text
main.c / tests/            <- вызывают Algorithm
    Algorithm              <- этот документ
        Board_API          <- единственный аппаратный контракт
            BSP / Drivers
                регистры
```

`Algorithm` не обращается к регистрам, GPIO, шинам, DMA и обработчикам
прерываний. Любой доступ к аппаратуре — только через `board_api.h`.

Обратное тоже верно: `BSP` и `Drivers` ничего не знают про `SystemContext`,
`SystemEvent`, `SystemState` и `handle_event()`.

---

## 2. Состав уровня

Каталог `Algorithm/`, библиотека CMake `natalia_algorithm`.


| Файл               | Строк | Роль                                                                                                                   |
| ---------------------- | ---------: | -------------------------------------------------------------------------------------------------------------------------- |
| `src/state.c`          |        713 | Автомат состояний:`handle_event()` и восемь обработчиков по режимам.           |
| `src/actions.c`        |       1454 | Действия`action_*` — вся работа, которую автомат поручает выполнить.      |
| `src/algorithm.c`      |        510 | Кооперативные поллеры режимов ERASE / TEST / DUMP / OBSERVE, диспетчер очереди. |
| `src/transport.c`      |       1252 | Протокол: разбор КУ/КТ, сборка ТС, очередь передачи, адресация.          |
| `src/mram_store.c`     |        268 | Структуры и доступ к конфигурации и служебным данным в MRAM.                 |
| `src/event_queue.c`    |        207 | Очередь событий фиксированного размера.                                                 |
| `src/observe.c`        |        120 | Оркестровка режима наблюдений (каркас).                                                   |
| `src/tlm_staging.c`    |         50 | Кольцевой буфер полезной нагрузки телеметрических сообщений.         |
| `src/ni_packet.c`      |         14 | Интерфейс формирования пакетов НИ.**Все тела пустые.**                          |
| `src/board_api_stub.c` |       1216 | Хостовая заглушка Board_API (раздел 11).                                                             |

Заголовки — в `Algorithm/include/`.

---

## 3. Модель исполнения

Главный цикл — один на все режимы, без логики режимов внутри. `main.c`:

```c
while (1) {
    now_ms = timebase_millis();

    transport_poll(&ctx, now_ms);      /* принять сообщения -> положить события  */
    algorithm_poll(&ctx);              /* продвинуть длительную операцию на шаг   */
    algorithm_process_events(&ctx);    /* разобрать очередь -> handle_event()     */
}
```

Каждая из трёх функций делает ограниченную работу и возвращает управление.
Блокирующих ожиданий на уровне `Algorithm` нет.

Последовательность запуска:

```text
clock_init -> timebase_init -> debug_log_init
    -> init_system_context (state = INIT, alarm_mask = ALARM_ALL_MASK)
    -> system_event_queue_init
    -> EVENT_BOOT       (action_init_hardware -> check/restore/load MRAM)
    -> board_comm_init
    -> EVENT_INIT_DONE  (-> DUTY или ALARM)
    -> главный цикл
```

### `algorithm_poll()`

Продвигает поллер активного режима — и только его:


| Состояние | Поллер        |
| ------------------ | ------------------- |
| `STATE_ERASE`      | `erase_mode_poll`   |
| `STATE_TEST`       | `test_mode_poll`    |
| `STATE_OBSERVE`    | `observe_mode_poll` |
| `STATE_DUMP`       | `dump_mode_poll`    |

В `DUTY`, `INIT`, `ALARM`, `SHUTDOWN` не делает ничего.

Поллеры **не вызывают** `handle_event()`. Обнаружив завершение или отказ, они
кладут событие в очередь (`EVENT_ERASE_DONE`, `EVENT_TEST_DONE`,
`EVENT_DUMP_DONE`, `EVENT_NAND_FULL`).

### `algorithm_process_events()`

Единственное место, откуда вызывается `handle_event()`. За один вызов
разбирает не более `ALGORITHM_EVENTS_PER_POLL` событий (по умолчанию **8**,
`algorithm.c:12`), затем возвращает управление — очередь не может занять цикл
целиком.

---

## 4. Очередь событий

`Algorithm/src/event_queue.c`. Кольцевой буфер, статическая память, без
`malloc`. Ёмкость — `SYSTEM_EVENT_QUEUE_CAPACITY`, по умолчанию **32**
элемента типа `SystemEvent`.

```c
void     system_event_queue_init(void);
void     system_event_queue_clear(void);
bool     system_event_queue_push_back(const SystemEvent* event);
bool     system_event_queue_push_front(const SystemEvent* event);
bool     system_event_queue_push_back_type(EventType type);
bool     system_event_queue_push_front_type(EventType type);
bool     system_event_queue_pop(SystemEvent* event);
uint32_t system_event_queue_get_count(void);
uint32_t system_event_queue_get_overflow_count(void);
```

Переполнение не затирает данные: `push_*` возвращает `false` и увеличивает
счётчик, читаемый через `system_event_queue_get_overflow_count()`.

Очередь рассчитана на то, что события в неё могут класть и главный цикл, и
обработчик прерывания. Поэтому каждая операция `push_*` и `pop` на время работы
с указателями буфера запрещает прерывания: сохраняет регистр `PRIMASK`,
выполняет `cpsid i`, меняет указатели и восстанавливает прежнее состояние через
`cpsie i`. Участок короткий — несколько присваиваний, — так что задержка
обработки прерываний пренебрежимо мала.

Этот код собирается только под ARM (`#if defined(__arm__)`). В хостовой сборке
на его месте оказываются пустые функции, потому что прерываний там нет. Из
этого следует практическое ограничение: хостовые тесты проверяют логику
очереди, но не могут проверить её защиту от одновременного доступа —
корректность этой защиты подтверждается только на плате.

`push_front` в рабочем коде не используется покa — приоритетная
постановка предусмотрена (в основном для аварий), но пока не задействована.

---

## 5. События

`EventType` (`state.h:23`) — 27 значений. Кто порождает каждое:


| Событие                                              | Источник                      | Состояние                                                                            |
| ----------------------------------------------------------- | ------------------------------------- | --------------------------------------------------------------------------------------------- |
| `EVENT_CMD_TELEM_REQ`, `EVENT_CMD_STATUS_REQ`, `EVENT_CMD_SET_TIME`         | `transport.c:1141`                    | работает                                                                              |
| `EVENT_CMD_OBSERVE_START`, `EVENT_CMD_OBSERVE_CTRL`, `EVENT_CMD_DUTY`       | `transport.c:1141`                    | работает                                                                              |
| `EVENT_CMD_DUMP`, `EVENT_CMD_SET_CFG`, `EVENT_CMD_ERASE`                    | `transport.c:1141`                    | работает                                                                              |
| `EVENT_CMD_TEST`, `EVENT_CMD_TEST_RESULT`, `EVENT_CMD_SHUTDOWN`, `EVENT_CMD_RESET_ALARM` | `transport.c:1141`       | работает                                                                              |
| `EVENT_TLM_TIME_SYNC`, `EVENT_TLM_ORBIT`, `EVENT_TLM_MAGFIELD`              | `transport.c:1183`                    | работает                                                                              |
| `EVENT_BOOT`, `EVENT_INIT_DONE`                             | `main.c:158` (`pump_internal_event`)  | работает                                                                              |
| `EVENT_ERASE_DONE`                                          | `algorithm.c:21`                      | работает                                                                              |
| `EVENT_TEST_DONE`                                           | `algorithm.c:92`                      | работает                                                                              |
| `EVENT_DUMP_DONE`                                           | `algorithm.c:297`                     | работает                                                                              |
| `EVENT_NAND_FULL`                                           | `algorithm.c:417`                     | работает                                                                              |
| `EVENT_RTC_1HZ`                                             | `observe.c:10` (`observe_on_rtc_1hz`) | **функцию никто не вызывает, кроме теста**                    |
| `EVENT_PED_TRIGGER`                                         | —                                    | **не порождается**; есть только в `tests/firmware/PED_and_CAN_test.c` |
| `EVENT_INIT_FAIL`                                           | —                                    | **не порождается**; обработчик в `state.c:116` есть               |
| `EVENT_MASKED_ALARM_SET` / `_CLEAR`                         | —                                    | **не порождается**; обработчики есть в шести режимах |

### `SystemEvent`

```c
typedef struct {
    EventType      type;
    uint32_t       msg_id;      /* исходный MSG_ID для ответа ACK      */
    uint8_t        tlm_slot;    /* слот tlm_staging для КТ             */
    CommandPayload command;     /* union разобранных параметров команды */
} SystemEvent;
```

`CommandPayload` — union из девяти структур (`CmdErase`, `CmdTest`, `CmdDump`,
`CmdObserveStart`, `CmdObserveCtrl`, `CmdDuty`, `CmdSetTime`, `CmdSetConfig`,
`CmdTestResult`). Событие самодостаточно: после постановки в очередь исходное
сообщение CAN больше не нужно.

Полезная нагрузка КТ в событие не копируется — она кладётся в кольцевой буфер
`tlm_staging` (6 слотов по 128 байт), а в событии остаётся номер слота.

---

## 6. Автомат состояний

`Algorithm/src/state.c`. Восемь состояний:

```text
INIT -> DUTY -> { ERASE, TEST, OBSERVE, DUMP } -> DUTY
  |       |            |                            |
  +-------+------------+----------------------------+--> ALARM --> DUTY
          |                                         |
          +-----------------------------------------+--> SHUTDOWN
```

`handle_event()` обрабатывает **ровно одно** событие и устроен как диспетчер по
текущему состоянию:

```c
switch (ctx->state) {
case STATE_INIT:     return handle_init_event(ctx, event);
case STATE_DUTY:     return handle_duty_event(ctx, event);
case STATE_ERASE:    return handle_erase_event(ctx, event);
case STATE_TEST:     return handle_test_event(ctx, event);
case STATE_OBSERVE:  return handle_observe_event(ctx, event);
case STATE_DUMP:     return handle_dump_event(ctx, event);
case STATE_ALARM:    return handle_alarm_event(ctx, event);
case STATE_SHUTDOWN: return handle_shutdown_event(ctx, event);
}
```

Каждый обработчик — `switch` по `event->type`, где ветви пронумерованы в
комментариях номерами строк из «Таблицы состояний и переходов».

Общие помощники:


| Функция              | Назначение                                                                                   |
| --------------------------- | ------------------------------------------------------------------------------------------------------ |
| `transition_to`             | Смена состояния с сохранением`previous_state`.                               |
| `reject_command`            | Команда недопустима в режиме — ACK с кодом`ERR_MODE` (0x07).           |
| `enter_alarm`               | `action_mark_alarm` -> `action_enter_safe_config` -> переход в `ALARM` -> ТС статуса. |
| `is_alarm_active`           | `ctx->masked_alarm != 0`.                                                                              |
| `finish_command_transition` | ACK + переход + ТС статуса, либо ACK с ошибкой.                            |
| `finish_command_result`     | ACK без перехода.                                                                           |
| `ack_status_from_result`    | `ActionResult` -> код ACK.                                                                          |

### Коды квитанции

Протокол определяет ровно пять кодов. Первый байт ТС «Квитанция» устроен так:
бит 0 — признак отклонения команды (`0` — принята, `1` — отклонена), биты 1–7 —
номер ошибки. Отсюда и получаются «неровные» значения байта:

| Номер ошибки | Обозначение | Байт квитанции | Кто выставляет |
| ---: | --- | --- | --- |
| 0 | `OK` | `0x00` | `ACTION_OK` |
| 1 | `ERR_MSG_ID` | `0x03` | transport — неизвестный `MSG_ID`, событие не создаётся |
| 2 | `ERR_CONTENT` | `0x05` | `ACTION_ERR_CONTENT` |
| 3 | `ERR_MODE` | `0x07` | `reject_command()` — команда недопустима в текущем режиме |
| 4 | `ERR_OTHER` | `0x09` | `ACTION_ERR_OTHER` и `ACTION_ALARM` |

Из четырёх значений `ActionResult` автомат получает только три разных кода —
`ACTION_ALARM` и `ACTION_ERR_OTHER` дают один и тот же байт `0x09`. Это не
недосмотр: отдельного кода «авария» в протоколе нет, а `ERR_OTHER` описан как
«иная ошибка», так что более точного варианта не существует.

Различаются эти два результата не квитанцией, а тем, что происходит после неё:

```c
case ACTION_ALARM:
case ACTION_ERR_OTHER:
default:
    return TRANSPORT_ACK_ERR_OTHER;   /* код один и тот же */
```

```c
(void)action_send_ack_status(event, ack_status_from_result(result));
if (result == ACTION_ALARM) {
    enter_alarm(ctx);                 /* только для ACTION_ALARM */
}
```

То есть `ACTION_ERR_OTHER` — «команду выполнить не удалось, прибор продолжает
работать в прежнем режиме», а `ACTION_ALARM` — «команду выполнить не удалось, и
это авария»: дополнительно выполняются `action_mark_alarm`,
`action_enter_safe_config`, переход в `ALARM` и выдача ТС «Статус». Наземный
сегмент отличает эти случаи по следующему за квитанцией ТС «Статус», а не по
коду квитанции.

Коды `ERR_MSG_ID` и `ERR_MODE` через `ActionResult` не проходят вовсе: первый
выставляет transport ещё до создания события, второй — `reject_command()` в
автомате, когда команда не разрешена в текущем режиме.

---

## 7. `SystemContext`

Единственное состояние уровня. Передаётся указателем во все функции; глобальных
переменных режима нет.

```c
typedef struct {
    SystemState      state;
    SystemState      previous_state;
    uint32_t         alarm_status;      /* признаки аварий, alarm.h        */
    uint32_t         alarm_mask;        /* маска из конфигурации           */
    uint32_t         masked_alarm;      /* alarm_status & alarm_mask       */
    uint16_t         observe_session_id;
    uint16_t         can_control;
    NandRuntimeState nand1, nand2;      /* питание/подключение/заполненность */
    PedRuntimeState  ped;
    UsbRuntimeState  usb;
    EraseContext     erase;             /* контексты режимов             */
    TestContext      test;
    DumpContext      dump;
    ObserveContext   observe;
    ShutdownContext  shutdown;
} SystemContext;
```

У каждого длительного режима — свой контекст с полем `stage` (`EraseStage`,
`TestStage`, `DumpStage`, `ObserveStage`, `ShutdownStage`). Поллер продвигает
`stage`, автомат смотрит на него при завершении.

Контекст крупный: `TestContext` содержит `nerr[2048]` плюс два буфера по
2048 байт, `DumpContext` и `ObserveContext` — по буферу пакета 2048 байт.
`SystemContext` в тестах объявляют как локальную переменную — на хосте это
нормально, но при добавлении полей стоит помнить о размере стека.

---

## 8. Действия (`actions.c`)

45 функций `action_*`, все возвращают `ActionResult`. Автомат решает
**что** делать, действия — **как**. По группам:


| Группа                        | Функции                                                                                                                                                                                  |
| ----------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Запуск и MRAM                | `action_init_hardware`, `action_load_mram`, `action_check_mram`, `action_restore_mram_copy`, `action_mark_init_done`, `action_mark_init_fail`                                                   |
| Аварии                        | `action_mark_alarm`, `action_enter_safe_config`, `action_recalc_masked_alarm`, `action_clear_alarm_status`, `action_mark_alarm_exit`                                                            |
| Ответы                        | `action_send_status`, `action_send_ack`, `action_send_ack_status`, `action_send_dump_ack`, `action_send_telem`, `action_send_test_result`                                                       |
| Конфигурация            | `action_set_time`, `action_apply_config`, `action_write_mram`                                                                                                                                   |
| Пуск режимов             | `action_start_erase`, `action_start_test`, `action_start_dump`, `action_start_observe`, `action_start_shutdown`                                                                                 |
| Завершение режимов | `action_finish_erase`, `action_finish_test`, `action_finish_dump`, `action_finish_observe`, `action_finish_observe_full` + варианты `*_alarm`                                           |
| Служебные данные     | `action_update_service_data`, `action_update_erase_service_data`, `action_update_dump_service_data`, `action_update_test_service_data`, `action_update_test_results`, `action_fix_dump_results` |
| Состояние NAND             | `action_update_nand_state`, `action_clear_nand_full_flag`                                                                                                                                       |
| Наблюдения                | `action_observe_periodic`, `action_handle_ped_trigger`, `action_update_observe_config`, `action_accept_time_sync`, `action_accept_orbit`, `action_accept_magfield`                              |

---

## 9. Transport

`Algorithm/src/transport.c` — граница протокола. Разбирает КУ/КТ, строит
`SystemEvent`, кладёт их в очередь и отправляет ТС. Переходов режимов не
содержит и `handle_event()` не вызывает.

### Команды управления (КУ)


| MSG_ID   | Команда                                              | Событие                                                   |
| -------- | ----------------------------------------------------------- | ---------------------------------------------------------------- |
| `0x0000` | Запрос телеметрии                           | `EVENT_CMD_TELEM_REQ`                                            |
| `0x0001` | Запрос статуса                                 | `EVENT_CMD_STATUS_REQ`                                           |
| `0x0002` | Установка времени                           | `EVENT_CMD_SET_TIME`                                             |
| `0x0003` | Начать наблюдения                           | `EVENT_CMD_OBSERVE_START`                                        |
| `0x0004` | Управление наблюдениями               | `EVENT_CMD_OBSERVE_CTRL`                                         |
| `0x0005` | Дежурный режим                                 | `EVENT_CMD_DUTY`                                                 |
| `0x0006` | Вывод данных                                     | `EVENT_CMD_DUMP`                                                 |
| `0x0007` | Установка конфигурации                 | `EVENT_CMD_SET_CFG`                                              |
| `0x0008` | Стирание ППЗУ                                   | `EVENT_CMD_ERASE`                                                |
| `0x0009` | Тест ППЗУ                                           | `EVENT_CMD_TEST`                                                 |
| `0x000A` | Запрос результатов теста              | `EVENT_CMD_TEST_RESULT`                                          |
| `0x000B` | Выключение                                        | `EVENT_CMD_SHUTDOWN`                                             |
| `0x000C` | Сброс аварии                                     | `EVENT_CMD_RESET_ALARM`                                          |
| `0x0401` | Установка времени от «Спутникс» | `EVENT_CMD_SET_TIME`                                             |
| `0x0A61` | Установка адреса получателя        | обрабатывается в transport, события нет |
| `0x0A62` | Установка адреса прибора              | обрабатывается в transport, события нет |

### Команды телеметрии (КТ)


| MSG_ID   | Содержание                                    | Длина | Событие        |
| -------- | ------------------------------------------------------- | ---------: | --------------------- |
| `0xF210` | Время, орбита, ориентация          |        125 | `EVENT_TLM_TIME_SYNC` |
| `0xF221` | Геомагнитное поле, ориентация |         76 | `EVENT_TLM_MAGFIELD`  |
| `0x0100` | Параметры Мак-Илвайна                |         24 | `EVENT_TLM_ORBIT`     |

### Телеметрические сообщения (ТС)


| MSG_ID   | Сообщение                             | Состояние                                                                            |
| -------- | ---------------------------------------------- | --------------------------------------------------------------------------------------------- |
| `0x0200` | Статус                                   | работает                                                                              |
| `0x0201` | Квитанция (ACK)                       | работает                                                                              |
| `0x0203` | Результаты теста, 6146 байт | работает                                                              |
| `0x0202` | Телеметрия, 100 байт             | **заглушка**: `transport_send_telemetry` возвращает `BOARD_ERR_UNSUPPORTED` |

### Передача

Кольцевая очередь на `TRANSPORT_TX_QUEUE_LENGTH` = 4 коротких сообщения плюс
отдельный буфер `transport_long_tx_buffer` на 6146 байт для ТС 0203h (одно
одновременно, флаг `transport_long_tx_busy`). До `TRANSPORT_TX_MAX_RETRIES` = 3
повторов.

### Адресация

Адрес прибора по умолчанию `0x1E` (НА), получателя — `0x05` (БВ-С). Слово
`can_control` из конфигурации:


| Бит | Значение                                                                                            |
| ------ | ----------------------------------------------------------------------------------------------------------- |
| 0      | Отвечать отправителю запроса, а не по сохранённому адресу. |
| 1      | Игнорировать КУ установки времени`0x0401` от «Спутникс».          |

Адреса, заданные командами `0x0A61` / `0x0A62`, живут только в ОЗУ и в MRAM не
сохраняются.

---

## 10. Хранение в MRAM

`Algorithm/src/mram_store.c`. Две резервируемые копии, запись всегда в
обе (`write_both_copies`), чтение — из первой валидной.

Внутри копии (смещения задаёт `mram_store.c`, копия — 1024 байта):


| Смещение | Область                                                                                                                                                                                                              |
| ---------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `0x0000`         | `MramStoreConfig` — пороги аварий, маска, `can_control`, счётчики, версия.                                                                                                                  |
| `0x0100`         | `MramStoreServiceData` — статус аварий, признаки заполнения банков, счётчики пакетов/стираний/тестов, последний выведенный пакет. |

Результаты теста лежат в отдельных областях и адресуются не смещением, а парой
«копия + банк» через `board_mram_write_test_result` / `_read_test_result`.
Раскладку по адресам держит `BSP/Board_API/src/board_api.c`
(`0x2000` для банка 1, `0x4000` для банка 2), уровню `Algorithm` она не видна.
Размер образа — 6144 байта `Nerr` + 2 байта CRC.

`Nerr` пакуется по 24 бита на блок (2048 блоков), значения выше `0xFFFFFF`
насыщаются. Проверка размера — `_Static_assert` в начале файла.

Если обе копии не проходят CRC, поднимается `ALARM_MRAM`. Записи заводского
образа по умолчанию нет.

---

## 11. Аварии

`Algorithm/include/alarm.h`. 16 признаков:


| Бит | Имя           | Бит | Имя           |
| -----: | ---------------- | -----: | ---------------- |
|      0 | `ALARM_MC_TEMP`  |      8 | `ALARM_PED_PS`   |
|      1 | `ALARM_PU_TEMP`  |      9 | `ALARM_PED_DIR`  |
|      2 | `ALARM_PED_TEMP` |     10 | `ALARM_PED_ST`   |
|      3 | `ALARM_BD_TEMP`  |     11 | `ALARM_NAND_PS`  |
|      4 | `ALARM_PU_VOLT`  |     12 | `ALARM_NAND_PR`  |
|      5 | `ALARM_PU_CURR`  |     13 | `ALARM_USB_VBUS` |
|      6 | `ALARM_PED_VOLT` |     14 | `ALARM_USB_PR`   |
|      7 | `ALARM_PED_CURR` |     15 | `ALARM_MRAM`     |

`masked_alarm = alarm_status & alarm_mask`. Немаскируемые биты (`NAND_PS`,
`NAND_PR`, `USB_PR`) принудительно добавляются в маску функцией
`alarm_sanitize_mask()`.

**Реализовано:** режим `ALARM` как состояние автомата, переходы в него и
выход по `CMD_RESET_ALARM`, пересчёт маски при `CMD_SET_CFG`.

**Не реализовано:** периодический мониторинг параметров (цикл 20 с), который
должен выставлять биты и порождать `EVENT_MASKED_ALARM_SET`. Из 16 признаков
код выставляет только `ALARM_MRAM`.

---

## 12. Режим наблюдений

**Не финальный.** Каркас есть, научная часть отсутствует.

`observe.c` реализует расписание форматов по секундному тику:


| Момент                                                       | Формат                                                                 |
| ------------------------------------------------------------------ | ---------------------------------------------------------------------------- |
| Первый тик сессии                                   | `NI_FORMAT_TELEMETRY` (04h)                                                  |
| Каждую секунду                                        | `NI_FORMAT_COUNTERS` (01h)                                                   |
| Каждую секунду, если`spectrum_mode == 1` / `== 2` | `NI_FORMAT_SPECTRUM_1` / `_2` (02h/03h)                                      |
| Каждые 20 с и при смене конфигурации   | `NI_FORMAT_TELEMETRY` (04h)                                                  |
| При приёме КТ                                           | `NI_FORMAT_SYNC_ORBIT_ATTITUDE` / `_GEOMAGNETIC` / `_MCILWAIN` (05h/06h/07h) |

`observe_start_session()` разбирает слово параметров наблюдений на поля
`events_mode` (биты 0–2), `events_nmax_sel` (3–5), `spectrum_mode` (6–7),
`spectrum_nhist_sel` (8–10).

Ключевое: **`ni_packet.c` — три пустые функции**. `ni_packet_session_begin`,
`ni_packet_session_end` и `ni_packet_write_format` не делают ничего.
Формирование пакета НИ (1024 слова по 16 бит, заголовок 7 слов, CRC16 по
словам 8–1023) и сериализация восьми форматов — отдельная фаза. То есть
расписание форматов работает, а данные не формируются.

Формат `NI_FORMAT_EVENTS` (00h) в расписании отсутствует: он привязан к
триггерам ПЭД, а `EVENT_PED_TRIGGER` пока никто не порождает.

---

## 13. Хостовая заглушка Board_API

`Algorithm/src/board_api_stub.c`, включается флагом `NATALIA_USE_BOARD_STUBS=ON`.
Реализует **все** функции `board_api.h`, поэтому уровень `Algorithm` собирается
и работает на обычном ПК.

Поведение по каждой функции — в [`Board_API.md`](Board_API.md), раздел
«Поведение хостовой заглушки». Здесь — то, что важно при написании тестов.

### Параметры модели


| Параметр          |    Значение | Комментарий                                        |
| ------------------------- | ------------------: | ------------------------------------------------------------- |
| Банков NAND         |                   2 |                                                               |
| Ёмкость банка | **64 пакета** | На плате — 262144. Разница ~4000 раз.       |
| Размер пакета |       2048 байт | Совпадает с полётным.                       |
| Копий MRAM           |                   2 |                                                               |
| Размер копии   |     1024 байта | Совпадает с полётным`BOARD_MRAM_COPY_SIZE`. |

### Что моделируется по-настоящему

- **NAND** — массив в ОЗУ. Запись пакетов, чтение по индексу и потоком,
  счётчик записанных пакетов, признак заполнения, стирание (заполнение `0xFF`).
- **MRAM** — два массива в ОЗУ, чтение/запись по смещению, копирование одной
  копии в другую.
- **Связь** — по одному слоту на приём и на передачу плюс счётчик переданных
  сообщений.

### Что упрощено до констант

Эти функции всегда возвращают `BOARD_OK` и фиксированное значение. Проверить
через них соответствующую логику **нельзя**:


| Функция                                      | Всегда возвращает                                      |
| --------------------------------------------------- | ---------------------------------------------------------------------- |
| `board_mram_check_crc`                              | `is_valid = 1` — копия всегда валидна               |
| `board_nand_erase_is_done`                          | `is_done = 1` — стирание мгновенное                 |
| `board_nand_write_poll` / `board_nand_write_flush`  | `is_idle` / `is_done` = 1                                              |
| `board_nand_is_powered` / `board_ped_is_powered`    | `1`                                                                    |
| `board_rtc_take_1hz_events`                         | `0` — секундные события не моделируются |
| `board_ped_take_trigger_events`                     | `0` — триггеры ПЭД не моделируются           |
| `board_ped_read_status` / `board_read_power_status` | `0`                                                                    |
| `board_ped_read_event`                              | `bytes_read = 0`                                                       |
| `board_rtc_get_time`                                | `0 с, 0 мс` — время не идёт                             |
| `board_usb_write` / `board_data_write`              | принято всё,`bytes_written = size`                           |
| `board_usb_is_ready` / `board_data_is_ready`        | `1`                                                                    |
| температуры                              | `25000` м°C                                                          |
| `board_read_power_monitor`                          | `bus_voltage_mv = 3300`                                                |

Путь «обе копии MRAM испорчены -> `ALARM_MRAM`»,
таймауты стирания, и всё, что зависит от хода времени, секундных тиков и
триггеров ПЭД, хостовыми тестами не покрывается и покрыто быть не может без
доработки заглушки.

### Точки внедрения

Всё, чем тест может управлять извне:

```c
/* board_stub.h */
void board_stub_set_mram_write_fail(bool fail);   /* board_mram_write -> BOARD_ERR_IO */

/* board_comm_stub.h */
void     board_comm_stub_reset(void);
void     board_comm_stub_inject_rx(uint16_t message_id, uint16_t address_from,
                                   uint16_t address_to, const uint8_t* data,
                                   uint16_t length);
uint32_t board_comm_stub_tx_count(void);
bool     board_comm_stub_last_tx(uint16_t* message_id, uint16_t* address_to,
                                 uint8_t* buffer, uint16_t capacity,
                                 uint16_t* length);
```

Ограничения, о которые легко споткнуться:

- приём — **один слот**: второй `inject_rx` до вызова `transport_poll`
  затирает первый;
- передача — тоже **один слот**: `board_comm_stub_last_tx()` отдаёт только
  последнее сообщение. Чтобы проверить, что ушли и ACK, и ТС статуса, надо
  разбирать очередь по шагам, опираясь на `board_comm_stub_tx_count()`;
- **состояние NAND и MRAM не сбрасывается между тестами.** `board_stub_init_once()`
  срабатывает один раз за процесс, а функции полного сброса нет. Внутри одного
  тестового бинарника данные переносятся из теста в тест. Либо задавайте
  исходное состояние явно, либо учитывайте порядок вызовов.

---

## 14. Как выглядит хостовый тест

Общий шаблон (по `tests/test_erase.c`):

```c
static void begin_test(SystemContext *ctx, SystemState state) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = state;
    ctx->previous_state = state;
    ctx->nand1.bank = NAND_BANK_1;
    ctx->nand2.bank = NAND_BANK_2;

    system_event_queue_init();
    board_comm_stub_reset();
    transport_reset();          /* иначе состояние TX течёт между тестами */
}
```

Дальше — два способа подачи воздействия:

```c
/* 1. Через протокол: проверяется и разбор transport, и автомат */
board_comm_stub_inject_rx(0x0008U, ADDR_BVS, ADDR_NA, payload, 6U);
transport_poll(&ctx, 0U);
algorithm_process_events(&ctx);

/* 2. Напрямую событием: проверяется только автомат */
SystemEvent event = { .type = EVENT_CMD_ERASE };
event.command.erase.bank = NAND_BANK_1;
system_event_queue_push_back(&event);
algorithm_process_events(&ctx);
```

Длительные режимы прокручиваются циклом со счётчиком-предохранителем — так
тест не зависнет, если режим не завершится:

```c
for (guard = 0U; (guard < 100U) && (ctx->state != STATE_DUTY); ++guard) {
    algorithm_poll(ctx);
    algorithm_process_events(ctx);
}
assert(ctx->state == STATE_DUTY);
```

Проверки — по `SystemContext`, по содержимому MRAM через `mram_store_*` и по
последнему переданному сообщению через `board_comm_stub_last_tx()`.

Тесты используют `assert()` и возвращают `0` из `main()`; регистрация — в
`tests/CMakeLists.txt`.

### Запуск

```bash
cmake -S . -B cmake-build-tests -G Ninja \
  -DNATALIA_BUILD_TESTS=ON \
  -DNATALIA_USE_BOARD_STUBS=ON

cmake --build cmake-build-tests -j 18
ctest --test-dir cmake-build-tests --output-on-failure
```

---

## 15. Что хостовые тесты покрывают


| Тест             | Проверяет                                                                                                               |
| -------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------- |
| `test_state_machine` | Базовые переходы,`EVENT_INIT_FAIL`, `EVENT_RTC_1HZ`                                                                              |
| `test_transport`     | Разбор КУ/КТ, длины, байты-заполнители, сборка ТС 0200h/0201h/0203h                                      |
| `test_set_cfg`       | Декод 66 байт, запись обеих копий, пересчёт маски, слово управления записью         |
| `test_erase`         | Полный цикл ERASE, счётчик стираний, допустимость команд, вход в ALARM                          |
| `test_mode_test`     | Обвязку автомата и инкремент счётчика теста                                                               |
| `test_dump`          | Вывод по USB, ACK с числом пакетов, счётчик последнего пакета                                       |
| `test_shutdown`      | Сохранение служебных данных, отказ записи, допустимость команд                            |
| `test_mram_store`    | Загрузку/запись/восстановление, упаковку`Nerr` в 24 бита, проверку аргументов        |
| `test_observe`       | Расписание форматов и защёлкивание КТ — вызовом`observe_*` напрямую, минуя автомат |

Не покрыто вовсе, потому что кода нет: мониторинг аварий, сбор секундных тиков
и триггеров ПЭД в рабочем цикле, ТС телеметрии 0202h.

---

## 16. Правила уровня

- C11, без C++, без `malloc`/`free`, без динамической памяти.
- Обращение к аппаратуре — только через `board_api.h`.
- `handle_event()` вызывается только из `algorithm_process_events()`.
  Исключение — точечные тесты автомата.
- Поллеры режимов не вызывают `handle_event()`, а ставят события в очередь.
- Обработчики прерываний не трогают `SystemContext` и не вызывают `action_*`.
- Длительные операции кооперативные: ограниченная работа за вызов.
- При изменении `board_api.h` правятся **обе** реализации — прошивочная
  `BSP/Board_API/src/board_api.c` и хостовая `Algorithm/src/board_api_stub.c`.
