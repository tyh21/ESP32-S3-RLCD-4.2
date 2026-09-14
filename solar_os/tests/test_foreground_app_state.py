import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest


REPOSITORY = Path(__file__).resolve().parents[1]
CHECKER_PATH = REPOSITORY / "scripts" / "check_foreground_app_state.py"
SPEC = importlib.util.spec_from_file_location("check_foreground_app_state", CHECKER_PATH)
assert SPEC is not None and SPEC.loader is not None
check_foreground_app_state = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = check_foreground_app_state
SPEC.loader.exec_module(check_foreground_app_state)


class ForegroundAppStatePolicyTest(unittest.TestCase):
    def test_foreground_apps_have_no_unreviewed_eager_mutable_state(self):
        self.assertEqual(check_foreground_app_state.check(REPOSITORY), [])

    def test_checker_rejects_eager_state_and_accepts_cold_pointer(self):
        fixture = [
            "static unsigned eager[16];",
            "static void *cold_state;",
        ]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "fixture.c"
            path.write_text("\n".join(fixture), encoding="utf-8")
            self.assertEqual(
                check_foreground_app_state.violations(path),
                [(1, "static unsigned eager[16];")],
            )

    def test_foreground_launches_use_the_shared_lifecycle(self):
        self.assertEqual(
            check_foreground_app_state.lifecycle_bypasses(REPOSITORY), []
        )


if __name__ == "__main__":
    unittest.main()
