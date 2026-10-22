import sys
from io import TextIOWrapper

sys.stdout = TextIOWrapper(sys.stdout.buffer, encoding='utf-8')
sys.stderr = TextIOWrapper(sys.stderr.buffer, encoding='utf-8')

import simpy
import random
import copy
import numpy as np
import matplotlib.pyplot as plt
from dataclasses import dataclass, field
from typing import List, Tuple, Dict, Optional
from collections import defaultdict


# 
# Параметры модели
# 

SIM_TIME = 10000
BATCH_MIN, BATCH_MAX = 4, 8
BATCH_DELAY = 10
IO_CHANNELS = 1
N_PHASES = 8

QUANTA = [8, 16, 32]
BOOST_PERIOD = 1000

CTX_SWITCH = 1.0

WEIGHTS = {
    'cpu':      0.00,
    'io':       0.00,
    'balanced': 1.00,
}

PHASE_PARAMS = {
    'cpu':      (50.0, 25.0, 10.0, 5.0),
    'io':       (10.0, 5.0, 50.0, 25.0),
    'balanced': (30.0, 15.0, 30.0, 15.0),
}

SEED = random.randint(0, 99)
SEED_BASE = 1000


# 
# Структуры данных
# 

@dataclass
class Phase:
    kind: str
    duration: float


@dataclass
class Job:
    job_id: int
    batch_id: int
    job_type: str
    arrival_time: float
    phases: List[Phase]
    completion_time: Optional[float] = None


@dataclass
class Stats:
    jobs: List[Job] = field(default_factory=list)
    cpu_busy_intervals: List[Tuple[float, float]] = field(default_factory=list)
    io_busy_intervals: List[Tuple[float, float]] = field(default_factory=list)
    batch_marks: List[Tuple[float, float]] = field(default_factory=list)
    switch_count: int = 0
    switch_time: float = 0.0
    last_cpu_task_id: Optional[int] = None


# 
# Генерация задания (время прихода передаётся явно)
# 

def make_job(job_id, batch_id, arrival_time, rng, weights):
    job_type = rng.choices(
        list(weights.keys()), weights=list(weights.values())
    )[0]
    cpu_mean, cpu_std, io_mean, io_std = PHASE_PARAMS[job_type]
    phases = []
    for i in range(N_PHASES):
        if i % 2 == 0:
            d = max(0.1, rng.gauss(cpu_mean, cpu_std))
            phases.append(Phase('cpu', d))
        else:
            d = max(0.1, rng.gauss(io_mean, io_std))
            phases.append(Phase('io', d))
    return Job(job_id=job_id, batch_id=batch_id, job_type=job_type,
               arrival_time=arrival_time, phases=phases)


# 
# Предгенерация нагрузки (ОДИН набор для всех планировщиков)
# 

def generate_workload(rng, weights, sim_time):
    """
    Открытый цикл: партия N приходит в фиксированное время
    batch_arrival[N], не зависящее от планировщика.

    Время следующей партии = текущее + оценка длительности + BATCH_DELAY.
    Оценка = max(Σ CPU, Σ I/O / IO_CHANNELS) — нижняя граница времени
    партии при идеальном перекрытии CPU и I/O.
    """
    jobs = []
    batch_id, job_id = 0, 0
    batch_arrival = 0.0
    while batch_arrival < sim_time:
        batch_size = rng.randint(BATCH_MIN, BATCH_MAX)
        batch_jobs = []
        for i in range(batch_size):
            j = make_job(job_id + i, batch_id, batch_arrival, rng, weights)
            batch_jobs.append(j)
        job_id += batch_size
        jobs.extend(batch_jobs)

        sum_cpu = sum(p.duration for j in batch_jobs
                      for p in j.phases if p.kind == 'cpu')
        sum_io = sum(p.duration for j in batch_jobs
                     for p in j.phases if p.kind == 'io')
        est_duration = max(sum_cpu, sum_io / IO_CHANNELS)

        batch_arrival += est_duration + BATCH_DELAY
        batch_id += 1
    return jobs


def compute_batch_marks(jobs):
    """Для каждого batch_id: (min arrival, max completion или arrival)."""
    by_batch = defaultdict(list)
    for j in jobs:
        by_batch[j.batch_id].append(j)
    marks = []
    for bid in sorted(by_batch):
        batch = by_batch[bid]
        start = min(j.arrival_time for j in batch)
        ends = [j.completion_time for j in batch if j.completion_time is not None]
        end = max(ends) if ends else start
        marks.append((start, end))
    return marks


# 
# Классическая модель (FCFS, без вытеснения) + ctx switch
# 

def classic_job_process(env, job, cpu, io_res, stats, ctx_switch=0.0):
    for phase in job.phases:
        if phase.kind == 'cpu':
            with cpu.request() as req:
                yield req
                if (ctx_switch > 0 and stats.last_cpu_task_id is not None
                        and stats.last_cpu_task_id != job.job_id):
                    yield env.timeout(ctx_switch)
                    stats.switch_count += 1
                    stats.switch_time += ctx_switch
                stats.last_cpu_task_id = job.job_id
                start = env.now
                yield env.timeout(phase.duration)
                stats.cpu_busy_intervals.append((start, env.now))
        else:
            with io_res.request() as req:
                yield req
                start = env.now
                yield env.timeout(phase.duration)
                stats.io_busy_intervals.append((start, env.now))
    job.completion_time = env.now
    stats.jobs.append(job)


# 
# MLFQ с учётом ctx switch
# 

class MLFQ:
    def __init__(self, env, quanta, stats, boost_period=0, ctx_switch=0.0):
        self.env = env
        self.quanta = list(quanta)
        self.n_levels = len(quanta)
        self.waiting = [[] for _ in range(self.n_levels)]
        self.stats = stats
        self.boost_period = boost_period
        self.ctx_switch = ctx_switch
        self.signal = env.event()
        env.process(self._dispatcher())
        if boost_period and boost_period > 0:
            env.process(self._booster())

    def request_cpu(self, duration, initial_level=0, task_id=None):
        event = self.env.event()
        req = {'remaining': duration, 'level': initial_level,
               'event': event, 'task_id': task_id}
        self.waiting[initial_level].append(req)
        if not self.signal.triggered:
            self.signal.succeed()
        return event

    def _dispatcher(self):
        while True:
            picked = None
            for level in range(self.n_levels):
                if self.waiting[level]:
                    picked = self.waiting[level].pop(0)
                    picked['level'] = level
                    break

            if picked is None:
                self.signal = self.env.event()
                yield self.signal
                continue

            if (self.ctx_switch > 0
                    and self.stats.last_cpu_task_id is not None
                    and self.stats.last_cpu_task_id != picked['task_id']):
                yield self.env.timeout(self.ctx_switch)
                self.stats.switch_count += 1
                self.stats.switch_time += self.ctx_switch

            self.stats.last_cpu_task_id = picked['task_id']

            q = self.quanta[picked['level']]
            run_time = min(picked['remaining'], q)
            start = self.env.now
            yield self.env.timeout(run_time)
            self.stats.cpu_busy_intervals.append((start, self.env.now))
            picked['remaining'] -= run_time

            if picked['remaining'] <= 1e-9:
                picked['event'].succeed()
            else:
                new_level = min(picked['level'] + 1, self.n_levels - 1)
                picked['level'] = new_level
                self.waiting[new_level].append(picked)

    def _booster(self):
        while True:
            yield self.env.timeout(self.boost_period)
            moved = False
            for level in range(1, self.n_levels):
                while self.waiting[level]:
                    req = self.waiting[level].pop(0)
                    req['level'] = 0
                    self.waiting[0].append(req)
                    moved = True
            if moved and not self.signal.triggered:
                self.signal.succeed()


def mlfq_job_process(env, job, mlfq, io_res, stats):
    for phase in job.phases:
        if phase.kind == 'cpu':
            yield mlfq.request_cpu(phase.duration, initial_level=0,
                                   task_id=job.job_id)
        else:
            with io_res.request() as req:
                yield req
                start = env.now
                yield env.timeout(phase.duration)
                stats.io_busy_intervals.append((start, env.now))
    job.completion_time = env.now
    stats.jobs.append(job)


# 
# Запуск (принимает готовый список задач)
# 

def run_model(jobs, sim_time=SIM_TIME, scheduler='classic',
              ctx_switch=CTX_SWITCH):
    jobs = copy.deepcopy(jobs)          # изоляция между запусками
    env = simpy.Environment()
    io_res = simpy.Resource(env, capacity=IO_CHANNELS)
    stats = Stats()

    if scheduler == 'classic':
        cpu = simpy.Resource(env, capacity=1)
        def factory(j, s):
            return classic_job_process(env, j, cpu, io_res, s,
                                       ctx_switch=ctx_switch)
    elif scheduler == 'mlfq':
        mlfq = MLFQ(env, quanta=QUANTA, stats=stats,
                    boost_period=BOOST_PERIOD, ctx_switch=ctx_switch)
        def factory(j, s):
            return mlfq_job_process(env, j, mlfq, io_res, s)
    else:
        raise ValueError(f"Неизвестный планировщик: {scheduler}")

    def arrival_process(job):
        if job.arrival_time > env.now:
            yield env.timeout(job.arrival_time - env.now)
        yield env.process(factory(job, stats))

    for j in jobs:
        env.process(arrival_process(j))

    env.run(until=sim_time)
    stats.batch_marks = compute_batch_marks(jobs)
    return stats, env.now


# 
# Метрики
# 

def compute_metrics(stats, sim_end):
    completed = [j for j in stats.jobs if j.completion_time is not None]
    throughput = len(completed) / sim_end
    turnarounds = [j.completion_time - j.arrival_time for j in completed]
    avg_turnaround = float(np.mean(turnarounds)) if turnarounds else 0.0

    by_type = defaultdict(list)
    for j in completed:
        by_type[j.job_type].append(j.completion_time - j.arrival_time)
    turnaround_by_type = {k: float(np.mean(v)) for k, v in by_type.items()}

    cpu_busy = sum(e - s for s, e in stats.cpu_busy_intervals)
    io_busy  = sum(e - s for s, e in stats.io_busy_intervals)

    return {
        'completed': len(completed),
        'throughput': throughput,
        'avg_turnaround': avg_turnaround,
        'turnaround_by_type': turnaround_by_type,
        'cpu_busy': cpu_busy,
        'cpu_util': cpu_busy / sim_end,
        'cpu_idle_ratio': 1.0 - cpu_busy / sim_end,
        'io_busy': io_busy,
        'io_util': io_busy / (sim_end * IO_CHANNELS),
        'switch_count': stats.switch_count,
        'switch_time': stats.switch_time,
        'switch_overhead': stats.switch_time / sim_end,
    }


# 
# Серия экспериментов
# 

def _build_weights(fix, fixed_value, varying, v, third):
    remainder = 1.0 - fixed_value - v
    if remainder < -1e-9:
        return None
    remainder = max(0.0, remainder)
    w = {'cpu': 0.0, 'io': 0.0, 'balanced': 0.0}
    w[fix] = fixed_value
    w[varying] = v
    w[third] = remainder
    return w


def _init_results():
    return {
        'varying_value': [], 'weights_used': [],
        'throughput': [], 'avg_turnaround': [], 'turnaround_by_type': [],
        'cpu_util': [], 'io_util': [],
        'switch_count': [], 'switch_time': [], 'switch_overhead': [],
    }


def _accumulate(acc, m):
    acc['thr'].append(m['throughput'])
    acc['turn'].append(m['avg_turnaround'])
    acc['c_u'].append(m['cpu_util'])
    acc['io_u'].append(m['io_util'])
    acc['sw_c'].append(m['switch_count'])
    acc['sw_t'].append(m['switch_time'])
    acc['sw_o'].append(m['switch_overhead'])
    for k, val in m['turnaround_by_type'].items():
        acc['turn_by_type'][k].append(val)


def _new_acc():
    return {'thr': [], 'turn': [], 'c_u': [], 'io_u': [],
            'sw_c': [], 'sw_t': [], 'sw_o': [],
            'turn_by_type': defaultdict(list)}


def _flush(results_entry, v, weights, acc):
    results_entry['varying_value'].append(v)
    results_entry['weights_used'].append(weights)
    results_entry['throughput'].append(float(np.mean(acc['thr'])))
    results_entry['avg_turnaround'].append(float(np.mean(acc['turn'])))
    results_entry['turnaround_by_type'].append(
        {k: float(np.mean(vs)) for k, vs in acc['turn_by_type'].items()})
    results_entry['cpu_util'].append(float(np.mean(acc['c_u'])))
    results_entry['io_util'].append(float(np.mean(acc['io_u'])))
    results_entry['switch_count'].append(float(np.mean(acc['sw_c'])))
    results_entry['switch_time'].append(float(np.mean(acc['sw_t'])))
    results_entry['switch_overhead'].append(float(np.mean(acc['sw_o'])))


def run_sweep(fix='io', fixed_value=0.1, varying='cpu', values=None,
              n_seeds=5, sim_time=SIM_TIME, scheduler='classic',
              ctx_switch=CTX_SWITCH):
    if values is None:
        values = [0.05, 0.15, 0.25, 0.35, 0.45, 0.55, 0.65, 0.75, 0.85]
    if varying == fix:
        raise ValueError("varying и fix должны быть разными типами")

    types = ['cpu', 'io', 'balanced']
    third = [t for t in types if t not in (fix, varying)][0]
    results = _init_results()

    for v in values:
        weights = _build_weights(fix, fixed_value, varying, v, third)
        if weights is None:
            continue
        acc = _new_acc()
        for s in range(n_seeds):
            rng = random.Random(SEED_BASE + s)
            jobs = generate_workload(rng, weights, sim_time)
            stats, sim_end = run_model(jobs, sim_time, scheduler, ctx_switch)
            _accumulate(acc, compute_metrics(stats, sim_end))
        _flush(results, v, weights, acc)

    return results


def run_sweep_compare(fix='io', fixed_value=0.1, varying='cpu', values=None,
                      n_seeds=5, sim_time=SIM_TIME, ctx_switch=CTX_SWITCH):
    if values is None:
        values = [0.05, 0.15, 0.25, 0.35, 0.45, 0.55, 0.65, 0.75, 0.85]
    if varying == fix:
        raise ValueError("varying и fix должны быть разными типами")

    types = ['cpu', 'io', 'balanced']
    third = [t for t in types if t not in (fix, varying)][0]

    results = {'classic': _init_results(), 'mlfq': _init_results()}

    for v in values:
        weights = _build_weights(fix, fixed_value, varying, v, third)
        if weights is None:
            continue
        acc = {'classic': _new_acc(), 'mlfq': _new_acc()}
        for s in range(n_seeds):
            rng = random.Random(SEED_BASE + s)
            jobs = generate_workload(rng, weights, sim_time)  # ОДИН набор
            for sched in ('classic', 'mlfq'):
                stats, sim_end = run_model(jobs, sim_time, sched, ctx_switch)
                _accumulate(acc[sched], compute_metrics(stats, sim_end))
        for sched in ('classic', 'mlfq'):
            _flush(results[sched], v, weights, acc[sched])

    return results


def plot_sweep_compare(results, fix='io', fixed_value=0.1, varying='cpu'):
    x = results['classic']['varying_value']
    fig, axes = plt.subplots(2, 2, figsize=(14, 9))

    axes[0, 0].plot(x, results['classic']['throughput'], 'o-',  color='tab:blue', label='Classic')
    axes[0, 0].plot(x, results['mlfq']['throughput'],    's--', color='tab:red',  label='MLFQ')
    axes[0, 0].set_title('Производительность')
    axes[0, 0].set_ylabel('Заданий / ед. времени')
    axes[0, 0].grid(True, linestyle='--', alpha=0.4); axes[0, 0].legend()

    axes[0, 1].plot(x, results['classic']['avg_turnaround'], 'o-',  color='tab:blue', label='Classic')
    axes[0, 1].plot(x, results['mlfq']['avg_turnaround'],    's--', color='tab:red',  label='MLFQ')
    axes[0, 1].set_title('Среднее оборотное время')
    axes[0, 1].set_ylabel('Время')
    axes[0, 1].grid(True, linestyle='--', alpha=0.4); axes[0, 1].legend()

    axes[1, 0].plot(x, [v * 100 for v in results['classic']['cpu_util']], 'o-',  color='tab:blue', label='Classic')
    axes[1, 0].plot(x, [v * 100 for v in results['mlfq']['cpu_util']],    's--', color='tab:red',  label='MLFQ')
    axes[1, 0].set_title('Загрузка CPU')
    axes[1, 0].set_ylabel('Загрузка, %')
    axes[1, 0].grid(True, linestyle='--', alpha=0.4); axes[1, 0].legend()

    axes[1, 1].plot(x, [v * 100 for v in results['classic']['switch_overhead']], 'o-',  color='tab:blue', label='Classic')
    axes[1, 1].plot(x, [v * 100 for v in results['mlfq']['switch_overhead']],    's--', color='tab:red',  label='MLFQ')
    axes[1, 1].set_title('Накладные расходы на ctx switch')
    axes[1, 1].set_ylabel('Доля времени, %')
    axes[1, 1].grid(True, linestyle='--', alpha=0.4); axes[1, 1].legend()

    for ax in axes.flat:
        ax.set_xlabel(f'Доля {varying}-bound задач')

    fig.suptitle(f'Classic vs MLFQ | {fix}-bound = {fixed_value:.2f} | '
                 f'ctx_switch = {CTX_SWITCH}', fontsize=13)
    plt.tight_layout(rect=(0, 0, 1, 0.96))
    plt.show()


# 
# Визуализация таймлайнов
# 

def _lanes_from_intervals(intervals):
    lanes = []
    for s, e in sorted(intervals):
        for lane in lanes:
            if lane[-1][1] <= s:
                lane.append((s, e)); break
        else:
            lanes.append([(s, e)])
    return lanes


def plot_timelines(stats, sim_end, batch_marks=None,
                   title='Загруженность ресурсов'):
    fig, (ax_cpu, ax_io) = plt.subplots(2, 1, figsize=(14, 7), sharex=True)

    for ax, intervals, color, name in [
        (ax_cpu, stats.cpu_busy_intervals, 'tab:blue',   'CPU'),
        (ax_io,  stats.io_busy_intervals,  'tab:orange', 'I/O'),
    ]:
        lanes = _lanes_from_intervals(intervals)
        if lanes:
            for li, lane in enumerate(lanes):
                for s, e in lane:
                    ax.broken_barh([(s, e - s)], (li - 0.4, 0.8),
                                   facecolors=color, edgecolors='black',
                                   linewidth=0.5)
            ax.set_ylim(-0.6, len(lanes) - 0.4)
        else:
            ax.set_ylim(-0.6, 0.6)
        ax.set_title(f'Загруженность {name}')
        ax.grid(True, axis='x', linestyle='--', alpha=0.4)

    ax_io.set_xlabel('Время')

    if batch_marks:
        for (bs, be) in batch_marks:
            for ax in (ax_cpu, ax_io):
                ax.axvline(bs, color='green', linestyle='-', linewidth=0.9, alpha=0.7)
                ax.axvline(be, color='red',   linestyle='-', linewidth=0.9, alpha=0.7)

    ax_cpu.set_xlim(0, sim_end)
    fig.suptitle(title, fontsize=13)
    plt.tight_layout(rect=(0, 0, 1, 0.96))
    plt.show()


# 
# Точка входа
# 

if __name__ == '__main__':
    rng = random.Random(SEED)
    jobs = generate_workload(rng, WEIGHTS, SIM_TIME)

    stats_c, end_c = run_model(jobs, scheduler='classic')
    stats_m, end_m = run_model(jobs, scheduler='mlfq')
    m_c = compute_metrics(stats_c, end_c)
    m_m = compute_metrics(stats_m, end_m)

    print('=' * 70)
    print('Параметры модели')
    print('=' * 70)
    print(f'Время моделирования:            {SIM_TIME}')
    print(f'Фаз в задании:                  {N_PHASES} (4 CPU + 4 I/O)')
    print(f'Пакет:                          [{BATCH_MIN}, {BATCH_MAX}]')
    print(f'Задержка между пакетами:        {BATCH_DELAY}')
    print(f'Каналов I/O:                    {IO_CHANNELS}')
    print(f'Кванты MLFQ:                    {QUANTA}')
    print(f'Период boost (MLFQ):            {BOOST_PERIOD}')
    print(f'Стоимость ctx switch:           {CTX_SWITCH}')
    print(f'Seed нагрузки:                  {SEED}')
    print(f'Задач сгенерировано:            {len(jobs)}')
    print(f'  из них batch-партий:          {len({j.batch_id for j in jobs})}')
    print()

    print('=' * 70)
    print('Сравнение:')
    print('=' * 70)
    print(f'{"Метрика":<34}{"Classic":>16}{"MLFQ":>16}')
    print('-' * 66)
    print(f'{"Завершено заданий":<34}{m_c["completed"]:>16d}{m_m["completed"]:>16d}')
    print(f'{"Производительность":<34}{m_c["throughput"]:>16.4f}{m_m["throughput"]:>16.4f}')
    print(f'{"Ср. оборотное время":<34}{m_c["avg_turnaround"]:>16.2f}{m_m["avg_turnaround"]:>16.2f}')
    for t in ['cpu', 'io', 'balanced']:
        print(f'{"  " + t + "-bound":<34}'
              f'{m_c["turnaround_by_type"].get(t, 0):>16.2f}'
              f'{m_m["turnaround_by_type"].get(t, 0):>16.2f}')
    print(f'{"Загрузка CPU, %":<34}'
          f'{m_c["cpu_util"] * 100:>16.1f}{m_m["cpu_util"] * 100:>16.1f}')
    print(f'{"Загрузка I/O, %":<34}'
          f'{m_c["io_util"] * 100:>16.1f}{m_m["io_util"] * 100:>16.1f}')
    print(f'{"Переключений контекста":<34}'
          f'{m_c["switch_count"]:>16d}{m_m["switch_count"]:>16d}')
    print(f'{"Время на ctx switch":<34}'
          f'{m_c["switch_time"]:>16.2f}{m_m["switch_time"]:>16.2f}')
    print()

    plot_timelines(stats_c, end_c, batch_marks=stats_c.batch_marks,
                   title='Classic (FCFS)')
    plot_timelines(stats_m, end_m, batch_marks=stats_m.batch_marks,
                   title='MLFQ')

    FIX, FIXED_VALUE, VARYING, N_SEEDS = 'io', 0.0, 'cpu', 2
    VALUES = [round(x, 3) for x in np.linspace(0.1, 0.9, 5)]

    sweep = run_sweep_compare(fix=FIX, fixed_value=FIXED_VALUE,
                              varying=VARYING, values=VALUES, n_seeds=N_SEEDS)
    plot_sweep_compare(sweep, fix=FIX, fixed_value=FIXED_VALUE, varying=VARYING)