#!/usr/bin/env python3
"""Автотесты логики прошивки на симуляторе (виртуальное время).
Запуск: make test (в папке firmware/host_sim)."""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SIM = os.path.join(HERE, 'ofet_sim')
SIM7 = os.path.join(HERE, 'ofet_sim_relay7')

BOOT = '#connect\n#wait 1500\n'
SEL = 'SEL;{n}\n#until ACK;SEL 3000\n'
T_CMD = 'TRANS;p;-5.0000;0.0000;-16.0000;4.0000;200;100;1.0000E-03;{rev}\n'

failed = 0


def run(script, sim=SIM, args=()):
    r = subprocess.run([sim, *args], input=script, capture_output=True, text=True, timeout=120)
    out = []
    for line in r.stdout.splitlines():
        m = re.match(r'(\d+) (\S+) ?(.*)', line)
        if m:
            out.append((int(m.group(1)), m.group(2), m.group(3)))
    return out


def rx(out, prefix=''):
    return [(t, s) for t, d, s in out if d == '<' and s.startswith(prefix)]


def check(name, cond, out=None):
    global failed
    viol = [s for t, d, s in (out or []) if d in ('!!', '??')]
    if cond and not viol:
        print('ok   ', name)
    else:
        failed += 1
        print('FAIL ', name, viol)
        if out:
            for t, d, s in out[-25:]:
                print('      ', t, d, s)


def state(out):
    s = [x for t, d, x in out if d == '==']
    return s[-1] if s else ''


# 1. Проходная туда и обратно
out = run(BOOT + SEL.format(n=2) + 'RANGE;0\n' + T_CMD.format(rev=1) + '#until DONE 60000\n#state\n')
data = rx(out, 'DATA')
check('TRANS: 10 точек, DONE, безопасное состояние в конце',
      len(data) == 10 and rx(out, 'DONE;TRANS') and 'Ugs=0.000' in state(out) and 'Uds=0.000' in state(out), out)
ugs = [float(s.split(';')[3]) for t, s in data]
check('TRANS: порядок Uзи и номера проходов',
      ugs == [0, -4, -8, -12, -16, -16, -12, -8, -4, 0]
      and [int(s.split(';')[2]) for t, s in data] == [1] * 5 + [2] * 5, out)
cur = [float(s.split(';')[5]) for t, s in data]
check('TRANS: ток p-типа отрицательный и растёт по модулю', cur[4] < -5e-7 and abs(cur[0]) < 1e-10, out)

# 2. Сетка точек: последняя точка всегда stop
out = run(BOOT + SEL.format(n=1) + 'TRANS;p;-1.0000;0.0000;-1.0000;0.3000;100;20;1.0000E-03;0\n#until DONE 60000\n')
ugs = [s.split(';')[3] for t, s in rx(out, 'DATA')]
check('сетка 0...-1 шаг 0,3: 0, -0.3, -0.6, -0.9, -1', ugs == ['0.0000', '-0.3000', '-0.6000', '-0.9000', '-1.0000'], out)

# 3. Выходная
out = run(BOOT + SEL.format(n=3) +
          'OUTP;p;0.0000;-8.0000;4.0000;0.0000;-2.0000;1.0000;100;20;1.0000E-03;1\n#until DONE 60000\n')
data = rx(out, 'DATA')
pairs = [(s.split(';')[3], s.split(';')[4], s.split(';')[2]) for t, s in data]
check('OUTP: 3 Uзи x 3 Uси x 2 прохода = 18 точек', len(data) == 18 and rx(out, 'DONE;OUTP'), out)
check('OUTP: внутри Uзи сначала прямой, потом обратный проход по Uси',
      pairs[:6] == [('0.0000', '0.0000', '1'), ('0.0000', '-1.0000', '1'), ('0.0000', '-2.0000', '1'),
                    ('0.0000', '-2.0000', '2'), ('0.0000', '-1.0000', '2'), ('0.0000', '0.0000', '2')], out)

# 4. Превышение тока
out = run(BOOT + SEL.format(n=2) + '#bigcurrent 1\n' + T_CMD.format(rev=0) + '#until ABORT 60000\n#state\n')
msgs = [s for t, s in rx(out)]
i = next((k for k, s in enumerate(msgs) if s.startswith('OVERI')), -1)
check('Ilim: OVERI, затем ABORT, напряжения сняты',
      i >= 0 and msgs[i + 1] == 'ABORT' and not rx(out, 'DONE') and 'Ugs=0.000' in state(out), out)

# 5. STOP во время развёртки
out = run(BOOT + SEL.format(n=2) + T_CMD.format(rev=0) + '#until DATA;2 60000\nSTOP\n#wait 3000\n#state\n')
check('STOP: ABORT, больше нет DATA, безопасное состояние',
      rx(out, 'ABORT') and len(rx(out, 'DATA')) == 2 and 'Ugs=0.000' in state(out), out)

# 6. ZERO во время развёртки
out = run(BOOT + SEL.format(n=2) + T_CMD.format(rev=0) + '#until DATA;1 60000\nZERO\n#wait 2000\n#state\n')
msgs = [s for t, s in rx(out)]
check('ZERO во время развёртки: ABORT и ACK;ZERO', 'ABORT' in msgs and 'ACK;ZERO' in msgs
      and len(rx(out, 'DATA')) == 1 and 'Ugs=0.000' in state(out), out)

# 7. PAUSE / RESUME
out = run(BOOT + SEL.format(n=2) + T_CMD.format(rev=0) + '#until DATA;2 60000\nPAUSE\nSTAT?\n#wait 5000\n'
          'RESUME\n#until DONE 60000\n')
data = rx(out, 'DATA')
t_pause = next(t for t, d, s in out if d == '>' and s == 'PAUSE')
t_res = next(t for t, d, s in out if d == '>' and s == 'RESUME')
check('PAUSE: нет DATA во время паузы, STAT = PAUSE, после RESUME все 5 точек',
      not [1 for t, s in data if t_pause < t < t_res] and rx(out, 'STAT;PAUSE')
      and [s.split(';')[1] for t, s in data] == ['1', '2', '3', '4', '5'], out)

# 8. Ошибки
out = run(BOOT + T_CMD.format(rev=0) + 'SETV;-20.0000;0.0000\nSETV;abc;1\nRANGE;9\nSEL;6\nFOO\n'
          + SEL.format(n=1) + T_CMD.format(rev=0) + 'SETV;1;1\nSEL;2\n#until DONE 60000\n'
          + '#i2cfail 1\nSETV;1.0000;1.0000\n#wait 10\n')
msgs = [s for t, s in rx(out, 'ERR')]
check('коды ошибок 5, 2, 1, 1, 1, 1, 4, 4, 3',
      msgs == ['ERR;TRANS;5', 'ERR;SETV;2', 'ERR;SETV;1', 'ERR;RANGE;1', 'ERR;SEL;1', 'ERR;FOO;1',
               'ERR;SETV;4', 'ERR;SEL;4', 'ERR;SETV;3'], out)

# 9. Выбор каждого транзистора
ok = True
for n in range(1, 6):
    out = run(BOOT + 'SETV;2.0000;3.0000\n' + SEL.format(n=n) + '#state\n')
    ok = ok and f'T={n}' in state(out) and 'Ugs=0.000' in state(out) and rx(out, f'ACK;SEL;{n}')
check('SEL 1...5: реле в нужном положении, напряжения сняты перед переключением', ok, out)

# 10. Фиксированный диапазон
out = run(BOOT + SEL.format(n=2) + 'RANGE;3\n#until ACK;RANGE 3000\nSETV;-10.0000;-5.0000\nMEAS?\n'
          '#until MEAS 3000\nSTAT?\n')
m = rx(out, 'MEAS;')
check('RANGE;3: MEAS на диапазоне 3, STAT авто = 0', m and m[0][1].split(';')[5] == '3'
      and rx(out, 'STAT;IDLE')[0][1].endswith(';3;0'), out)

# 11. Потеря связи
out = run(BOOT + SEL.format(n=2) + 'TRANS;p;-5.0000;0.0000;-16.0000;0.1000;200;100;1.0000E-03;0\n'
          '#wait 15000\n#state\n')
last = max(t for t, s in rx(out, 'DATA'))
check('нет строк от ПК 10 с: развёртка остановлена, напряжения сняты',
      not rx(out, 'DONE') and last < 13000 and 'Ugs=0.000' in state(out), out)

out = run(BOOT + SEL.format(n=2) + T_CMD.format(rev=0) + '#until DATA;2 60000\n#disconnect\n#wait 3000\n#state\n')
check('отключение ПК: развёртка остановлена, напряжения сняты',
      len(rx(out, 'DATA')) == 2 and 'Ugs=0.000' in state(out), out)

# 12. Длинная задержка: сообщения не реже раза в секунду
out = run(BOOT + SEL.format(n=2) + 'TRANS;p;-5.0000;0.0000;-1.0000;1.0000;5000;100;1.0000E-03;0\n'
          '#wait 4000\nPING\n#wait 4000\nPING\n#wait 4000\nPING\n#wait 2000\n')
times = [t for t, s in rx(out) if s.startswith(('DATA', 'PROG', 'DONE'))]
t0 = next(t for t, s in rx(out, 'ACK;TRANS'))
times = [t for t in times if t > t0]
gaps = [b - a for a, b in zip([t0] + times, times)]
check('задержка 5 с: PROG не реже раза в секунду', rx(out, 'DONE') and max(gaps) <= 1000, out)

# 13. Старт без ЦАП: питание ±18 В не включается
out = run('#i2cfail 1\n#connect\n#wait 3000\nSTAT?\n#state\n#i2cfail 0\n#wait 1500\nSTAT?\n#state\n')
st = [x for t, d, x in out if d == '==']
check('нет ответа ЦАП: BOOT, ±18 В выключено; после появления ЦАП старт',
      rx(out, 'STAT;BOOT') and 'pwr=0' in st[0] and rx(out, 'STAT;IDLE') and 'pwr=1' in st[1], out)

# 14. Обычные реле диапазонов (7 линий)
out = run(BOOT + SEL.format(n=2) + 'RANGE;0\n' + T_CMD.format(rev=1) + '#until DONE 60000\n', sim=SIM7)
check('обычные реле диапазонов: проходная без нарушений', len(rx(out, 'DATA')) == 10, out)

# 15. n-тип
out = run(BOOT + SEL.format(n=4) + 'TRANS;n;5.0000;0.0000;15.0000;5.0000;200;100;1.0000E-03;0\n#until DONE 60000\n',
          args=('--n',))
cur = [float(s.split(';')[5]) for t, s in rx(out, 'DATA')]
check('n-тип: ток положительный', len(cur) == 4 and cur[-1] > 5e-7, out)

# 16. Настройки сети
out = run(BOOT + 'NET?\nNETSET;STATIC;192.168.77.10;255.255.255.0;0.0.0.0\nNET?\nNETSET;STATIC;300.1.1.1;255.255.255.0;0.0.0.0\n'
          'NETSET;FOO\nNETSET;AUTO\nNET?\n#wait 10\n')
msgs = [s for t, s in rx(out)]
check('NET? и NETSET: авто, постоянный адрес, ошибки формата',
      msgs[0] == 'NET;AUTO;0.0.0.0;0.0.0.0;0.0.0.0;127.0.0.1' and msgs[1] == 'ACK;NETSET'
      and msgs[2] == 'NET;STATIC;192.168.77.10;255.255.255.0;0.0.0.0;127.0.0.1'
      and msgs[3] == 'ERR;NETSET;1' and msgs[4] == 'ERR;NETSET;1' and msgs[5] == 'ACK;NETSET'
      and msgs[6].startswith('NET;AUTO'), out)

print()
print('Провалено тестов:', failed)
sys.exit(1 if failed else 0)
