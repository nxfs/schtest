# schtest

## initial goal

Test benchmark to assess the correctness of the core affinity scheduler patches to support upstreaming process


## future goals

Test benchmark to assess the correctness and performance of the scheduler


## tools

### run.sh

End-to-end validation script. Example usage:

`./bin/run.sh -t "bin/stress -d 10" -n 16 -c 8 -s "1-4"`

* mount cgroups
* launches schtest with the following config
 * 16 stressors processes running for 10 seconds
 * 8 cookies
 * 4 cpus
* trace schtest with perf
* post process collected data and assert correctness of core scheduling (no overlaps)


### schtest

Tool to launches tasks, and optionally assign them a cpuset and various cookies


### stress

Single threaded tool to stress cpu using a task rate arrival model.
Units of work are created at periodic wall clock time intervals.
