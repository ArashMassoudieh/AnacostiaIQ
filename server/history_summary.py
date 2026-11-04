"""Optional read-only time buckets for numeric sensor history."""

from datetime import datetime, timedelta, timezone
from decimal import Decimal, InvalidOperation


def summarize_history(items, interval_seconds):
    """Consume a paginated stream; memory grows with buckets, not raw rows.

    Bucket timestamps use the stored wall-clock convention. Naive timestamps
    are not silently interpreted in the API host's local timezone.
    """
    buckets = {}
    epoch = datetime(1970, 1, 1)
    for item in items:
        try:
            stamp = datetime.fromisoformat(item['timestamp'].replace('Z', '+00:00'))
            aware = stamp.tzinfo is not None
            if aware:
                stamp = stamp.astimezone(timezone.utc).replace(tzinfo=None)
            value = Decimal(str(item['value']))
            if not value.is_finite():
                continue
        except (KeyError, ValueError, TypeError, InvalidOperation):
            continue
        seconds = int((stamp - epoch).total_seconds())
        start = epoch + timedelta(seconds=seconds // interval_seconds * interval_seconds)
        key = (start, item.get('unit', ''), aware)
        bucket = buckets.get(key)
        if bucket is None:
            bucket = dict(item)
            bucket.update(timestamp=start.isoformat() + ('+00:00' if aware else ''),
                          sample_count=0, minimum=value, maximum=value,
                          interval_seconds=interval_seconds, _sum=Decimal(0))
            buckets[key] = bucket
        bucket['_sum'] += value
        bucket['sample_count'] += 1
        bucket['minimum'] = min(bucket['minimum'], value)
        bucket['maximum'] = max(bucket['maximum'], value)
    result = []
    for _, bucket in sorted(buckets.items()):
        bucket['value'] = bucket.pop('_sum') / bucket['sample_count']
        result.append(bucket)
    return result
