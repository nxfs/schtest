# perf script event handlers, generated by perf script -g python
# Licensed under the terms of the GNU GPL License version 2

# The common_* event handler fields are the most useful fields common to
# all events.  They don't necessarily correspond to the 'common_*' fields
# in the format files.  Those fields not available as handler params can
# be retrieved using Python functions of the form common_*(context).
# See the perf-script-python Documentation for the list of available functions.

from __future__ import print_function
from collections import defaultdict

import glob
import math
import os
import re
import sys

sys.path.append(os.environ['PERF_EXEC_PATH'] + \
	'/scripts/python/Perf-Trace-Util/lib/Perf/Trace')

from perf_trace_context import *
from Core import *


class Task:
    def __init__(self, id, pid, cookie, stop_ns, exit_code, cpu_time, runq_wait_time, forceidle_time):
        self.id = id
        self.pid = pid
        self.cookie = cookie
        self.stop_ns = stop_ns
        self.exit_code = exit_code
        self.cpu_time = cpu_time
        self.runq_wait_time = runq_wait_time
        self.forceidle_time = forceidle_time


class SchtestConfig:
    def __init__(self, tasks, cpu_set, cpu_count, cpu_groups, start_ns, stop_ns):
        self.tasks = tasks
        self.cpu_set = cpu_set
        self.cpu_count = cpu_count
        self.cpu_groups = cpu_groups
        self.start_ns = start_ns
        self.stop_ns = stop_ns
        self.pid_to_cookie = {}
        for t in tasks:
            self.pid_to_cookie[t.pid] = t.cookie
        self.cpu_to_group = {}
        self.number_of_cpu_siblings = None
        for idx, g in enumerate(self.cpu_groups):
            if self.number_of_cpu_siblings is None:
                self.number_of_cpu_siblings = len(g)
            else:
                assert(self.number_of_cpu_siblings == len(g))
            for c in g:
                self.cpu_to_group[c] = idx

def parse_cpu_set(cpu_set):
    if len(cpu_set) == 0:
        return None
    if (cpu_set == "empty\n"):
        return None
    cpus = set()
    tok = cpu_set.split(',')
    for t in tok:
        sub_tok = t.split("-")
        if len(sub_tok) == 1:
            cpus.add(int(sub_tok[0]))
        else:
            for c in range(int(sub_tok[0]), int(sub_tok[1]) + 1):
                cpus.add(int(c))
    cpus


def parse_schtest_out():
    path = f"{results_dir}/out.txt"
    f = open(path)
    cpu_set = parse_cpu_set(f.readline())
    print(f"cpu set: {cpu_set}")
    cpu_count = int(f.readline())
    cpu_groups = []
    print("cpu groups:")
    for c in range(cpu_count):
        g = set(map(int, f.readline().split(' ')))
        cpu_groups.append(g)
        print(g)
    task_count = int(f.readline())
    print(f"task count: {task_count}")
    tasks = []
    print("tasks:")
    for p in range(task_count):
        tok = f.readline().split(' ')
        raw_cookie = int(tok[2])
        task = Task(id=int(tok[0]),
                pid=int(tok[1]),
                cookie=raw_cookie if raw_cookie != 0 else None,
                stop_ns=int(tok[3]),
                exit_code=int(tok[4]),
                cpu_time=int(tok[5])/1000000000,
                runq_wait_time=int(tok[6])/1000000000,
                forceidle_time=int(tok[7])/1000000000
            )
        tasks.append(task)
        print(f"id={task.id}, pid={task.pid}, cookie={task.cookie}, stop_ns={task.stop_ns}, exit_code={task.exit_code}")
    tok = f.readline().split(' ')
    start_ns = int(tok[0])
    stop_ns = int(tok[1])
    print(f"start ns: {start_ns}; stop ns: {stop_ns}")
    return SchtestConfig(tasks=tasks, cpu_set=cpu_set, cpu_count=cpu_count, cpu_groups=cpu_groups, start_ns=start_ns, stop_ns=stop_ns)


class Event:
    def __init__(self, event_name, cpu, pid, time, comm):
        self.event_name = event_name
        self.cpu = cpu
        self.pid = pid
        self.time = time
        self.comm = comm


class RuntimeEvent:
    def __init__(self, start, stop, cookie, pid, cpu):
        self.start = start
        self.stop = stop
        self.cookie = cookie
        self.pid = pid
        self.cpu = cpu


class CpuTimeline:
    def __init__(self):
        self.runtime_events = []

    def add_runtime_event(self, run_event):
        self.runtime_events.append(run_event)


class TaskTimeline:
    def __init__(self):
        self.runtime_events = []

    def add_runtime_event(self, run_event):
        self.runtime_events.append(run_event)


class Timeline:
    def __init__(self, cfg):
        self.cfg = cfg
        self.events = []
        self.cpu_timeline = defaultdict(CpuTimeline)
        self.task_timeline = defaultdict(TaskTimeline)

    def add_event(self, event):
        self.events.append(event)

    def add_runtime_event(self, event, runtime):
        start = event.time - runtime
        stop = event.time
        cookie = self.cfg.pid_to_cookie.get(event.pid, None)
        runtime_event = RuntimeEvent(start=start, stop=stop, cookie=cookie, pid=event.pid, cpu=event.cpu)
        self.cpu_timeline[event.cpu].add_runtime_event(runtime_event)
        self.task_timeline[event.pid].add_runtime_event(runtime_event)

    # an overlap is defined by a period of time where two process are sharing an hyperthread with incompatible cookies
    # in other words it's a violation of core scheduling invariants
    def check_overlaps(self):
        overlap_file_path = f"{results_dir}/overlap.txt"
        overlap_file = open(overlap_file_path, 'w')
        print(f"\nChecking overlaps, full results will be written at {overlap_file_path}")
        overlap_buckets = defaultdict(lambda: 0)
        longest_overlap = 0
        self.cpu_timeline = dict(self.cpu_timeline)
        checked_cpus = set()
        for cpu, cpu_timeline in self.cpu_timeline.items():
            if cpu in checked_cpus:
                continue
            cpu_group = None
            for g in self.cfg.cpu_groups:
                if cpu in g:
                    cpu_group = g
                    break
            if cpu_group is None:
                # adding cpu not belonging to groups of interest
                # perf might have picked up other processes or it was recorded before setting up cpu affinity
                # but it's also possible that we have a kernel issue with cpu affinity not respected, so tracking it
                cpu_group = set()
                cpu_group.add(cpu)
            cpu_event_iterators = {}
            cpu_cookie = {}
            cpu_cookie_time = {}
            for c in cpu_group:
                if len(self.cpu_timeline[c].runtime_events) > 0:
                    cpu_event_iterators[c] = 0
                    cpu_cookie[c] = None
                    cpu_cookie_time[c] = None
                if c in checked_cpus:
                    raise Exception(f"CPU already checked: {c}")
                checked_cpus.add(c)

            NO_COOKIE = -1
            check_overlaps = False
            while(len(cpu_event_iterators.keys()) > 1):
                # find next event (start/stop running) across all cpus in group
                next_cpu = None
                next_time = None
                next_cookie = None
                for c in cpu_event_iterators.keys():
                    rte = self.cpu_timeline[c].runtime_events[cpu_event_iterators[c]]
                    if cpu_cookie[c] is None:
                        # schedule in
                        next_cpu_time = rte.start
                        next_cpu_cookie = NO_COOKIE if rte.cookie is None else rte.cookie
                    else:
                        # schedule out
                        next_cpu_time = rte.stop
                        next_cpu_cookie = None
                    if (next_time is None) or (next_cpu_time < next_time):
                        next_cpu = c
                        next_time = next_cpu_time
                        next_cookie = next_cpu_cookie

                if next_time > cfg.stop_ns:
                    break

                check_overlaps |= next_time > cfg.start_ns

                # advance event iterator
                if next_cookie is None:
                    cpu_event_iterators[next_cpu] += 1
                    if cpu_event_iterators[next_cpu] >= len(self.cpu_timeline[next_cpu].runtime_events):
                        del cpu_event_iterators[next_cpu]

                # on schedule out check for overlaps, ignoring early ones before cookies are setup properly
                if next_cookie is None and check_overlaps:
                    for c in cpu_event_iterators.keys():
                        if c == next_cpu:
                            continue
                        if cpu_cookie[c] is not None and cpu_cookie[c] != cpu_cookie[next_cpu] and cpu_cookie_time[c] > cfg.start_ns:
                            overlap = next_time - cpu_cookie_time[c]
                            if overlap > 0:
                                overlap_bucket = int(math.log2(overlap))
                                overlap_buckets[overlap_bucket] += 1
                                print(f"Overlap of {overlap} between CPU {c} with cookie {cpu_cookie[c]} scheduled in at {cpu_cookie_time[c]} and CPU {next_cpu} with cookie {cpu_cookie[next_cpu]} scheduled out at {next_time}", file=overlap_file)
                            if overlap > longest_overlap:
                                longest_overlap = overlap

                # update state
                cpu_cookie[next_cpu] = next_cookie
                cpu_cookie_time[next_cpu] = next_time

        if len(overlap_buckets) > 0:
            overlap_buckets_keys = list(overlap_buckets.keys())
            overlap_buckets_keys.sort()
            nano_to_micro = 1000000
            print(f"Longest overlap: {longest_overlap/nano_to_micro:.1e} ms".replace("e+0", "e+").replace("e-0", "e-"))
            for b in reversed(range(0, overlap_buckets_keys[len(overlap_buckets_keys) - 1] + 1)):
                count = overlap_buckets[b]
                star_count = int(math.log2(count)) + 1 if count > 0 else 0
                low_bucket = (2 ** b) / nano_to_micro
                high_bucket = (2 ** (b + 1)) / nano_to_micro
                print(f"Overlap count [{low_bucket:.1e}, {high_bucket:.1e}) ms: {count:6d}     {'*' * star_count}".replace("e+0", "e+").replace("e-0", "e-"))
        else:
            print("No overlaps were found!")

        overlap_file.close()

    # spread is defined when multiple processes sharing the same cookie are spreading across multiple core when they could actually run on hyperthread siblings
    # spread should be prevented by core affinity
    def check_spread(self):
        spread_file_path = f"{results_dir}/spread.txt"
        spread_file = open(spread_file_path, 'w')
        print(f"\nChecking spread, full results will be written at {spread_file_path}")
        task_accumulated_spread=defaultdict(lambda: 0)
        self.task_timeline = dict(self.task_timeline)
        task_groups = defaultdict(list)
        for task in self.cfg.tasks:
            if task.cookie:
                task_groups[task.cookie].append(task)

        for cookie, task_group in task_groups.items():
            task_event_iterators = {}
            task_cpu = {}
            task_cpu_time = {}
            for t in task_group:
                if len(self.task_timeline[t.pid].runtime_events) > 0:
                    task_event_iterators[t.pid] = 0
                    task_cpu[t.pid] = None
                    task_cpu_time[t.pid] = None

            check_spread = False
            running_count = 0
            while(len(task_event_iterators.keys()) > 1):
                # find next event (start/stop running) across all tasks in group
                next_task = None
                next_time = None
                next_cpu = None
                for t in task_event_iterators.keys():
                    rte = self.task_timeline[t].runtime_events[task_event_iterators[t]]
                    if task_cpu[t] is None:
                        # schedule in
                        next_task_time = rte.start
                        next_task_cpu = rte.cpu
                    else:
                        # schedule out
                        next_task_time = rte.stop
                        next_task_cpu = None
                    if (next_time is None) or (next_task_time < next_time):
                        next_task = t
                        next_time = next_task_time
                        next_cpu = next_task_cpu

                if next_time > cfg.stop_ns:
                    break

                check_spread |= next_time > cfg.start_ns

                # advance event iterator
                if next_cpu is None:
                    task_event_iterators[next_task] += 1
                    if task_event_iterators[next_task] >= len(self.task_timeline[next_task].runtime_events):
                        del task_event_iterators[next_task]

                # on schedule out check for spread, ignoring early ones before cookies are setup properly
                if next_cpu is None and running_count > 1 and check_spread:
                    cpu_groups = set()
                    max_time = None
                    for t in task_event_iterators.keys():
                        if task_cpu[t] is not None:
                            cpu_groups.add(self.cfg.cpu_to_group[task_cpu[t]])
                            if t != next_task and ((max_time is None) or (max_time < task_cpu_time[t])):
                                max_time = task_cpu_time[t]
                    min_number_of_groups = int(math.ceil(running_count / self.cfg.number_of_cpu_siblings))
                    if len(cpu_groups) > min_number_of_groups:
                        spread = next_time - max_time
                        assert(spread > 0)
                        task_accumulated_spread[next_task] += spread
                        print(f"Spread of {spread} of task {t} between {max_time} and {next_time}", file=spread_file)

                # update state
                if next_cpu is None:
                    running_count -= 1
                else:
                    running_count += 1
                task_cpu[next_task] = next_cpu
                task_cpu_time[next_task] = next_time

        if len(task_accumulated_spread) > 0:
            sorted_tas = sorted(task_accumulated_spread.items(), key=lambda x: -x[1])
            nano_to_seconds = 1000000000
            for t, s in sorted_tas:
                print(f"Task {t:6d} spread: {s/nano_to_seconds:.3f} s")
        else:
            print("No spread was found!")

        spread_file.close()


    def compute_bogops_count(self):
        file_pattern = 'fork_*.txt'
        bogops_pattern = r'^\s*Bogops count\s*=\s*(\d+)\s*$'
        matching_files = glob.glob(os.path.join(results_dir, file_pattern))
        total_bogops_count = 0
        for filename in matching_files:
            bogops_count = None
            with open(filename, 'r') as file:
                for line in file:
                    match = re.match(bogops_pattern, line)
                    if match:
                        bogops_count = int(match.group(1))
            if bogops_count is not None:
                total_bogops_count += bogops_count
            else:
                print(f"Could not find bogops count in {filename}, defaulting to zero")
        print(f"\nPerformance statistics")
        print(f"Total bogops count: {total_bogops_count}")
        avg_bogops_count = total_bogops_count / len(matching_files)
        print(f"Average bogops count: {avg_bogops_count:.0f}")
        total_cpu_time = 0
        total_runq_wait_time = 0
        total_forceidle_time = 0
        for t in cfg.tasks:
            total_cpu_time += t.cpu_time
            total_runq_wait_time += t.runq_wait_time
            total_forceidle_time += t.forceidle_time
        print(f"Total cpu time: {total_cpu_time:.3f}")
        avg_cpu_time = total_cpu_time / len(cfg.tasks)
        print(f"Average cpu time: {avg_cpu_time:.3f}")
        print(f"Total runq wait time: {total_runq_wait_time:.3f}")
        avg_runq_wait_time = total_runq_wait_time / len(cfg.tasks)
        print(f"Average runq wait time: {avg_runq_wait_time:.3f}")
        print(f"Total forceidle time: {total_forceidle_time:.3f}")
        avg_forceidle_time = total_forceidle_time / len(cfg.tasks)
        print(f"Average forceidle time: {avg_forceidle_time:.3f}")
        bogops_per_cpu_time = total_bogops_count / total_cpu_time
        print(f"Bogops per cpu time: {bogops_per_cpu_time:.0f}")


cfg = None
timeline = None
runtimes = {}
results_dir = os.environ.get('results_dir', '')


def trace_begin():
    global cfg
    global timeline
    cfg = parse_schtest_out()
    timeline = Timeline(cfg)


def trace_end():
    timeline.check_overlaps()
    timeline.check_spread()
    timeline.compute_bogops_count()


def sched__sched_stat_runtime(event_name, context, common_cpu,
                              common_secs, common_nsecs, common_pid, common_comm,
                              common_callchain, comm, pid, runtime, vruntime,
                              perf_sample_dict):

    event = Event(event_name = event_name, cpu = common_cpu, pid = common_pid, time = common_secs * 1000000000 + common_nsecs, comm = common_comm)
    timeline.add_event(event)
    timeline.add_runtime_event(event, runtime)


def sched__sched_stat_iowait(event_name, context, common_cpu,
                             common_secs, common_nsecs, common_pid, common_comm,
                             common_callchain, comm, pid, delay, perf_sample_dict):

    event = Event(event_name = event_name, cpu = common_cpu, pid = common_pid, time = common_secs * 1000000000 + common_nsecs, comm = common_comm)
    timeline.add_event(event)


def sched__sched_stat_sleep(event_name, context, common_cpu,
                            common_secs, common_nsecs, common_pid, common_comm,
                            common_callchain, comm, pid, delay, perf_sample_dict):
    event = Event(event_name = event_name, cpu = common_cpu, pid = common_pid, time = common_secs * 1000000000 + common_nsecs, comm = common_comm)
    timeline.add_event(event)


def sched__sched_stat_wait(event_name, context, common_cpu,
                           common_secs, common_nsecs, common_pid, common_comm,
                           common_callchain, comm, pid, delay, perf_sample_dict):

    event = Event(event_name = event_name, cpu = common_cpu, pid = common_pid, time = common_secs * 1000000000 + common_nsecs, comm = common_comm)
    timeline.add_event(event)


def sched__sched_process_fork(event_name, context, common_cpu,
	common_secs, common_nsecs, common_pid, common_comm,
	common_callchain, parent_comm, parent_pid, child_comm, child_pid,
		perf_sample_dict):

    event = Event(event_name = event_name, cpu = common_cpu, pid = common_pid, time = common_secs * 1000000000 + common_nsecs, comm = common_comm)
    timeline.add_event(event)


def sched__sched_migrate_task(event_name, context, common_cpu,
	common_secs, common_nsecs, common_pid, common_comm,
	common_callchain, comm, pid, prio, orig_cpu,
	dest_cpu, perf_sample_dict):

    event = Event(event_name = event_name, cpu = common_cpu, pid = common_pid, time = common_secs * 1000000000 + common_nsecs, comm = common_comm)
    timeline.add_event(event)


def sched__sched_switch(event_name, context, common_cpu,
	common_secs, common_nsecs, common_pid, common_comm,
	common_callchain, prev_comm, prev_pid, prev_prio, prev_state,
	next_comm, next_pid, next_prio, perf_sample_dict):

    event = Event(event_name = event_name, cpu = common_cpu, pid = common_pid, time = common_secs * 1000000000 + common_nsecs, comm = common_comm)
    timeline.add_event(event)

def sched__sched_wakeup_new(event_name, context, common_cpu,
    common_secs, common_nsecs, common_pid, common_comm,
    common_callchain, comm, pid, prio, target_cpu,
        perf_sample_dict):

    event = Event(event_name = event_name, cpu = common_cpu, pid = common_pid, time = common_secs * 1000000000 + common_nsecs, comm = common_comm)
    timeline.add_event(event)


def sched__sched_waking(event_name, context, common_cpu,
    common_secs, common_nsecs, common_pid, common_comm,
    common_callchain, comm, pid, prio, target_cpu,
    perf_sample_dict):

    event = Event(event_name = event_name, cpu = common_cpu, pid = common_pid, time = common_secs * 1000000000 + common_nsecs, comm = common_comm)
    timeline.add_event(event)

def trace_unhandled(event_name, context, event_fields_dict, perf_sample_dict):
    raise Exception(f'Unhandled event: {event_name}')
