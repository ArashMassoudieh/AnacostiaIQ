from flask import Flask, request, jsonify
from flask_cors import CORS
import boto3
from boto3.dynamodb.conditions import Key
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import shutil
import tempfile
import time
import uuid

app = Flask(__name__)
CORS(app)

dynamodb = boto3.resource('dynamodb', region_name='us-west-2')
table = dynamodb.Table('sensor_readings')

# Token required for destructive operations. Set WIPE_TOKEN in the
# server environment; requests must send it via the X-Wipe-Token header.
WIPE_TOKEN = os.environ.get('WIPE_TOKEN')

# Lab camera storage. Camera uploads and Pi polling require CAMERA_TOKEN.
# The public dashboard may read camera state and request a capture; requests
# are rate-limited server-side so a public page cannot hammer the camera.
CAMERA_TOKEN = os.environ.get('CAMERA_TOKEN')
CAMERA_STATE_ROOT = Path(os.environ.get(
    'CAMERA_STATE_ROOT', '/home/ubuntu/sensor-api/camera-state'))
CAMERA_IMAGE_ROOT = Path(os.environ.get(
    'CAMERA_IMAGE_ROOT', '/home/ubuntu/dashboard/lab/camera'))
CAMERA_PUBLIC_BASE = os.environ.get('CAMERA_PUBLIC_BASE', '/lab/camera').rstrip('/')
CAMERA_RETENTION_DAYS = int(os.environ.get('CAMERA_RETENTION_DAYS', '30'))
CAMERA_MAX_IMAGES = int(os.environ.get('CAMERA_MAX_IMAGES', '500'))
CAMERA_REQUEST_MIN_SECONDS = int(os.environ.get(
    'CAMERA_REQUEST_MIN_SECONDS', '30'))
CAMERA_ALLOWED_INTERVALS = (0, 1, 6, 12, 24)
CAMERA_STATION_RE = re.compile(r'^[A-Za-z0-9_-]{1,64}$')

# ---------------------------------------------------------------------------
# Project namespacing
#
# The table's partition key is sensor_id, so two projects posting the same
# sensor name at the same timestamp would overwrite each other. To isolate
# them without changing the key schema (DynamoDB cannot alter one in place),
# the stored partition key is "<project>#<sensor_id>" and the API splits it
# back out on read. Clients only ever see project and sensor_id separately.
#
# Readings written before this change carry a bare sensor_id with no "#".
# They are served when no project is given, so existing data and the
# dashboard keep working untouched.
# ---------------------------------------------------------------------------
PROJECT_SEP = '#'


def _storage_key(project, sensor_id):
    """Partition key actually stored. No project => legacy bare sensor_id."""
    if project:
        return '{}{}{}'.format(project, PROJECT_SEP, sensor_id)
    return sensor_id


def _split_key(stored):
    """(project, sensor_id) from a stored partition key. Legacy rows have
    no separator and report project None."""
    if PROJECT_SEP in stored:
        project, _, sensor_id = stored.partition(PROJECT_SEP)
        return project, sensor_id
    return None, stored


def _public_item(item):
    """Rewrite a stored item for the wire: split the composite key back into
    project + sensor_id so clients never see the "#" encoding."""
    project, sensor_id = _split_key(item.get('sensor_id', ''))
    out = dict(item)
    out['sensor_id'] = sensor_id
    if project is not None:
        out['project'] = project
    return out


def _key_condition(stored_key, start, end):
    """Shared by GET and DELETE so their range semantics cannot drift."""
    cond = Key('sensor_id').eq(stored_key)
    if start and end:
        cond = cond & Key('timestamp').between(start, end)
    elif start:
        cond = cond & Key('timestamp').gte(start)
    elif end:
        cond = cond & Key('timestamp').lte(end)
    return cond


def _query_all(key_condition, **kwargs):
    """Query every page. The original GET returned only the first page
    (~1 MB) and silently truncated past that."""
    items = []
    response = table.query(KeyConditionExpression=key_condition, **kwargs)
    items.extend(response['Items'])
    while 'LastEvaluatedKey' in response:
        response = table.query(KeyConditionExpression=key_condition,
                               ExclusiveStartKey=response['LastEvaluatedKey'],
                               **kwargs)
        items.extend(response['Items'])
    return items


def _scan_all(**kwargs):
    items = []
    response = table.scan(**kwargs)
    items.extend(response['Items'])
    while 'LastEvaluatedKey' in response:
        response = table.scan(ExclusiveStartKey=response['LastEvaluatedKey'],
                              **kwargs)
        items.extend(response['Items'])
    return items


def _authorized(req):
    """Destructive ops require a matching token. If no token is
    configured on the server, deletion is refused outright."""
    if not WIPE_TOKEN:
        return False
    return req.headers.get('X-Wipe-Token') == WIPE_TOKEN


def _camera_authorized(req):
    return bool(CAMERA_TOKEN) and \
        req.headers.get('X-Camera-Token') == CAMERA_TOKEN


def _camera_station(station_id):
    if not CAMERA_STATION_RE.fullmatch(station_id):
        return None
    return station_id


def _camera_state_path(station_id):
    return CAMERA_STATE_ROOT / ('{}.json'.format(station_id))


def _camera_default_state(station_id):
    return {
        'station_id': station_id,
        'schedule_hours': 6,
        'latest': None,
        'request': None,
    }


def _camera_read_state(station_id):
    path = _camera_state_path(station_id)
    try:
        with path.open('r', encoding='utf-8') as fh:
            state = json.load(fh)
    except (OSError, ValueError):
        state = _camera_default_state(station_id)
    state.setdefault('station_id', station_id)
    state.setdefault('schedule_hours', 6)
    state.setdefault('latest', None)
    state.setdefault('request', None)
    return state


def _camera_write_state(station_id, state):
    CAMERA_STATE_ROOT.mkdir(parents=True, exist_ok=True)
    path = _camera_state_path(station_id)
    fd, temporary = tempfile.mkstemp(prefix=path.name + '.',
                                     dir=str(CAMERA_STATE_ROOT))
    try:
        with os.fdopen(fd, 'w', encoding='utf-8') as fh:
            json.dump(state, fh, sort_keys=True)
            fh.write('\n')
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def _camera_public_state(state):
    request_state = state.get('request')
    public_request = None
    if request_state:
        public_request = {
            'id': request_state.get('id'),
            'status': request_state.get('status'),
            'requested_at': request_state.get('requested_at'),
            'completed_at': request_state.get('completed_at'),
        }
    return {
        'station_id': state.get('station_id'),
        'schedule_hours': state.get('schedule_hours', 6),
        'allowed_schedule_hours': list(CAMERA_ALLOWED_INTERVALS),
        'latest': state.get('latest'),
        'request': public_request,
    }


def _camera_cleanup_archive(archive):
    images = sorted(archive.glob('*.jpg'),
                    key=lambda p: p.stat().st_mtime,
                    reverse=True)
    cutoff = time.time() - CAMERA_RETENTION_DAYS * 86400
    for index, path in enumerate(images):
        if index >= CAMERA_MAX_IMAGES or path.stat().st_mtime < cutoff:
            try:
                path.unlink()
            except OSError:
                pass


@app.route('/sensor', methods=['POST'])
def receive_sensor_data():
    data = request.json

    # Use client-provided timestamp if present, otherwise server time
    timestamp = data.get('timestamp', datetime.utcnow().isoformat() + 'Z')

    project = (data.get('project') or '').strip()
    if PROJECT_SEP in project:
        return jsonify({'error':
                        'project must not contain "{}"'.format(PROJECT_SEP)}), 400

    item = {
        'sensor_id': _storage_key(project, data['sensor_id']),
        'timestamp': timestamp,
        'value': str(data['value']),
        'unit': data.get('unit', '')
    }
    # Kept as a plain attribute too, so the project is visible in the raw
    # table and in any consumer that reads DynamoDB directly.
    if project:
        item['project'] = project

    table.put_item(Item=item)
    return jsonify({'status': 'success'}), 200


@app.route('/sensor/<sensor_id>', methods=['GET'])
def get_sensor_data(sensor_id):
    start = request.args.get('start')
    end = request.args.get('end')
    project = request.args.get('project')

    items = _query_all(_key_condition(_storage_key(project, sensor_id),
                                      start, end))
    return jsonify([_public_item(i) for i in items]), 200


@app.route('/sensor/<sensor_id>', methods=['DELETE'])
def delete_sensor_data(sensor_id):
    """Delete readings for one sensor, optionally limited to a time
    range via ?start=...&end=... (same semantics as the GET endpoint)
    and to one project via ?project=... .
    Requires a valid X-Wipe-Token header."""
    if not _authorized(request):
        return jsonify({'error': 'unauthorized'}), 401

    start = request.args.get('start')
    end = request.args.get('end')
    project = request.args.get('project')
    stored_key = _storage_key(project, sensor_id)

    items = _query_all(_key_condition(stored_key, start, end),
                       ProjectionExpression='sensor_id, #ts',
                       ExpressionAttributeNames={'#ts': 'timestamp'})

    deleted = 0
    with table.batch_writer() as batch:
        for item in items:
            batch.delete_item(Key={
                'sensor_id': item['sensor_id'],
                'timestamp': item['timestamp']
            })
            deleted += 1

    result = {'status': 'success', 'sensor_id': sensor_id, 'deleted': deleted}
    if project:
        result['project'] = project
    return jsonify(result), 200


@app.route('/sensors', methods=['GET'])
def list_sensors():
    """Sensor ids. With ?project=X, only that project's sensors (and the
    bare sensor_id is returned, not the stored composite key). Without it,
    every sensor across every project, which is what the pre-project API
    did -- pass ?project= explicitly to scope it."""
    project = request.args.get('project')
    items = _scan_all(ProjectionExpression='sensor_id')

    ids = set()
    for item in items:
        item_project, sensor_id = _split_key(item['sensor_id'])
        if project is None or item_project == project:
            ids.add(sensor_id)
    return jsonify(sorted(ids)), 200


@app.route('/projects', methods=['GET'])
def list_projects():
    """Distinct project names. Legacy rows written before project support
    report as null, so they are visible rather than silently missing."""
    items = _scan_all(ProjectionExpression='sensor_id')
    projects = set()
    for item in items:
        item_project, _ = _split_key(item['sensor_id'])
        projects.add(item_project)

    named = sorted(p for p in projects if p is not None)
    if None in projects:
        named.append(None)        # legacy, unprefixed rows
    return jsonify(named), 200


@app.route('/camera/<station_id>/state', methods=['GET'])
def camera_state(station_id):
    """Public state used by the dashboard. It contains URLs and timestamps,
    never the upload token or local filesystem paths."""
    if not _camera_station(station_id):
        return jsonify({'error': 'invalid station id'}), 400
    return jsonify(_camera_public_state(_camera_read_state(station_id))), 200


@app.route('/camera/<station_id>/request', methods=['POST'])
def camera_request(station_id):
    """Queue one instant capture. Public dashboard requests are bounded to
    one request per station per CAMERA_REQUEST_MIN_SECONDS."""
    if not _camera_station(station_id):
        return jsonify({'error': 'invalid station id'}), 400

    state = _camera_read_state(station_id)
    current = state.get('request') or {}
    requested_epoch = float(current.get('requested_epoch') or 0)
    age = time.time() - requested_epoch
    if current.get('status') == 'pending' and age < CAMERA_REQUEST_MIN_SECONDS:
        return jsonify(_camera_public_state(state)), 202
    if age < CAMERA_REQUEST_MIN_SECONDS:
        return jsonify({
            'error': 'capture requests are rate limited',
            'retry_after_seconds': max(1, int(CAMERA_REQUEST_MIN_SECONDS - age)),
        }), 429

    now = datetime.now(timezone.utc).isoformat(timespec='seconds')
    state['request'] = {
        'id': uuid.uuid4().hex,
        'status': 'pending',
        'requested_at': now,
        'requested_epoch': time.time(),
    }
    _camera_write_state(station_id, state)
    return jsonify(_camera_public_state(state)), 202


@app.route('/camera/<station_id>/schedule', methods=['PUT', 'POST'])
def camera_schedule(station_id):
    if not _camera_station(station_id):
        return jsonify({'error': 'invalid station id'}), 400
    data = request.get_json(silent=True) or {}
    try:
        hours = int(data.get('hours'))
    except (TypeError, ValueError):
        return jsonify({'error': 'hours must be one of {}'.format(
            CAMERA_ALLOWED_INTERVALS)}), 400
    if hours not in CAMERA_ALLOWED_INTERVALS:
        return jsonify({'error': 'hours must be one of {}'.format(
            CAMERA_ALLOWED_INTERVALS)}), 400
    state = _camera_read_state(station_id)
    state['schedule_hours'] = hours
    state['schedule_updated_at'] = datetime.now(timezone.utc).isoformat(
        timespec='seconds')
    _camera_write_state(station_id, state)
    return jsonify(_camera_public_state(state)), 200


@app.route('/camera/<station_id>/pending', methods=['GET'])
def camera_pending(station_id):
    """Pi polling endpoint. It also carries the current schedule so the
    dashboard can change intervals without logging into the Pi."""
    if not _camera_station(station_id):
        return jsonify({'error': 'invalid station id'}), 400
    if not _camera_authorized(request):
        return jsonify({'error': 'unauthorized'}), 401
    state = _camera_read_state(station_id)
    pending = state.get('request')
    if not pending or pending.get('status') != 'pending':
        pending = None
    return jsonify({
        'station_id': station_id,
        'schedule_hours': state.get('schedule_hours', 6),
        'request': pending,
    }), 200


@app.route('/camera/<station_id>/upload', methods=['POST'])
def camera_upload(station_id):
    if not _camera_station(station_id):
        return jsonify({'error': 'invalid station id'}), 400
    if not _camera_authorized(request):
        return jsonify({'error': 'unauthorized'}), 401
    uploaded = request.files.get('image')
    if uploaded is None or not uploaded.filename:
        return jsonify({'error': 'multipart image file is required'}), 400
    if request.content_length and request.content_length > 12 * 1024 * 1024:
        return jsonify({'error': 'image too large'}), 413

    station_root = CAMERA_IMAGE_ROOT / station_id
    archive = station_root / 'archive'
    archive.mkdir(parents=True, exist_ok=True)
    now = datetime.now(timezone.utc)
    capture_id = (request.form.get('capture_id') or uuid.uuid4().hex)
    capture_id = re.sub(r'[^A-Za-z0-9_-]', '', capture_id)[:64]
    if not capture_id:
        capture_id = uuid.uuid4().hex
    archive_name = '{}-{}.jpg'.format(now.strftime('%Y%m%dT%H%M%SZ'),
                                      capture_id[:16])
    archive_path = archive / archive_name
    temporary = archive / ('.upload-' + uuid.uuid4().hex)
    uploaded.save(str(temporary))
    if temporary.stat().st_size < 1024:
        temporary.unlink(missing_ok=True)
        return jsonify({'error': 'uploaded image is unexpectedly small'}), 400
    os.replace(temporary, archive_path)

    latest_tmp = station_root / '.latest.jpg.tmp'
    shutil.copyfile(archive_path, latest_tmp)
    os.replace(latest_tmp, station_root / 'latest.jpg')

    source = request.form.get('source') or 'scheduled'
    latest = {
        'capture_id': capture_id,
        'captured_at': request.form.get('captured_at') or
                       now.isoformat(timespec='seconds'),
        'received_at': now.isoformat(timespec='seconds'),
        'source': source,
        'image_url': '{}/{}/latest.jpg'.format(
            CAMERA_PUBLIC_BASE, station_id),
        'archive_url': '{}/{}/archive/{}'.format(
            CAMERA_PUBLIC_BASE, station_id, archive_name),
        'bytes': archive_path.stat().st_size,
    }
    state = _camera_read_state(station_id)
    state['latest'] = latest
    queued = state.get('request') or {}
    if queued.get('id') == capture_id or source == 'instant':
        queued['status'] = 'complete'
        queued['completed_at'] = now.isoformat(timespec='seconds')
        state['request'] = queued
    _camera_write_state(station_id, state)
    _camera_cleanup_archive(archive)
    return jsonify({'status': 'success', 'latest': latest}), 200


@app.route('/health', methods=['GET'])
def health():
    return jsonify({'status': 'ok'}), 200


if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000)
