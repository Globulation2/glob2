Tested dd89225a8 against base 6d968edae030c373581ec3d9a627a8b1333755e8 on Ubuntu 26.04 x86_64 / Python 3.14.

Executed the revised workflow's embedded Python against the actual downloaded `browser-determinism-*` artifacts from full run 37184200265. All seven complete native/browser verification traces match one another and the committed golden trace. Exercised all four combinations of compatibility/macOS selection (5, 6, 6, 7 traces); each passes its complete inventory and rejects a deliberately removed trace. These are artifact comparisons, not newly simulated runs.

`python3 -m unittest discover -s test/build_system -p 'test_ci*.py'`: 84 tests passed. Hosted cheap build/runner/simulation/vendor contracts also passed. The full local build-system suite is being checked with the workflow's pinned asset encoder; the initial host-Pillow invocation failed four unrelated asset fixture checks because the subprocess encoder cannot observe the test's monkeypatches. Its failure log is retained and is not claimed as a pass.

No simulation or workflow coverage changes. Full hosted verification of the merged master follows this repair.

Final full local contract command: `"$(python3 tools/package_assets.py --encoder-python)" -m unittest discover -s test/build_system`. **297 passed, three expected skips, 33.520 seconds.** The pinned encoder runtime restores the same in-process assumptions used by hosted CI. Initial host-Pillow failures remain in the separate log.
