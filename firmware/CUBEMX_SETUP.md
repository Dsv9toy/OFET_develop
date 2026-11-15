# Настройка проекта STM32CubeIDE (STM32F407ZET6)

Документ для создания проекта в STM32CubeIDE. Пункты, помеченные **TBD**, ждут данных по схеме платы.

## 0. Создание проекта

1. STM32CubeIDE: File -> New -> STM32 Project.
2. Вкладка MCU/MPU Selector: `STM32F407ZETx` -> Next.
3. Project Name: `ofet_fw`, Location: снять галочку Use default location и указать `<репозиторий>\firmware\ofet_fw`.
   Targeted Language: C, Binary Type: Executable, Project Type: STM32Cube -> Finish.
4. Откроется файл `ofet_fw.ioc` (графическая настройка CubeMX). Дальше по пунктам.

## 1. System Core

- **SYS**: Debug = `Serial Wire` (иначе после прошивки отладчик перестанет подключаться). Timebase Source = `SysTick`.
- **RCC**: High Speed Clock (HSE) = `Crystal/Ceramic Resonator`. **TBD: частота кварца МК** (надпись на кварце рядом с МК, обычно 8 или 25 МГц).
- **GPIO** (выходы, начальный уровень Low, если не сказано иное):
  - PA3 = `GPIO_Output`, метка `ETH_NRST`, начальный уровень **High** (сброс PHY, активный низкий).
  - PA6 = `GPIO_Input`, метка `ETH_NINT` (прерывание PHY, пока не используется).
  - 6 линий коммутатора транзисторов: метки `SEL_CH1` ... `SEL_CH6`, Output Push Pull, Pull-down, начальный уровень Low. **TBD: выводы.**
  - Линии реле диапазонов: `RNG_1` ... `RNG_7` (или 14 для бистабильных). **TBD: выводы.**
  - Включение питания ±18 В (если есть): `PWR18_EN`, начальный уровень Low. **TBD.**

## 2. Тактирование (Clock Configuration)

Цель: SYSCLK = HCLK = 168 МГц, APB1 = 42 МГц, APB2 = 84 МГц, 48 МГц для USB не нужен.

| Кварц HSE | PLLM | PLLN | PLLP | PLLQ |
|---|---|---|---|---|
| 8 МГц | 8 | 336 | 2 | 7 |
| 25 МГц | 25 | 336 | 2 | 7 |

System Clock Mux = PLLCLK, AHB Prescaler = 1, APB1 = 4, APB2 = 2. CubeMX сам предложит Flash Latency 5 WS.

## 3. Connectivity

### ETH (Ethernet, PHY LAN8710A, RMII)
- Mode = `RMII`. Выводы (проверены, все допустимы для ETH на F407):

| Сигнал | Вывод |
|---|---|
| ETH_REF_CLK (50 МГц от генератора ASV-50.000MHZ) | PA1 |
| ETH_MDIO | PA2 |
| ETH_CRS_DV | PA7 |
| ETH_MDC | PC1 |
| ETH_RXD0 | PC4 |
| ETH_RXD1 | PC5 |
| ETH_TX_EN | PB11 |
| ETH_TXD0 | PB12 |
| ETH_TXD1 | PB13 |

  CubeMX по умолчанию может поставить TX_EN/TXD0/TXD1 на PG11/PG13/PG14. Нужно вручную щёлкнуть по PB11, PB12, PB13 на картинке корпуса и выбрать соответствующие функции ETH.
- Parameter Settings: Ethernet MAC Address оставить по умолчанию (`00:80:E1:00:00:00`), Rx Mode = `Interrupt Mode`, PHY Address: **0** (у LAN8710A адрес задаётся выводом PHYAD0 при сбросе, обычно 0; прошивка при старте проверит адреса 0...31).
- Advanced Parameters / PHY: если CubeMX спрашивает драйвер, выбрать `LAN8742` (регистры совместимы с LAN8710A).
- NVIC: включить `Ethernet global interrupt`.
- GPIO Settings для всех выводов ETH: Maximum output speed = `Very High`.

### I2C1 (ЦАП AD5693R)
- I2C1 = `I2C`, выводы PB6 = SCL, PB7 = SDA (CubeMX может поставить PB8/PB9, перенести вручную).
- Speed Mode = `Fast Mode`, 400 кГц (на время отладки можно 100 кГц, Standard Mode).

## 4. Analog: ADC1 (Uтиа и Uсм)

**TBD: выводы.** Выводы PA1, PA2, PA3, PA6, PA7, PC1, PC4, PC5 заняты Ethernet. Для АЦП остаются, например, PA0 (IN0), PA4 (IN4), PA5 (IN5), PB0 (IN8), PB1 (IN9), PC0 (IN10), PC2 (IN12), PC3 (IN13).

- Включить два канала (Utia, Ubias).
- Scan Conversion Mode = Enabled, Number Of Conversion = 2, Rank 1 = канал Uтиа, Rank 2 = канал Uсм, Sampling Time = `480 Cycles`.
- External Trigger = `Timer 2 Trigger Out event`, Rising edge. Continuous Conversion = Disabled. DMA Continuous Requests = Enabled.
- DMA Settings: Add -> ADC1, Mode = `Circular`, Data Width = Half Word / Half Word.

## 5. Timers

- **TIM2**: Clock Source = Internal Clock. Prescaler = 83 (84 МГц / 84 = 1 МГц), Counter Period = 199 (5 кГц, т. е. 5000 пар отсчётов в секунду; за 20 мс ровно 100 отсчётов). Trigger Event Selection (TRGO) = `Update Event`.

## 6. Middleware: LwIP

- Включить `LWIP`.
- General Settings: LWIP_DHCP = Enabled (плата сначала ждёт адрес от роутера, при прямом кабеле прошивка сама перейдёт на 192.168.1.10). IP-адрес в CubeMX не задавать, это делается в коде.
- Без FreeRTOS (NO_SYS = 1): вся программа работает в одном цикле `while (1)` с вызовом `MX_LWIP_Process()`.
- Key Options: MEM_SIZE = 16384, TCP_MSS = 1460, TCP_SND_BUF = 5840 (4 * MSS), LWIP_NETIF_LINK_CALLBACK = Enabled.

## 7. Project Manager

- Code Generator: `Generate peripheral initialization as a pair of '.c/.h' files per peripheral` = включить.
- `Keep User Code when re-generating` = включить.
- Сохранить (Ctrl+S), CubeIDE сгенерирует код. Собрать (молоток), проверить, что ошибок нет, и закоммитить папку `firmware/ofet_fw` (без `Debug/` и `Release/`).

## 8. Программатор (ST-Link с платы Nucleo-F446RE)

1. На Nucleo снять **обе** перемычки CN2 (ST-LINK отключается от своего МК на Nucleo).
2. Разъём CN4 (SWD) на Nucleo -> разъём программирования на плате OFET:

| CN4 | Сигнал | Плата OFET |
|---|---|---|
| 1 | VDD_TARGET (только измерение напряжения) | +3,3 В |
| 2 | SWCLK | PA14 |
| 3 | GND | GND |
| 4 | SWDIO | PA13 |
| 5 | NRST | NRST |
| 6 | SWO | PB3 (необязательно) |

3. Плата OFET питается от своего источника, Nucleo по USB к ПК.
4. В CubeIDE: Run -> Debug As -> STM32 C/C++ Application, Debug probe = ST-LINK (ST-LINK GDB server).
