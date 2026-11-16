#!/usr/bin/env python3
"""Эмулятор платы установки ВАХ OFET.

TCP-сервер, который отвечает по протоколу из CLAUDE.md (п. 5) и выдаёт
правдоподобные характеристики транзистора. Нужен, чтобы проверить
программу ПК без железа.

Запуск:  python ofet_emulator.py            (порт 5000, p-тип)
         python ofet_emulator.py --type n --port 5000 --speed 10
         python ofet_emulator.py --host 0.0.0.0   (доступен с других ПК сети)
--speed 10 ускоряет все паузы в 10 раз.
В программе ПК указать IP 127.0.0.1 и тот же порт или нажать «Найти плату»
(эмулятор отвечает на UDP-поиск, порт 5001).
"""

import argparse
import asyncio
import math
import random
import time

UBIAS_NOM = 1.65
U_MIN, U_MAX = -18.0, 15.0
RF = [1e3, 1e4, 1e5, 1e6, 1e7, 1.01e8, 1.0101e9]   # Rос диапазонов 1..7
LINK_TIMEOUT_S = 10.0

FLAG_OVL = 1      # перегрузка ТИА (|Uтиа - Uсм| > 1,55 В)
FLAG_LOW = 2      # сигнал ниже 0,12 В на самом чувствительном диапазоне


def sweep_points(start, stop, step):
    """Та же логика, что в программе ПК: последняя точка всегда stop."""
    step = abs(step)
    span = stop - start
    d = 1.0 if span >= 0 else -1.0
    n = int(math.floor(abs(span) / step + 1e-9))
    pts = [start + d * step * i for i in range(n + 1)]
    if abs(pts[-1] - stop) > 1e-9:
        pts.append(stop)
    return pts


class Device:
    """Модель транзистора (квадратичная, с током утечки и шумом)."""

    def __init__(self, ttype):
        self.ttype = ttype
        self.k = 2e-8        # А/В^2
        self.uth = -2.0 if ttype == 'p' else 2.0
        self.ioff = 1e-11

    def current(self, ugs, uds, transistor):
        k = self.k * (1.0 + 0.15 * (transistor - 3))
        s = -1.0 if self.ttype == 'p' else 1.0       # p-тип: всё "зеркально"
        vov = s * (ugs - self.uth)
        vds = s * uds
        if vov <= 0:
            i = 0.0
        elif vds < vov:
            i = k * (vov * vds - vds * vds / 2)
        else:
            i = k / 2 * vov * vov * (1 + 0.02 * vds)
        i = s * i + self.ioff * (uds / 5.0)
        return i * (1 + random.gauss(0, 0.005)) + random.gauss(0, 2e-13)


class Board:
    def __init__(self, args):
        self.args = args
        self.dev = Device(args.type)
        self.transistor = 0
        self.range = 1
        self.auto = True
        self.ugs = 0.0
        self.uds = 0.0
        self.mode = 'IDLE'
        self.sweep_task = None
        self.paused = asyncio.Event()
        self.paused.set()
        self.writer = None
        self.last_rx = time.monotonic()

    # --- вспомогательное
    def ubias(self):
        return UBIAS_NOM + random.gauss(0, 0.0003)

    async def sleep(self, ms):
        await asyncio.sleep(ms / 1000.0 / self.args.speed)

    def send(self, line):
        if self.writer is None:
            return
        print('>', line)
        self.writer.write((line + '\n').encode('ascii'))

    def safe(self):
        self.ugs = 0.0
        self.uds = 0.0

    def measure_raw(self):
        """Uтиа и Uсм так, как их видел бы АЦП (0...3,3 В, шаг 0,806 мВ)."""
        ub = self.ubias()
        i = self.dev.current(self.ugs, self.uds, self.transistor or 1)
        utia = ub - i * RF[self.range - 1]
        utia = min(max(utia, 0.0), 3.3)
        q = 3.3 / 4096
        return round(utia / q) * q, round(ub / q) * q

    async def measure_point(self, window_ms):
        """Одна точка с автовыбором диапазона (CLAUDE.md, п. 3.6)."""
        while True:
            await self.sleep(window_ms)
            utia, ub = self.measure_raw()
            du = abs(utia - ub)
            if self.auto:
                if du > 1.4 and self.range > 1:
                    self.range -= 1
                    await self.sleep(200)
                    continue
                if du < 0.12 and self.range < 7:
                    self.range += 1
                    await self.sleep(200)
                    continue
            flags = 0
            if du > 1.55:
                flags |= FLAG_OVL
            if du < 0.12 and self.range == 7:
                flags |= FLAG_LOW
            i = (ub - utia) / RF[self.range - 1]
            return i, flags

    # --- развёртки
    async def run_sweep(self, kind, ttype, outer, inner, delay_ms, window_ms, ilim, reverse):
        """outer = список Uзи (выходная) или [None] (проходная);
        inner = список значений развёртываемого напряжения."""
        passes = [1, 2] if reverse else [1]
        total = len(outer) * len(inner) * len(passes)
        n = 0
        last_msg = time.monotonic()
        try:
            for ugs_fam in outer:
                if kind == 'OUTP':
                    self.ugs = ugs_fam
                for p in passes:
                    seq = inner if p == 1 else list(reversed(inner))
                    for u in seq:
                        await self.paused.wait()
                        if kind == 'TRANS':
                            self.ugs = u
                        else:
                            self.uds = u
                        # задержка кусками: во время неё PROG не реже раза в 0,5 с
                        left = delay_ms
                        while left > 0:
                            chunk = min(left, 500 * self.args.speed)
                            await self.sleep(chunk)
                            left -= chunk
                            if time.monotonic() - last_msg > 0.5:
                                self.send('PROG;%d;%d' % (n, total))
                                last_msg = time.monotonic()
                        i, flags = await self.measure_point(window_ms)
                        n += 1
                        if abs(i) > ilim:
                            self.safe()
                            self.send('OVERI;%d;%.4E' % (n, i))
                            self.send('ABORT')
                            return
                        self.send('DATA;%d;%d;%.4f;%.4f;%.4E;%d;%d'
                                  % (n, p, self.ugs, self.uds, i, self.range, flags))
                        if time.monotonic() - last_msg > 0.5 or n == total:
                            self.send('PROG;%d;%d' % (n, total))
                            last_msg = time.monotonic()
                if kind == 'OUTP':
                    self.uds = inner[0]
            self.safe()
            self.send('DONE;' + kind)
        except asyncio.CancelledError:
            self.safe()
            raise
        finally:
            self.mode = 'IDLE'
            self.paused.set()

    # --- команды
    def busy(self):
        return self.mode in ('TRANS', 'OUTP', 'PAUSE')

    async def handle(self, line):
        f = line.split(';')
        cmd = f[0]
        try:
            if cmd == 'PING':
                self.send('PONG')
            elif cmd == 'STAT?':
                self.send('STAT;%s;%.4f;%d;%d;%d'
                          % (self.mode, self.ubias(), self.transistor, self.range, 1 if self.auto else 0))
            elif cmd == 'MEAS?':
                if self.busy():
                    return self.send('ERR;MEAS;4')
                i, flags = await self.measure_point(100)
                self.send('MEAS;%.4f;%.4f;%.4f;%.4E;%d;%d'
                          % (self.ubias(), self.ugs, self.uds, i, self.range, flags))
            elif cmd == 'SEL':
                n = int(f[1])
                if self.busy():
                    return self.send('ERR;SEL;4')
                if not 1 <= n <= 5:
                    return self.send('ERR;SEL;1')
                self.safe()
                await self.sleep(4 * 50 + 200)   # импульсы реле + пауза
                self.transistor = n
                self.send('ACK;SEL;%d' % n)
            elif cmd == 'SETV':
                ugs, uds = float(f[1]), float(f[2])
                if self.busy():
                    return self.send('ERR;SETV;4')
                if not (U_MIN <= ugs <= U_MAX and U_MIN <= uds <= U_MAX):
                    return self.send('ERR;SETV;2')
                self.ugs, self.uds = ugs, uds
                self.send('ACK;SETV;%.4f;%.4f' % (ugs, uds))
            elif cmd == 'ZERO':
                if self.sweep_task and not self.sweep_task.done():
                    self.sweep_task.cancel()
                    self.send('ABORT')
                self.safe()
                self.send('ACK;ZERO')
            elif cmd == 'RANGE':
                n = int(f[1])
                if self.busy():
                    return self.send('ERR;RANGE;4')
                if not 0 <= n <= 7:
                    return self.send('ERR;RANGE;1')
                self.auto = n == 0
                self.range = 1 if n == 0 else n
                self.send('ACK;RANGE;%d' % n)
            elif cmd in ('TRANS', 'OUTP'):
                if self.busy():
                    return self.send('ERR;%s;4' % cmd)
                if self.transistor == 0:
                    return self.send('ERR;%s;5' % cmd)
                ttype = f[1]
                if ttype not in ('n', 'p'):
                    return self.send('ERR;%s;1' % cmd)
                if cmd == 'TRANS':
                    uds, g0, g1, gs = map(float, f[2:6])
                    rest = f[6:10]
                    volts = [uds, g0, g1]
                    if gs <= 0 or g0 == g1:
                        return self.send('ERR;TRANS;1')
                    outer, inner = [None], sweep_points(g0, g1, gs)
                else:
                    g0, g1, gs, d0, d1, ds = map(float, f[2:8])
                    rest = f[8:12]
                    volts = [g0, g1, d0, d1]
                    if gs <= 0 or ds <= 0 or g0 == g1 or d0 == d1:
                        return self.send('ERR;OUTP;1')
                    outer, inner = sweep_points(g0, g1, gs), sweep_points(d0, d1, ds)
                delay_ms, window_ms, ilim, rev = int(rest[0]), int(rest[1]), float(rest[2]), rest[3] == '1'
                if not all(U_MIN <= v <= U_MAX for v in volts):
                    return self.send('ERR;%s;2' % cmd)
                if not (100 <= delay_ms <= 60000 and 20 <= window_ms <= 2000
                        and window_ms % 20 == 0 and ilim > 0):
                    return self.send('ERR;%s;1' % cmd)
                self.safe()
                if cmd == 'TRANS':
                    self.uds = uds
                else:
                    self.uds = inner[0]
                self.mode = cmd
                self.send('ACK;' + cmd)
                self.sweep_task = asyncio.ensure_future(
                    self.run_sweep(cmd, ttype, outer, inner, delay_ms, window_ms, ilim, rev))
            elif cmd == 'PAUSE':
                if self.mode in ('TRANS', 'OUTP'):
                    self.paused.clear()
                    self.prev_mode = self.mode
                    self.mode = 'PAUSE'
                self.send('ACK;PAUSE')
            elif cmd == 'RESUME':
                if self.mode == 'PAUSE':
                    self.mode = self.prev_mode
                    self.paused.set()
                self.send('ACK;RESUME')
            elif cmd == 'STOP':
                if self.sweep_task and not self.sweep_task.done():
                    self.sweep_task.cancel()
                self.safe()
                self.mode = 'IDLE'
                self.send('ABORT')
            else:
                self.send('ERR;%s;1' % cmd)
        except (ValueError, IndexError):
            self.send('ERR;%s;1' % cmd)

    # --- сеть
    async def client(self, reader, writer):
        if self.writer is not None:
            writer.close()
            return
        print('Клиент подключён:', writer.get_extra_info('peername'))
        self.writer = writer
        self.last_rx = time.monotonic()
        watchdog = asyncio.ensure_future(self.link_watchdog())
        queue = asyncio.Queue()
        worker = asyncio.ensure_future(self.worker(queue))
        try:
            while True:
                data = await reader.readline()
                if not data:
                    break
                self.last_rx = time.monotonic()
                line = data.decode('ascii', 'replace').strip()
                if not line:
                    continue
                print('<', line)
                if line in ('STOP', 'PAUSE', 'RESUME', 'PING', 'STAT?', 'ZERO'):
                    await self.handle(line)          # срочные команды без очереди
                else:
                    queue.put_nowait(line)          # остальные строго по порядку
                await writer.drain()
        except ConnectionError:
            pass
        finally:
            watchdog.cancel()
            worker.cancel()
            self.lost_link('соединение закрыто')
            self.writer = None
            writer.close()

    async def worker(self, queue):
        while True:
            line = await queue.get()
            await self.handle(line)

    def lost_link(self, why):
        if self.sweep_task and not self.sweep_task.done():
            self.sweep_task.cancel()
        self.safe()
        self.mode = 'IDLE'
        print('Безопасное состояние:', why)

    async def link_watchdog(self):
        while True:
            await asyncio.sleep(1)
            if time.monotonic() - self.last_rx > LINK_TIMEOUT_S:
                self.lost_link('нет команд от ПК дольше %d с' % LINK_TIMEOUT_S)
                self.last_rx = time.monotonic()


DISCOVER_PORT = 5001


class Discover(asyncio.DatagramProtocol):
    """Ответ на UDP-поиск: DISCOVER -> OFET;<IP>;<порт>."""

    def __init__(self, host, port):
        self.host, self.port = host, port

    def connection_made(self, transport):
        self.transport = transport

    def datagram_received(self, data, addr):
        if data.strip() != b'DISCOVER':
            return
        ip = self.host
        if ip in ('0.0.0.0', ''):
            # Узнаём свой адрес в сети запрашивающего
            import socket
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            try:
                s.connect((addr[0], 1))
                ip = s.getsockname()[0]
            finally:
                s.close()
        print('Поиск от %s, отвечаем %s:%d' % (addr[0], ip, self.port))
        self.transport.sendto(('OFET;%s;%d\n' % (ip, self.port)).encode('ascii'), addr)


async def main():
    ap = argparse.ArgumentParser(description='Эмулятор платы ВАХ OFET')
    ap.add_argument('--port', type=int, default=5000)
    ap.add_argument('--host', default='127.0.0.1')
    ap.add_argument('--type', choices=['n', 'p'], default='p', help='тип транзистора в модели')
    ap.add_argument('--speed', type=float, default=1.0, help='ускорение пауз (например, 10)')
    args = ap.parse_args()
    board = Board(args)
    server = await asyncio.start_server(board.client, args.host, args.port)
    loop = asyncio.get_running_loop()
    try:
        import socket
        us = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        us.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)   # порт может быть занят другой программой
        us.bind(('0.0.0.0', DISCOVER_PORT))
        await loop.create_datagram_endpoint(lambda: Discover(args.host, args.port), sock=us)
    except OSError:
        print('Замечание: UDP-порт %d занят другой программой, кнопка «Найти плату» эмулятор не найдёт.\n'
              'Это не мешает работе: в программе впишите IP 127.0.0.1 и порт %d вручную.'
              % (DISCOVER_PORT, args.port))
    print('Эмулятор слушает %s:%d, модель %s-типа, ускорение x%g'
          % (args.host, args.port, args.type, args.speed))
    async with server:
        await server.serve_forever()


if __name__ == '__main__':
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
