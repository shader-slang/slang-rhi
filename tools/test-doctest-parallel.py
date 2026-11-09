#!/usr/bin/env python3
"""Check the reusable header using its standalone doctest-only executable."""

import argparse
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

BINARY = None


class DoctestParallelTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="doctest parallel ")
        self.addCleanup(self.directory.cleanup)

    def run_fixture(self, *arguments, success=True):
        # The fixture deliberately enables a custom prefix and disables both
        # unprefixed options and short test macros.
        replacements = {
            "-jobs=": "-fixture-jobs=", "-test-timeout=": "-fixture-test-timeout=",
            "-tc=": "--fixture-test-case=", "--no-skip": "--fixture-no-skip",
            "--list-test-cases": "--fixture-list-test-cases", "--no-run": "--fixture-no-run",
            "--first=": "--fixture-first=", "--last=": "--fixture-last=",
        }
        arguments = list(arguments)
        for index, argument in enumerate(arguments):
            for prefix, replacement in replacements.items():
                if argument.startswith(prefix):
                    arguments[index] = replacement + argument[len(prefix):]
                    break
        result = subprocess.run(
            [str(BINARY), *arguments], cwd=self.directory.name, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=15,
        )
        self.assertEqual(result.returncode == 0, success, result.stdout)
        return result.stdout

    def results(self, output):
        return re.findall(r"^fixture result (\S+) 1$", output, re.MULTILINE)

    def test_multitu_and_subcases(self):
        output = self.run_fixture("-jobs=2", "-tc=fixture-pass-*,fixture-subcases")
        self.assertCountEqual(self.results(output), ["fixture-pass-a", "fixture-pass-b", "fixture-subcases"])
        self.assertIn("0 failures", output)

    def test_persistent_worker_and_binary_metadata(self):
        output = self.run_fixture("-jobs=1", "-tc=fixture-pass-*,fixture-subcases")
        self.assertEqual(output.count("fixture worker start"), 1)
        self.assertEqual(output.count("fixture worker stop"), 1)
        self.assertEqual(len(self.results(output)), 3)
        self.assertIn("0 failures", output)  # The callback checks binary metadata equality.

    def test_assertion_failure_does_not_stop_queue(self):
        output = self.run_fixture("-jobs=1", "--no-skip", "-tc=fixture-failure,fixture-pass-a", success=False)
        self.assertCountEqual(self.results(output), ["fixture-failure", "fixture-pass-a"])
        self.assertIn("1 failures", output)

    def test_crash_replaces_worker(self):
        output = self.run_fixture("-jobs=1", "--no-skip", "-tc=fixture-crash,fixture-pass-a", success=False)
        self.assertIn("fixture result fixture-pass-a 1", output)
        self.assertEqual(output.count("fixture worker start"), 2)
        self.assertIn("1 failures", output)

    def test_timeout_replaces_worker(self):
        output = self.run_fixture("-jobs=1", "-test-timeout=1", "--no-skip",
                                  "-tc=fixture-hang,fixture-pass-a", success=False)
        self.assertIn("fixture result fixture-pass-a 1", output)
        self.assertEqual(output.count("fixture worker start"), 2)
        self.assertIn("1 failures", output)

    def test_worker_hooks_fail_run(self):
        for argument in ["--fixture-start-fails", "--fixture-stop-fails"]:
            self.run_fixture("-jobs=1", "-tc=fixture-pass-a", argument, success=False)

    def test_custom_runtime_skip(self):
        output = self.run_fixture("-jobs=1", "-tc=fixture-runtime-skip,fixture-subcases")
        self.assertIn("1 skipped", output)
        self.assertEqual(len(self.results(output)), 2)

    def test_ranges_and_configured_filters(self):
        selection = "-tc=fixture-pass-*,fixture-subcases"
        output = self.run_fixture("-jobs=2", selection, "--first=2", "--last=2")
        self.assertEqual(self.results(output), ["fixture-pass-b"])
        output = self.run_fixture("-jobs=2", selection, "--fixture-filter")
        self.assertCountEqual(self.results(output), ["fixture-pass-a", "fixture-subcases"])

    def test_option_spelling_and_validation(self):
        for jobs in ["-fixture-jobs=1", "--fixture-jobs=1"]:
            output = self.run_fixture(jobs, "--fixture-test-timeout=10", "-tc=fixture-pass-a")
            self.assertIn("1 assigned", output)
        # Doctest gives the last occurrence precedence, including over an invalid value.
        output = self.run_fixture("-jobs=0", "-jobs=1", "-tc=fixture-pass-a")
        self.assertIn("1 assigned", output)
        for option in ["-fixture-jobs", "-jobs=", "-jobs=0", "-jobs=-1", "-jobs=abc",
                       "-jobs=2147483648", "-fixture-test-timeout", "-test-timeout=0"]:
            self.run_fixture(option, "-tc=fixture-pass-a", success=False)
        output = self.run_fixture("--jobs=2", "-tc=fixture-pass-a")
        self.assertNotIn("fixture worker start", output)  # Unprefixed options are disabled.

    def test_output_file_preserves_failure_diagnostics(self):
        report = Path(self.directory.name) / "results.txt"
        output = self.run_fixture("-jobs=2", "--no-skip", "--fixture-success",
                                  "-tc=fixture-failure,fixture-pass-a", f"--fixture-out={report}",
                                  success=False)
        self.assertNotIn("fixture worker start", output)
        contents = report.read_text()
        self.assertIn("fixture-failure", contents)
        self.assertIn("fixture-pass-a", contents)
        self.assertIn("Status: FAILURE", contents)

    def test_queries_and_sequential_fallback(self):
        output = self.run_fixture("-jobs=2", "--list-test-cases", "-tc=fixture-pass-*")
        self.assertIn("fixture-pass-a", output)
        self.assertNotIn("fixture worker start", output)
        output = self.run_fixture("-jobs=2", "--no-run", "-tc=fixture-pass-*")
        self.assertNotIn("fixture worker start", output)
        output = self.run_fixture("-tc=fixture-pass-*")
        self.assertNotIn("fixture worker start", output)
        self.assertIn("SUCCESS", output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    args, remaining = parser.parse_known_args()
    BINARY = args.binary.resolve(strict=True)
    unittest.main(argv=[__file__, *remaining])
