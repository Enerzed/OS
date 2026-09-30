import simpy
import random
import numpy as np
import matplotlib.pyplot as plt
from dataclasses import dataclass, field
from typing import List, Tuple, Dict


# Параметры модели

SIM_TIME = 1000
BATCH_MIN, BATCH_MAX = 2, 8
BATCH_DELAY = 5
IO_CHANNELS = 2

# Веса
WEIGHTS = {
    'cpu':      0.33,
    'io':       0.33,
    'balanced': 0.34,
}

# Фазы
PHASE_PARAMS = {
    'cpu':      (20.0, 4.0, 1.0, 0.5),
    'io':       (1.0, 0.5, 20.0, 4.0),
    'balanced': (12.0, 3.0, 12.0, 3.0),
}

SEED = random.randint(0, 99)


# Структуры данных

@dataclass
class Job:
    job_id: int
    batch_id: int
    job_type: str
    arrival_time: float
    cpu_duration: float
    io_duration: float
    start_time: float = None
    completion_time: float = None
    cpu_start: float = None
    cpu_end: float = None
    io_start: float = None
    io_end: float = None


@dataclass
class Stats:
    jobs: List[Job] = field(default_factory=list)
    cpu_busy_intervals: List[Tuple[float, float]] = field(default_factory=list)
    io_busy_intervals: List[Tuple[float, float]] = field(default_factory=list)
    batch_marks: List[Tuple[float, float]] = field(default_factory=list)


# Генерация задания

def make_job(job_id, batch_id, env, rng, weights):
    job_type = rng.choices(
        list(weights.keys()),
        weights=list(weights.values())
    )[0]
    cpu_base, cpu_jit, io_base, io_jit = PHASE_PARAMS[job_type]
    cpu_dur = max(0.1, rng.gauss(cpu_base, cpu_jit))
    io_dur  = max(0.1, rng.gauss(io_base, io_jit))
    return Job(job_id=job_id, batch_id=batch_id, job_type=job_type,
               arrival_time=env.now, cpu_duration=cpu_dur, io_duration=io_dur)


# Процесс задания

def job_process(env, job, cpu, io, stats):
    job.start_time = env.now

    with cpu.request() as req:
        yield req
        job.cpu_start = env.now
        yield env.timeout(job.cpu_duration)
        job.cpu_end = env.now
        stats.cpu_busy_intervals.append((job.cpu_start, job.cpu_end))

    with io.request() as req:
        yield req
        job.io_start = env.now
        yield env.timeout(job.io_duration)
        job.io_end = env.now
        stats.io_busy_intervals.append((job.io_start, job.io_end))

    job.completion_time = env.now
    stats.jobs.append(job)


# ---------------------- Процесс пакета ----------------------

def batch_process(env, batch_id, jobs, cpu, io, stats):
    batch_start = env.now
    procs = [env.process(job_process(env, j, cpu, io, stats)) for j in jobs]
    for p in procs:
        yield p
    batch_end = env.now
    stats.batch_marks.append((batch_start, batch_end))


# ---------------------- Генератор пакетов ----------------------

def batch_generator(env, cpu, io, stats, rng, weights):
    batch_id = 0
    job_id = 0
    while True:
        if env.now >= SIM_TIME:
            return
        batch_size = rng.randint(BATCH_MIN, BATCH_MAX)
        batch = [make_job(job_id + i, batch_id, env, rng, weights)
                 for i in range(batch_size)]
        job_id += batch_size

        yield env.process(batch_process(env, batch_id, batch, cpu, io, stats))

        batch_id += 1
        yield env.timeout(BATCH_DELAY)


# ---------------------- Запуск модели ----------------------

def run_model(weights=None, sim_time=SIM_TIME, seed=SEED):
    if weights is None:
        weights = WEIGHTS
    rng = random.Random(seed)
    env = simpy.Environment()
    cpu = simpy.Resource(env, capacity=1)
    io = simpy.Resource(env, capacity=IO_CHANNELS)
    stats = Stats()

    env.process(batch_generator(env, cpu, io, stats, rng, weights))
    env.run(until=sim_time)
    return stats, env.now


# ---------------------- Метрики ----------------------

def compute_metrics(stats, sim_end):
    completed = [j for j in stats.jobs if j.completion_time is not None]

    throughput = len(completed) / sim_end
    turnarounds = [j.completion_time - j.arrival_time for j in completed]
    avg_turnaround = float(np.mean(turnarounds)) if turnarounds else 0.0

    cpu_busy = sum(e - s for s, e in stats.cpu_busy_intervals)
    io_busy  = sum(e - s for s, e in stats.io_busy_intervals)

    cpu_util = cpu_busy / sim_end
    io_util  = io_busy / (sim_end * IO_CHANNELS)
    cpu_idle = 1.0 - cpu_util

    return {
        'completed': len(completed),
        'throughput': throughput,
        'avg_turnaround': avg_turnaround,
        'cpu_busy': cpu_busy,
        'cpu_idle': sim_end - cpu_busy,
        'cpu_util': cpu_util,
        'cpu_idle_ratio': cpu_idle,
        'io_busy': io_busy,
        'io_util': io_util,
    }


# Серия экспериментов

def run_sweep(fix='io', fixed_value=0.1, varying='cpu',
              values=None, n_seeds=5, sim_time=SIM_TIME):
    if values is None:
        values = [0.05, 0.15, 0.25, 0.35, 0.45, 0.55, 0.65, 0.75, 0.85]

    if varying == fix:
        raise ValueError("varying и fix должны быть разными типами")

    types = ['cpu', 'io', 'balanced']
    third = [t for t in types if t not in (fix, varying)][0]

    results = {
        'varying_value': [],
        'weights_used': [],
        'throughput': [],
        'avg_turnaround': [],
        'cpu_idle_ratio': [],
        'cpu_util': [],
        'io_util': [],
    }

    for v in values:
        remainder = 1.0 - fixed_value - v
        if remainder < -1e-9:
            continue
        remainder = max(0.0, remainder)

        weights = {
            'cpu': 0.0,
            'io': 0.0,
            'balanced': 0.0,
        }
        weights[fix] = fixed_value
        weights[varying] = v
        weights[third] = remainder

        thr, turn, idle, c_u, io_u = [], [], [], [], []
        for s in range(n_seeds):
            seed = random.randint(0, 99)
            stats, sim_end = run_model(weights=weights,
                                       sim_time=sim_time, seed=seed)
            m = compute_metrics(stats, sim_end)
            thr.append(m['throughput'])
            turn.append(m['avg_turnaround'])
            idle.append(m['cpu_idle_ratio'])
            c_u.append(m['cpu_util'])
            io_u.append(m['io_util'])

        results['varying_value'].append(v)
        results['weights_used'].append(weights)
        results['throughput'].append(float(np.mean(thr)))
        results['avg_turnaround'].append(float(np.mean(turn)))
        results['cpu_idle_ratio'].append(float(np.mean(idle)))
        results['cpu_util'].append(float(np.mean(c_u)))
        results['io_util'].append(float(np.mean(io_u)))

    return results


def plot_sweep(results, fix='io', fixed_value=0.1, varying='cpu'):
    x = results['varying_value']

    fig, axes = plt.subplots(2, 2, figsize=(13, 8))

    axes[0, 0].plot(x, results['throughput'], 'o-', color='tab:blue')
    axes[0, 0].set_title('Производительность')
    axes[0, 0].set_ylabel('Заданий / ед. времени')
    axes[0, 0].grid(True, linestyle='--', alpha=0.4)

    axes[0, 1].plot(x, results['avg_turnaround'], 'o-', color='tab:red')
    axes[0, 1].set_title('Среднее оборотное время')
    axes[0, 1].set_ylabel('Время')
    axes[0, 1].grid(True, linestyle='--', alpha=0.4)

    axes[1, 0].plot(x, [v * 100 for v in results['cpu_idle_ratio']],
                    'o-', color='tab:green')
    axes[1, 0].set_title('Простой CPU')
    axes[1, 0].set_ylabel('Простой, %')
    axes[1, 0].grid(True, linestyle='--', alpha=0.4)

    axes[1, 1].plot(x, [v * 100 for v in results['io_util']],
                    'o-', color='tab:orange')
    axes[1, 1].set_title('Загрузка I/O')
    axes[1, 1].set_ylabel('Загрузка, %')
    axes[1, 1].grid(True, linestyle='--', alpha=0.4)

    for ax in axes.flat:
        ax.set_xlabel(f'Доля {varying}-bound задач')

    fig.suptitle(f'{fix}-bound = {fixed_value:.2f}',
                 fontsize=13)
    plt.tight_layout(rect=(0, 0, 1, 0.96))
    plt.show()

# Визуализация одной модели

def _lanes_from_intervals(intervals):
    lanes = []
    for s, e in sorted(intervals):
        placed = False
        for lane in lanes:
            if lane[-1][1] <= s:
                lane.append((s, e))
                placed = True
                break
        if not placed:
            lanes.append([(s, e)])
    return lanes


def plot_timelines(stats, sim_end, batch_marks=None,
                   title='Загруженность ресурсов во времени'):
    fig, (ax_cpu, ax_io) = plt.subplots(
        2, 1, figsize=(14, 7), sharex=True
    )

    cpu_lanes = _lanes_from_intervals(stats.cpu_busy_intervals)
    if cpu_lanes:
        for lane_idx, lane in enumerate(cpu_lanes):
            for s, e in lane:
                ax_cpu.broken_barh(
                    [(s, e - s)], (lane_idx - 0.4, 0.8),
                    facecolors='tab:blue', edgecolors='black', linewidth=0.5
                )
        ax_cpu.set_ylim(-0.6, len(cpu_lanes) - 0.4)
    else:
        ax_cpu.set_ylim(-0.6, 0.6)
    ax_cpu.set_title('Загруженность CPU')
    ax_cpu.grid(True, axis='x', linestyle='--', alpha=0.4)

    io_lanes = _lanes_from_intervals(stats.io_busy_intervals)
    if io_lanes:
        for lane_idx, lane in enumerate(io_lanes):
            for s, e in lane:
                ax_io.broken_barh(
                    [(s, e - s)], (lane_idx - 0.4, 0.8),
                    facecolors='tab:orange', edgecolors='black', linewidth=0.5
                )
        ax_io.set_ylim(-0.6, len(io_lanes) - 0.4)
    else:
        ax_io.set_ylim(-0.6, 0.6)
    ax_io.set_title('Загруженность I/O')
    ax_io.set_xlabel('Время')
    ax_io.grid(True, axis='x', linestyle='--', alpha=0.4)

    if batch_marks:
        for (b_start, b_end) in batch_marks:
            for ax in (ax_cpu, ax_io):
                ax.axvline(b_start, color='green', linestyle='-',
                           linewidth=0.9, alpha=0.7)
                ax.axvline(b_end, color='red', linestyle='-',
                           linewidth=0.9, alpha=0.7)

    ax_cpu.set_xlim(0, sim_end)
    fig.suptitle(title, fontsize=13)
    plt.tight_layout(rect=(0, 0, 1, 0.96))
    plt.show()


# Точка входа

if __name__ == '__main__':
    stats, sim_end = run_model()
    metrics = compute_metrics(stats, sim_end)

    print('Входные параметры:')
    print(f'Время моделирования:            {SIM_TIME}')
    print(f'Пакет:                          [{BATCH_MIN}, {BATCH_MAX}]')
    print(f'Задержка между пакетами:        {BATCH_DELAY}')
    print(f'Каналов I/O:                    {IO_CHANNELS}')
    print(f'Веса:')
    print(f'    CPU:                        {WEIGHTS["cpu"]}')
    print(f'    IO:                         {WEIGHTS["io"]}')
    print(f'    Balanced:                   {WEIGHTS["balanced"]}')
    print(f'Фазы:')
    print(f'    CPU:                        {PHASE_PARAMS["cpu"]}')
    print(f'    I/O:                        {PHASE_PARAMS["io"]}')
    print(f'    Balanced:                   {PHASE_PARAMS["balanced"]}')
    print()

    print('Результаты моделирования:')
    print(f'Завершено заданий:              {metrics["completed"]}')
    print(f'Производительность:             {metrics["throughput"]:.4f}')
    print(f'Среднее оборотное время:        {metrics["avg_turnaround"]:.2f}')
    print(f'Загрузка CPU:                   {metrics["cpu_util"]*100:.1f}%')
    print(f'Простой CPU:                    {metrics["cpu_idle_ratio"]*100:.1f}%')
    print(f'Загрузка I/O:                   {metrics["io_util"]*100:.1f}%')
    print()

    plot_timelines(stats, sim_end, batch_marks=stats.batch_marks)

    # Серия экспериментов
    FIX = 'balanced'          # 'cpu', 'io', 'balanced'
    FIXED_VALUE = 0.0
    VARYING = 'cpu'     # 'cpu', 'io', 'balanced'
    N_SEEDS = 5

    max_v = 1.0 - FIXED_VALUE
    VALUES = [round(x, 3) for x in np.linspace(0.05, max_v - 0.05, 9)]

    print('Серия экспериментов')
    print(f'Фиксируем:       {FIX}-bound = {FIXED_VALUE}')
    print(f'Меняем:          {VARYING}-bound')
    print(f'Значения:        {VALUES}')
    print(f'Число seed:      {N_SEEDS}')
    print()

    sweep = run_sweep(fix=FIX, fixed_value=FIXED_VALUE,
                      varying=VARYING, values=VALUES,
                      n_seeds=N_SEEDS)

    for i, v in enumerate(sweep['varying_value']):
        w = sweep['weights_used'][i]
        print(f'cpu={w["cpu"]:.2f}  io={w["io"]:.2f}  bal={w["balanced"]:.2f}  '
              f'| thr={sweep["throughput"][i]:.4f}  '
              f'turn={sweep["avg_turnaround"][i]:.2f}  '
              f'cpu_idle={sweep["cpu_idle_ratio"][i]*100:.1f}%  '
              f'io_util={sweep["io_util"][i]*100:.1f}%')

    plot_sweep(sweep, fix=FIX, fixed_value=FIXED_VALUE, varying=VARYING)