import importlib.util
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location('windows_runner', Path(__file__).parents[1] / 'run.py')
runner = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(runner)


class GpuSafeModeTest(unittest.TestCase):
    def test_restart_switch_does_not_mutate_inherited_environment(self):
        inherited = {'BB_DRAW_PIPE': '1', 'BB_GAME_DIR': 'my-game', 'PATH': 'my-path'}
        with tempfile.TemporaryDirectory() as folder:
            config = Path(folder) / 'bbport.ini'
            config.write_text('gpu_safe_mode=1\n')
            safe = runner.native_environment(inherited, config)
            self.assertEqual(safe['BB_DRAW_PIPE'], '0')
            self.assertEqual(safe['BB_PREP_WORKERS'], '0')
            self.assertEqual(safe['BB_HONEST_LABELS'], '1')
            self.assertEqual(safe['BB_GAME_DIR'], 'my-game')
            self.assertNotIn('BB_PREP_WORKERS', inherited)
            self.assertEqual(inherited['BB_DRAW_PIPE'], '1')
            config.write_text('gpu_safe_mode=0\n')
            self.assertEqual(runner.native_environment(inherited, config), inherited)

    def test_missing_or_commented_setting_keeps_existing_options(self):
        inherited = {'BB_DRAW_PIPE': '1', 'BB_PREP_WORKERS': '3'}
        with tempfile.TemporaryDirectory() as folder:
            config = Path(folder) / 'bbport.ini'
            self.assertEqual(runner.native_environment(inherited, config), inherited)
            config.write_text('# gpu_safe_mode=1\nshow_fps=1\n')
            self.assertEqual(runner.native_environment(inherited, config), inherited)


if __name__ == '__main__':
    unittest.main()
