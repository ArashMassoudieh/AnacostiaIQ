#!/usr/bin/env python3
"""
test_project_api.py — acceptance tests for project namespacing.

Checks the behaviour that motivated the change: two projects may use the
same sensor id without overwriting each other, and readings written before
project support (bare sensor_id, no "#") stay reachable.

All writes go to throwaway sensor ids under throwaway projects and are
deleted afterwards. Existing data is only ever read.

    python3 scripts/test_project_api.py
    python3 scripts/test_project_api.py --api http://54.213.147.59:5000
    python3 scripts/test_project_api.py --token "$WIPE_TOKEN"   # enables cleanup

Without a token the destructive tests are skipped and the test rows are
left behind (the script prints what to remove).
"""
import argparse
import json
import sys
import urllib.error
import urllib.request
import uuid

API = "http://54.213.147.59:5000"
TOKEN = None

passed = failed = skipped = 0


def call(method, path, body=None, token=None):
    req = urllib.request.Request(API + path, method=method)
    data = None
    if body is not None:
        data = json.dumps(body).encode()
        req.add_header("Content-Type", "application/json")
    if token:
        req.add_header("X-Wipe-Token", token)
    try:
        with urllib.request.urlopen(req, data, timeout=30) as r:
            raw = r.read().decode()
            return r.status, (json.loads(raw) if raw else None)
    except urllib.error.HTTPError as e:
        raw = e.read().decode()
        try:
            return e.code, json.loads(raw)
        except ValueError:
            return e.code, raw


def check(name, cond, detail=""):
    global passed, failed
    if cond:
        passed += 1
        print("  PASS  " + name)
    else:
        failed += 1
        print("  FAIL  " + name + ("  -- " + str(detail) if detail else ""))


def skip(name, why):
    global skipped
    skipped += 1
    print("  SKIP  " + name + "  -- " + why)


def main():
    global API, TOKEN
    ap = argparse.ArgumentParser()
    ap.add_argument("--api", default=API)
    ap.add_argument("--token", default=None,
                    help="X-Wipe-Token; enables DELETE tests and cleanup")
    a = ap.parse_args()
    API, TOKEN = a.api.rstrip("/"), a.token

    # Unique per run so concurrent runs cannot collide.
    tag = uuid.uuid4().hex[:8]
    sensor = "selftest_" + tag          # the SAME id in both projects
    p1, p2 = "selftest_a_" + tag, "selftest_b_" + tag
    ts = "2026-01-01T00:00:00Z"
    created = []

    print("API: " + API)

    print("\n[1] service is up")
    st, body = call("GET", "/health")
    check("GET /health -> 200 ok", st == 200 and body.get("status") == "ok",
          (st, body))

    print("\n[2] the collision case: same sensor id, same timestamp, two projects")
    st, _ = call("POST", "/sensor", {"project": p1, "sensor_id": sensor,
                                     "value": 11.0, "unit": "m", "timestamp": ts})
    check("POST project A -> 200", st == 200, st)
    created.append((p1, sensor))
    st, _ = call("POST", "/sensor", {"project": p2, "sensor_id": sensor,
                                     "value": 22.0, "unit": "m", "timestamp": ts})
    check("POST project B -> 200", st == 200, st)
    created.append((p2, sensor))

    st, a_rows = call("GET", "/sensor/%s?project=%s" % (sensor, p1))
    st2, b_rows = call("GET", "/sensor/%s?project=%s" % (sensor, p2))
    check("project A keeps its own value",
          st == 200 and len(a_rows) == 1 and a_rows[0]["value"] == "11.0", a_rows)
    check("project B keeps its own value",
          st2 == 200 and len(b_rows) == 1 and b_rows[0]["value"] == "22.0", b_rows)
    check("B did not overwrite A (this is the regression)",
          a_rows and b_rows and a_rows[0]["value"] != b_rows[0]["value"])

    print("\n[3] the wire format hides the composite key")
    check("sensor_id comes back bare, not 'project#sensor'",
          a_rows and a_rows[0]["sensor_id"] == sensor, a_rows)
    check("project is echoed back",
          a_rows and a_rows[0].get("project") == p1, a_rows)

    print("\n[4] project scoping")
    st, ids = call("GET", "/sensors?project=" + p1)
    check("GET /sensors?project= lists only that project",
          st == 200 and ids == [sensor], ids)
    st, projects = call("GET", "/projects")
    check("GET /projects includes both test projects",
          st == 200 and p1 in projects and p2 in projects, projects)
    check("GET /projects reports legacy rows as null",
          None in projects, projects)

    print("\n[5] legacy (pre-project) readings are still reachable")
    st, rows = call("GET", "/sensor/" + sensor)      # no project
    check("querying without project does not see project rows",
          st == 200 and rows == [], rows)
    st, legacy = call("GET", "/sensor/precip_amount")
    check("an existing unnamespaced sensor still returns data",
          st == 200 and len(legacy) > 0, len(legacy) if st == 200 else st)
    check("legacy rows carry no project field",
          st == 200 and all("project" not in r for r in legacy[:50]))

    print("\n[6] range queries still filter")
    st, inrange = call("GET", "/sensor/%s?project=%s&start=2025-12-31&end=2026-01-02"
                       % (sensor, p1))
    check("start/end bracket the reading", st == 200 and len(inrange) == 1, inrange)
    st, outrange = call("GET", "/sensor/%s?project=%s&start=2030-01-01" % (sensor, p1))
    check("a range past the data returns nothing",
          st == 200 and outrange == [], outrange)

    print("\n[7] input validation")
    st, body = call("POST", "/sensor", {"project": "bad#name",
                                        "sensor_id": "x", "value": 1})
    check("'#' in a project name is rejected with 400", st == 400, (st, body))

    print("\n[8] destructive ops are token-gated")
    st, _ = call("DELETE", "/sensor/%s?project=%s" % (sensor, p1))
    check("DELETE without a token -> 401", st == 401, st)
    if TOKEN:
        st, body = call("DELETE", "/sensor/%s?project=%s" % (sensor, p1),
                        token=TOKEN)
        check("DELETE with a token removes only that project",
              st == 200 and body.get("deleted") == 1, body)
        st, left = call("GET", "/sensor/%s?project=%s" % (sensor, p2))
        check("the other project's reading survives the delete",
              st == 200 and len(left) == 1, left)
        st, _ = call("DELETE", "/sensor/%s?project=%s" % (sensor, p2), token=TOKEN)
        check("cleanup of the second project", st == 200, st)
        created = []
    else:
        skip("DELETE with a token", "no --token given")
        skip("scoped delete leaves other projects alone", "no --token given")

    if created:
        print("\nNOT cleaned up (no token). Remove with:")
        for proj, sid in created:
            print('  curl -X DELETE "%s/sensor/%s?project=%s" -H "X-Wipe-Token: $WIPE_TOKEN"'
                  % (API, sid, proj))

    print("\n%d passed, %d failed, %d skipped" % (passed, failed, skipped))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
