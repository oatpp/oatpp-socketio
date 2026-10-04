#!/usr/bin/env python3
"""Mutation check: is a test actually testing the behaviour it claims?

A test that passes with the behaviour removed is not testing that behaviour.
This reverts one product decision at a time, rebuilds, and runs the tests that
are supposed to notice. Anything that still passes is a hole in the suite.

Every entry here is a decision that was made deliberately, so each one is
worth more than a generic coverage number - and one of them (auto-create on
the connect path) was found exactly this way: the flag could be flipped back
to its old, exploitable default and the integration test did not care.

    ./tools_mutation_check.py                 # all of them
    ./tools_mutation_check.py --list          # what it would do
    ./tools_mutation_check.py mayPublish-ignored

Exits non-zero if any mutation went unnoticed. Files are restored even when a
build or a test blows up; if the script is killed hard, `git diff` shows what
is left behind.
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))

# (name, file, text to replace, replacement, tests that must FAIL)
MUTATIONS = [
    ("auto-create-on-by-default",
     "include/oatpp_sio/sio/sioServer.hpp",
     "    bool autoCreateSpaces = false;",
     "    bool autoCreateSpaces = true;",
     ["unit.SioServerTest", "integration.NamespacePolicyTest"]),

    ("connect-ignores-auto-create",
     "src/sio/sioServer.cpp",
     "    if (!space && autoCreateSpaces) {",
     "    if (false) {",
     ["unit.SioServerTest", "integration.NamespacePolicyTest"]),

    ("refusal-sends-DISCONNECT",
     "src/sio/adapter.cpp",
     "        msg->body = encodeConnectError(packet.nsp, reason);",
     "        msg->body = encodeDisconnect(packet.nsp);",
     ["integration.NamespacePolicyTest", "integration.AuthTest"]),

    ("mayPublish-ignored",
     "src/sio/adapter.cpp",
     "    if (!SioServer::serverInstance().authPlugin()->mayPublish(packet.nsp, self)) {",
     "    if (false) {",
     ["integration.AuthTest"]),

    ("auth-check-skipped",
     "src/sio/sioServer.cpp",
     "    if (!auth->mayConnect(spaceName, listener, reason)) {",
     "    if (false && !auth->mayConnect(spaceName, listener, reason)) {",
     ["unit.AuthPluginTest", "integration.AuthTest"]),

    ("out-params-not-cleared",
     "src/sio/sioServer.cpp",
     "    sioId.clear();\n    reason.clear();",
     "    // mutated: output parameters are not cleared",
     ["unit.AuthPluginTest"]),

    ("drop-root-allowed",
     "src/sio/sioServer.cpp",
     '    if (id == "/") {',
     "    if (false) {",
     ["unit.SioServerTest", "integration.NamespacePolicyTest"]),

    ("drop-with-members-allowed",
     "src/sio/sioServer.cpp",
     "    if (space->size() > 0) {",
     "    if (false) {",
     ["unit.SioServerTest", "integration.NamespacePolicyTest"]),

    ("null-plugin-installed",
     "src/sio/sioServer.cpp",
     "    if (!plugin) {",
     "    if (false) {",
     ["unit.AuthPluginTest", "integration.AuthTest"]),

    ("maxpayload-not-checked-before-reading",
     "include/oatpp_sio/webapi/controller/sioController.hpp",
     "            if (theEngine->exceedsMaxPayload(declared)) {",
     "            if (false) {",
     ["integration.MaxPayloadTest"]),

    ("maxpayload-not-checked-after-reading",
     "include/oatpp_sio/webapi/controller/sioController.hpp",
     "            if (oatpp_sio::eio::theEngine->exceedsMaxPayload(\n                    static_cast<long long>(body->size()))) {",
     "            if (false) {",
     ["integration.MaxPayloadTest"]),

    ("maxpayload-not-checked-on-websocket",
     "src/eio/wsConnection.cpp",
     "        if (buffered + static_cast<unsigned long long>(size) > limit) {",
     "        if (false) {",
     ["integration.MaxPayloadTest"]),

    ("registry-autocreate-unlocked",
     "src/sio/sioServer.cpp",
     "    {\n        std::lock_guard<std::mutex> guard(stateLock);\n        space = findSpaceLocked(spaceName);",
     "    {\n        space = findSpaceLocked(spaceName);",
     ["unit.RegistryConcurrencyTest"]),

    ("registry-join-unlocked",
     "src/sio/sioServer.cpp",
     "        std::lock_guard<std::mutex> guard(stateLock);\n        if (findSpaceLocked(spaceName) != space) {",
     "        if (false) {",
     ["unit.RegistryConcurrencyTest"]),

    ("space-getlistener-unlocked",
     "src/sio/space.cpp",
     "    std::lock_guard<std::mutex> guard(lock);\n    auto iter = subscriptions.find(id);",
     "    auto iter = subscriptions.find(id);",
     ["unit.RegistryConcurrencyTest"]),

    ("space-addlistener-unlocked",
     "src/sio/space.cpp",
     "void Space::addListener(SpaceListener::Ptr listener)\n{\n    std::lock_guard<std::mutex> guard(lock);",
     "void Space::addListener(SpaceListener::Ptr listener)\n{",
     ["unit.RegistryConcurrencyTest"]),

    ("space-removelistener-unlocked",
     "src/sio/space.cpp",
     "void Space::removeListener(const std::string& id)\n{\n    std::lock_guard<std::mutex> guard(lock);",
     "void Space::removeListener(const std::string& id)\n{",
     ["unit.RegistryConcurrencyTest"]),
]

UNIT_BIN = "./build/test/sio-unit-tests"
INTEGRATION_BIN = "./build/test/sio-integration-tests"


def run(cmd):
    p = subprocess.run(cmd, shell=True, cwd=ROOT,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return p.returncode, p.stdout.decode(errors="replace")


def build():
    rc, out = run("cmake --build build -j{}".format(os.cpu_count() or 4))
    return rc == 0, out


# Concurrency tests are probabilistic: one clean run of a racy program means
# nothing. These are run several times and count as passing only if every run
# passes.
REPEAT = {"unit.RegistryConcurrencyTest": 5}


def test_passes(name):
    binary = UNIT_BIN if name.startswith("unit.") else INTEGRATION_BIN
    arg = name.split(".", 1)[1]
    for _ in range(REPEAT.get(name, 1)):
        rc, _ = run("{} {}".format(binary, arg))
        if rc != 0:
            return False
    return True


def check(mutation):
    """apply one mutation, report whether the tests noticed, restore"""
    name, path, old, new, expected = mutation
    full = os.path.join(ROOT, path)
    original = open(full).read()

    if original.count(old) != 1:
        return name, "SKIPPED", ["pattern appears {} times in {}".format(
            original.count(old), path)]

    notes = []
    open(full, "w").write(original.replace(old, new))
    try:
        ok, out = build()
        if not ok:
            errors = [l for l in out.splitlines() if " error:" in l][:3]
            return name, "BUILD-FAILED", errors
        for t in expected:
            if test_passes(t):
                notes.append("{} still passes".format(t))
    finally:
        open(full, "w").write(original)
        build()  # leave the binaries matching the real sources

    if notes:
        return name, "NOT-DETECTED", notes
    return name, "load-bearing", []


def main(argv):
    what = argv[1:]
    if "--list" in what or "-l" in what:
        for m in MUTATIONS:
            print("{}\t{}".format(m[0], ", ".join(m[4])))
        return 0

    selected = [m for m in MUTATIONS if not what or m[0] in what]
    if not selected:
        print("no such mutation; --list shows the known ones")
        return 2

    results = []
    for m in selected:
        name, verdict, notes = check(m)
        results.append((name, verdict))
        print("{:15s} {}{}".format(
            verdict, name,
            "" if not notes else "   <- " + "; ".join(notes)))

    print("\n{} of {} mutations detected".format(
        sum(1 for r in results if r[1] == "load-bearing"), len(results)))
    return 0 if all(r[1] == "load-bearing" for r in results) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
