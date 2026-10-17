# Replay parser regression test

Configure with `-DPARSER_ENABLE_TESTS=ON` and build the `lux_review_regression`
target. Run `ctest --test-dir build -R lux_review_regression --output-on-failure`
with `WRPL_TEST_GAME_PATH` set to the War Thunder installation directory. The
test skips when that variable is absent.

Set `WRPL_TEST_REPLAY_PATH` to a server replay directory containing a rocket
with a resolvable owner and weapon to include the owner-destruction check. The
test destroys an entity in its own in-memory parser state; it does not modify
the replay files. Without this variable, only the synthetic packet cases run.
