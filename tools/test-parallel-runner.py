#!/usr/bin/env python3
"""Integration checks for slang-rhi-tests process scheduling (no GPU required).

Usage: python tools/test-parallel-runner.py build/Debug/slang-rhi-tests.exe
"""

import argparse
import ctypes
from ctypes import wintypes
import os
from pathlib import Path
import re
import signal
import subprocess
import tempfile
import time
import unittest

BINARY = None


def child_processes(parent):
    if os.name != "nt":
        output = subprocess.check_output(["ps", "-eo", "pid=,ppid="], text=True)
        return [int(pid) for pid, ppid in (line.split() for line in output.splitlines()) if int(ppid) == parent]

    class Entry(ctypes.Structure):
        _fields_ = [
            ("size", wintypes.DWORD), ("usage", wintypes.DWORD), ("pid", wintypes.DWORD),
            ("heap", ctypes.c_size_t), ("module", wintypes.DWORD), ("threads", wintypes.DWORD),
            ("parent", wintypes.DWORD), ("priority", wintypes.LONG), ("flags", wintypes.DWORD),
            ("exe", wintypes.WCHAR * 260),
        ]

    api = ctypes.WinDLL("kernel32", use_last_error=True)
    api.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
    api.CreateToolhelp32Snapshot.argtypes = [wintypes.DWORD, wintypes.DWORD]
    api.Process32FirstW.argtypes = [wintypes.HANDLE, ctypes.POINTER(Entry)]
    api.Process32NextW.argtypes = [wintypes.HANDLE, ctypes.POINTER(Entry)]
    api.CloseHandle.argtypes = [wintypes.HANDLE]
    snapshot = api.CreateToolhelp32Snapshot(2, 0)
    result = []
    try:
        entry = Entry()
        entry.size = ctypes.sizeof(entry)
        found = api.Process32FirstW(snapshot, ctypes.byref(entry))
        while found:
            if entry.parent == parent:
                result.append(entry.pid)
            found = api.Process32NextW(snapshot, ctypes.byref(entry))
    finally:
        api.CloseHandle(snapshot)
    return result


class ParallelRunnerTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="parallel runner ")
        self.addCleanup(self.directory.cleanup)
        self.cwd = Path(self.directory.name)

    def run_tests(self, *arguments, success=True):
        result = subprocess.run(
            [str(BINARY), *arguments], cwd=self.cwd, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30,
        )
        self.assertEqual(result.returncode == 0, success, result.stdout)
        return result.stdout

    def test_same_tests_exactly_once(self):
        selection = "-tc=arena-allocator,blob,short_vector,static_vector,task-pool-*,timer"
        sequential = self.run_tests(selection)
        parallel = self.run_tests("--jobs=4", "--test-timeout=10", selection)
        pattern = r"^([\w.-]+)\s+PASSED"
        expected = re.findall(pattern, sequential, re.MULTILINE)
        actual = re.findall(pattern, parallel, re.MULTILINE)
        self.assertGreater(len(expected), 4)
        self.assertCountEqual(actual, expected)
        self.assertEqual(len(actual), len(set(actual)))

    def test_range_and_queries(self):
        selection = "-tc=blob,short_vector,static_vector,timer"
        output = self.run_tests("--jobs=2", selection, "--first=2", "--last=3")
        self.assertCountEqual(re.findall(r"^([\w.-]+)\s+PASSED", output, re.MULTILINE),
                              ["short_vector", "static_vector"])
        self.assertEqual(self.run_tests(selection, "--list-test-cases"),
                         self.run_tests("--jobs=2", selection, "--list-test-cases"))
        self.assertIn("No tests matched", self.run_tests("--jobs=2", "-tc=does-not-exist"))

    def test_invalid_options(self):
        for option in ["--jobs=0", "--jobs=-1", "--jobs=abc", "--test-timeout=0"]:
            self.run_tests("--jobs=2", option, "-tc=blob", success=False)

    def test_runtime_skip(self):
        output = self.run_tests("--jobs=2", "-tc=compute-trivial.vulkan", "-select-devices=d3d12")
        self.assertIn("SKIPPED (device not selected)", output)
        self.assertIn("1 skipped", output)

    def test_required_device_failure(self):
        unavailable = "metal" if os.name == "nt" else "d3d12"
        output = self.run_tests("--jobs=2", "-tc=blob,timer", f"-require-devices={unavailable}", success=False)
        self.assertIn("Required device", output)

    def test_memory_reports_are_per_worker(self):
        report = self.cwd / "memory report.json"
        self.run_tests("--jobs=2", "-tc=blob,timer", f"-memory-report-file={report}")
        reports = list(self.cwd.glob("memory report.json.worker-*.json"))
        self.assertTrue(reports)
        self.assertFalse(report.exists())
        for worker_report in reports:
            text = worker_report.read_text()
            self.assertIn("worker-start", text)
            self.assertIn("after-destroy-rhi", text)

    def test_persistent_worker(self):
        log = self.cwd / "worker output.log"
        process = subprocess.run(
            [str(BINARY), f"--parallel-worker={log}", "-tc=blob,timer"], cwd=self.cwd,
            input="1\n2\n1\n", stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=10,
        )
        self.assertEqual(process.returncode, 0, process.stderr)
        responses = process.stdout.splitlines()
        self.assertEqual(responses[0], "ready")
        self.assertEqual(len(responses), 4)
        for response in responses[1:]:
            self.assertEqual(response.split()[:4], ["done", "0", "1", "0"])
        self.assertEqual(log.read_text().count("PASSED"), 3)

    def test_worker_crash_continues_queue(self):
        self.check_worker_interruption(suspend=False)

    def test_worker_timeout_continues_queue(self):
        self.check_worker_interruption(suspend=True)

    def check_worker_interruption(self, suspend):
        process = subprocess.Popen(
            [str(BINARY), "--jobs=1", "--test-timeout=1", "-tc=task-pool-work-stealing,timer"],
            cwd=self.cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
        )
        try:
            deadline = time.monotonic() + 10
            killed = False
            while process.poll() is None and time.monotonic() < deadline:
                for log in self.cwd.glob(".test_temp/*/worker-0.log"):
                    contents = log.read_text(errors="replace")
                    if "task-pool-work-stealing" in contents and "PASSED" not in contents:
                        children = child_processes(process.pid)
                        if children:
                            if not suspend:
                                os.kill(children[0], signal.SIGTERM)
                            elif os.name != "nt":
                                os.kill(children[0], signal.SIGSTOP)
                            else:
                                # Freeze the worker so its assignment deadline expires.
                                kernel = ctypes.WinDLL("kernel32", use_last_error=True)
                                kernel.OpenProcess.restype = wintypes.HANDLE
                                kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
                                kernel.CloseHandle.argtypes = [wintypes.HANDLE]
                                native = ctypes.WinDLL("ntdll")
                                native.NtSuspendProcess.argtypes = [wintypes.HANDLE]
                                handle = kernel.OpenProcess(0x0800, False, children[0])
                                self.assertTrue(handle)
                                try:
                                    self.assertEqual(native.NtSuspendProcess(handle), 0)
                                finally:
                                    kernel.CloseHandle(handle)
                            killed = True
                            break
                if killed:
                    break
                time.sleep(0.002)
            output = process.communicate(timeout=15)[0]
            self.assertTrue(killed, output)
            self.assertNotEqual(process.returncode, 0, output)
            self.assertRegex(output, r"timer\s+PASSED")
            self.assertIn("2 assigned", output)
        finally:
            if process.poll() is None:
                for child in child_processes(process.pid):
                    os.kill(child, signal.SIGTERM)
                process.kill()
                process.wait()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    args, remaining = parser.parse_known_args()
    BINARY = args.binary.resolve(strict=True)
    unittest.main(argv=[__file__, *remaining])
