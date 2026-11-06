import importlib.util
import json
import os
import unittest
from datetime import datetime, timezone
from pathlib import Path
from unittest.mock import Mock, patch
from urllib.parse import parse_qs, urlparse

spec = importlib.util.spec_from_file_location(
    'email_monitor', Path(__file__).resolve().parents[1] / 'scripts/health_email_monitor.py')
monitor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(monitor)


class FixedDateTime(datetime):
    @classmethod
    def now(cls, tz=None):
        if tz is not None:
            return cls(2026, 11, 6, 13, tzinfo=timezone.utc).astimezone(tz)
        return cls(2026, 11, 6, 8)  # monitor host in US Eastern time


class EmailMonitorTests(unittest.TestCase):
    def test_project_and_utc_query(self):
        response = Mock()
        response.__enter__ = Mock(return_value=response)
        response.__exit__ = Mock(return_value=False)
        response.read.return_value = b'[]'
        with patch.dict(os.environ, {'ANACOSTIAIQ_PROJECT': 'John-McCormack',
                                   'ANACOSTIAIQ_ALERT_TIMESTAMPS_UTC': 'true'}, clear=True), \
                patch.object(monitor, 'datetime', FixedDateTime), \
                patch.object(monitor, 'urlopen', return_value=response) as fetch:
            self.assertIsNone(monitor.latest_reading('http://api', 'health_station_application', 24))
        query = parse_qs(urlparse(fetch.call_args.args[0]).query)
        self.assertEqual(query['project'], ['John-McCormack'])
        self.assertEqual(query['end'], ['2026-11-06T13:00:00'])

    def test_naive_utc_heartbeat_is_stale_on_non_utc_monitor_host(self):
        with patch.dict(os.environ, {'ANACOSTIAIQ_ALERT_TIMESTAMPS_UTC': 'true'}, clear=True), \
                patch.object(monitor, 'datetime', FixedDateTime):
            state = monitor.reading_state(dict(value=0, timestamp='2026-11-06T12:45:00'),
                                          'application', 600)
        self.assertEqual(state[0], 'offline')

    def run_cycles(self, api_errors, reading):
        env = {'ANACOSTIAIQ_EMAIL_ALERTS_ENABLED': 'true',
               'ANACOSTIAIQ_ALERT_STATION_ID': 'test_station',
               'ANACOSTIAIQ_ALERT_COMPONENTS': 'sensor_staging_moisture_sensor'}
        sleeps = [None] * (len(api_errors) - 1) + [StopIteration]
        with patch.dict(os.environ, env, clear=True), \
                patch.object(monitor, 'load_json', return_value={'components': {}}), \
                patch.object(monitor, 'save_json'), \
                patch.object(monitor, 'monitoring_api_error', side_effect=api_errors), \
                patch.object(monitor, 'latest_reading', return_value=reading), \
                patch.object(monitor.time, 'sleep', side_effect=sleeps), \
                patch.object(monitor, 'send_email') as send:
            with self.assertRaises(StopIteration):
                monitor.main()
            return send.call_args_list

    def test_api_outage_is_one_alarm_after_three_failures_then_one_recovery(self):
        sent = self.run_cycles([None, 'unreachable', 'unreachable', 'unreachable', None],
                               dict(value=0, unit='wet_limit_reached', timestamp='2026-11-06T13:00:00'))
        self.assertEqual(len(sent), 2)
        self.assertIn('ALARM', sent[0].args[0])
        self.assertIn('Monitoring API', sent[0].args[0])
        self.assertIn('RECOVERY', sent[1].args[0])

    def test_valid_wet_limit_never_sends_an_alarm(self):
        sent = self.run_cycles([None, None, None],
                               dict(value=0, unit='wet_limit_reached', timestamp='2026-11-06T13:00:00'))
        self.assertEqual(sent, [])

    def test_alarm_transition_is_not_repeated_without_reminders(self):
        healthy = dict(value=0, unit='ok', timestamp='2026-11-06T13:00:00')
        offline = dict(value=2, unit='sensor_unavailable', timestamp='2026-11-06T13:01:00')
        # Keep API reachable; vary only one sensor's state over four polls.
        with patch.object(monitor, 'latest_reading', side_effect=[healthy, offline, offline, healthy]) as reading:
            # run_cycles supplies its own stub; use its helper with a stateful
            # dict whose health value is read through the mocked reader below.
            env = {'ANACOSTIAIQ_EMAIL_ALERTS_ENABLED': 'true',
                   'ANACOSTIAIQ_ALERT_COMPONENTS': 'sensor_staging_moisture_sensor'}
            with patch.dict(os.environ, env, clear=True), \
                    patch.object(monitor, 'load_json', return_value={'components': {}}), \
                    patch.object(monitor, 'save_json'), \
                    patch.object(monitor, 'monitoring_api_error', return_value=None), \
                    patch.object(monitor.time, 'sleep', side_effect=[None, None, None, StopIteration]), \
                    patch.object(monitor, 'send_email') as send:
                with self.assertRaises(StopIteration):
                    monitor.main()
            self.assertEqual(send.call_count, 2)
            self.assertIn('ALARM', send.call_args_list[0].args[0])
            self.assertIn('RECOVERY', send.call_args_list[1].args[0])


if __name__ == '__main__':
    unittest.main()

class EmailTimezoneTests(unittest.TestCase):
    def test_eastern_standard_and_daylight_times(self):
        with patch.dict(os.environ, {"ANACOSTIAIQ_ALERT_TIMESTAMPS_UTC": "true", "ANACOSTIAIQ_ALERT_TIMEZONE": "America/New_York"}):
            self.assertEqual(monitor.email_time("2026-11-06T13:00:00"), "2026-11-06 08:00:00 AM EST (UTC-0500)")
            self.assertEqual(monitor.email_time("2026-07-06T13:00:00+00:00"), "2026-07-06 09:00:00 AM EDT (UTC-0400)")
