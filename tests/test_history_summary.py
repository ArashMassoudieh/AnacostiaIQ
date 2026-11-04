import sys
import unittest
from decimal import Decimal
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'server'))
from history_summary import summarize_history


class HistorySummaryTests(unittest.TestCase):
    def row(self, timestamp, value, unit='in'):
        return dict(sensor_id='John-McCormack#depth', timestamp=timestamp,
                    value=Decimal(value), unit=unit)

    def test_weighted_mean_extrema_and_minute_boundaries(self):
        rows = [self.row('2026-11-04T10:00:01', '1'),
                self.row('2026-11-04T10:00:20', '3'),
                self.row('2026-11-04T10:00:59', '8'),
                self.row('2026-11-04T10:01:00', '9')]
        result = summarize_history(iter(rows), 60)
        self.assertEqual(len(result), 2)
        self.assertEqual(result[0]['value'], Decimal(4))
        self.assertEqual(result[0]['sample_count'], 3)
        self.assertEqual((result[0]['minimum'], result[0]['maximum']), (1, 8))
        self.assertEqual(result[1]['timestamp'], '2026-11-04T10:01:00')
        self.assertNotIn('sample_count', rows[0])  # input/history unchanged

    def test_missing_buckets_are_gaps_not_fabricated_zeroes(self):
        rows = [self.row('2026-11-04T10:00:10', '2'),
                self.row('2026-11-04T10:05:10', '4')]
        self.assertEqual(len(summarize_history(rows, 60)), 2)

    def test_invalid_and_nonfinite_values_do_not_bias_average(self):
        rows = [self.row('2026-11-04T10:00:10', '2'),
                self.row('2026-11-04T10:00:20', 'NaN'),
                dict(timestamp='invalid', value='10')]
        result = summarize_history(rows, 60)
        self.assertEqual(result[0]['value'], 2)
        self.assertEqual(result[0]['sample_count'], 1)

    def test_units_do_not_mix_and_explicit_offsets_normalize_to_utc(self):
        rows = [self.row('2026-11-04T10:00:10-05:00', '2'),
                self.row('2026-11-04T15:00:20Z', '4'),
                self.row('2026-11-04T15:00:30Z', '100', 'mm')]
        result = summarize_history(rows, 60)
        self.assertEqual(len(result), 2)
        inches = next(x for x in result if x['unit'] == 'in')
        self.assertEqual(inches['value'], 3)
        self.assertEqual(inches['timestamp'], '2026-11-04T15:00:00+00:00')


if __name__ == '__main__':
    unittest.main()
