# Initial Lava shield AI playtesting

These telemetry summaries come from the original revision-1 Linux executable,
before the bounded starter-patch fallback. The default 256-square four-colony
and minimum 128-square two-colony seeds used one generation candidate and were
played through 16,384 ticks with Nicowar, Numbi and Maxima mirrors. All six
games completed, every colony survived, and representative finished native
reports found zero disconnected walking pairs. Nicowar and Maxima expanded and
fought; Numbi built more modestly. The final code's newly viable starts may
alter some other seeds, so these games are initial feel evidence rather than a
complete playtest of the paired 504-request final cohort.

The attached [per-game summary](game-summary.json) and [512-tick time series](game-series.csv)
retain population, buildings, resource access, combat and hunger observations.
A resource-mix probe at 125% wheat/75% wood added several hundred wheat tiles
but worsened Nicowar's final critical hunger and starvation deaths on two
paired 256-square seeds, so 100% wheat and wood stayed as defaults. A separate
512-square twelve-colony Maxima game continued through 32,768 ticks after a
native map saved successfully; its [summary](dense-summary.json) and
[512-tick series](dense-series.csv) show the long-run economy and contact.

These tables can be reviewed in this PR. Full saves, replays and framework
attempt logs remain in the original local `artifacts/lava-shield/playtest/`
study directory; they are not part of the checked-in bulk evidence package.
Human play remains necessary to judge the crater rim and narrow beach detours.
