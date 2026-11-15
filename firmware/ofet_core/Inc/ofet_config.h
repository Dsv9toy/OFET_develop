/* Константы прошивки OFET. Значения из CLAUDE.md (п. 3-6). */
#ifndef OFET_CONFIG_H
#define OFET_CONFIG_H

/* Формирователь: Uвых = OFET_UOUT_SPAN * (1 - D / 32768). Для OPA2277/OP07 заменить на 15. */
#define OFET_UOUT_SPAN          17.0f
#define OFET_UOUT_HW_LIMIT      17.0f   /* |Uвых| не больше, В */

/* Пользовательские пределы Uзи и Uси (относительно истока), В */
#define OFET_U_USER_MIN         (-18.0f)
#define OFET_U_USER_MAX         15.0f

#define OFET_UBIAS_NOM          1.65f   /* номинал Uсм, В (до первого измерения) */

/* Реле диапазонов ТИА: 1 = бистабильные (14 линий SET/RESET), 0 = обычные (7 линий) */
#ifndef OFET_RANGE_BISTABLE
#define OFET_RANGE_BISTABLE     1
#endif
#define OFET_N_RANGES           7

/* Реле: импульс, пауза между импульсами, пауза после переключения, мс */
#define OFET_RELAY_PULSE_MS     50
#define OFET_RELAY_GAP_MS       10
#define OFET_RELAY_SETTLE_MS    200
#define OFET_RANGE_OVERLAP_MS   20      /* обычные реле: новое включено, ждём, затем старое выключаем */

/* Автовыбор диапазона по |Uтиа - Uсм|, В */
#define OFET_AUTORANGE_HI       1.4f
#define OFET_AUTORANGE_LO       0.12f
#define OFET_OVERLOAD_U         1.55f
#define OFET_AUTORANGE_MAX_STEPS 10

/* Связь */
#define OFET_LINK_TIMEOUT_MS    10000u  /* нет строк от ПК -> безопасное состояние */
#define OFET_PROG_PERIOD_MS     500u    /* PROG во время развёртки не реже */
#define OFET_LINE_MAX           192     /* максимальная длина строки команды */
#define OFET_QUEUE_LEN          8       /* очередь обычных команд */

/* Ограничения развёртки */
#define OFET_MAX_POINTS         100000L
#define OFET_STARTUP_WINDOW_MS  100u    /* окно измерения Uсм при старте */
#define OFET_MEAS_WINDOW_MS     100u    /* окно для команды MEAS? */

#endif
