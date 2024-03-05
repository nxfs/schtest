use std::{
    process::{Child, Command},
    time::Duration,
};
use wait_timeout::ChildExt;

use crate::{cgroup, prctl, proc::wait_for_threads};

pub struct RunCommandCfg {
    pub task: String,
    pub threads: usize,
    pub thread_wait: Duration,
    pub timeout: Duration,
    pub cgroup: String,
    pub cpuset: String,
    pub weight: u64,
    pub cookie_count: u64,
}

pub fn run_command(cfg: RunCommandCfg) {
    let maybe_cgroup = cgroup::create_cgroup(&cfg.cgroup);
    let mut handles: Vec<Child> = vec![];
    println!("spawning task \"{}\"", cfg.task);
    let tokens: Vec<&str> = cfg.task.split(" ").collect();
    let mut cmd = Command::new(tokens[0]);
    for t in tokens.iter().skip(1) {
        cmd.arg(t);
    }
    let handle = cmd.spawn().unwrap();
    println!("task \"{}\" has pid {}", cfg.task, handle.id());
    let thread_ids = wait_for_threads(handle.id() as i32, cfg.threads, cfg.thread_wait);
    if let Some(ref cgroup) = maybe_cgroup {
        cgroup::add_task_to_cgroup(&cgroup, handle.id() as u64);
        for thread_id in thread_ids.iter() {
            cgroup::add_task_to_cgroup(&cgroup, *thread_id as u64);
        }
        cgroup::set_weight(&cgroup, cfg.weight);
        if !cfg.cpuset.is_empty() {
            cgroup::set_cpu_affinity(&cgroup, &cfg.cpuset);
        }
    }
    create_cookies(cfg.cookie_count, thread_ids);
    handles.push(handle);
    println!("waiting for all threads to join");
    while let Some(mut handle) = handles.pop() {
        let id = handle.id();
        if !cfg.timeout.is_zero() {
            if handle.wait_timeout(cfg.timeout).unwrap().is_none() {
                println!("timed out waiting for all threads to join, sending kill signal");
                handle.kill().unwrap();
            }
        }
        let out = handle.wait_with_output().unwrap();
        println!(
            "task {}: out='{}', err='{}'",
            id,
            String::from_utf8(out.stdout).unwrap(),
            String::from_utf8(out.stderr).unwrap()
        );
    }
}

pub fn create_cookies(cookie_count: u64, thread_ids: Vec<i32>) {
    if cookie_count == 0 {
        return;
    }

    let mut pid_grps: Vec<Vec<i32>> = Vec::with_capacity(cookie_count as usize);

    for (idx, pid) in thread_ids.iter().enumerate() {
        if idx < cookie_count as usize {
            pid_grps.push(vec![]);
        }
        pid_grps[idx % cookie_count as usize].push(*pid);
    }

    prctl::create_cookies(pid_grps);

    for thread_id in thread_ids {
        println!(
            "cookie for {} is {}",
            thread_id,
            prctl::get_cookie(thread_id)
        );
    }
}