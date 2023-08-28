# schtest

## initial goal

Test benchmark to assess the correctness of the core affinity scheduler patches to support upstreaming process.

### overlaps

The correctness of the core scheduler is defined by its ability to eliminate overlaps.
An overlap is when a process with a cookie is scheduled on the CPU sibling of a process with a different or no cookie.

## future goals

Test benchmark to assess the correctness and performance of the scheduler.


## tools

### run.sh

Entry point. run.sh is an end-to-end overlap instrumentation script.

run.sh launches a set of processes using schtest and monitors scheduler events with perf sched.
Schtest will launches processes, optionally affine them to a set of cpus and assign them cookies.
Prior to calling schtest, run.sh will create the necessary cgroups.

run.sh then executes a perf script which computes for each CPU a timeline of running cookies and checks for overlaps between siblings.
The summary of found overlaps is printed while a fully detailed report is dumped in a result file.

All script options are passed through to `./bin/schtest`; refer to that script for available options.
The options -d (result directory) and -g (cgroup) are injected by run.sh and must not be supplied.

Example usage:
`./bin/run.sh -t "bin/stress -d 10" -n 16 -c 8 -s "0,48"`

* mount cgroups
* launches schtest with the following config
 * 16 stressors processes running for 10 seconds
 * 8 cookies
 * 2 cpus (0 and 48)
* trace schtest with perf
* post process collected data and assert correctness of core scheduling (no overlaps)


### schtest

Tool to launches tasks, and optionally assign them a cpuset and various cookies.
Run it without arguments to learn how to use it.


### stress

Single threaded tool to stress cpu using a task rate arrival model.
Units of work are created at periodic wall clock time intervals.
