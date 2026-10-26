# User guide

Using Track Rat day to day: driving, settings, tracks, power, the Wi-Fi page and session logs. Back to the [README](../README.md).

## Using it

**At a track:** after the logo, once the GPS has a fix, the nearest track within 5 km is picked automatically. If you're already moving (20 km/h or more), the session starts at once. If you're parked, you get that track's Ready screen, and the session starts by itself when you drive off (20 km/h for 2 seconds). Until then **MENU** (or pressing BOOT) takes you to the home screen, so a track made at home doesn't trap you in a session. Timing begins the first time you cross the start/finish line; everything before that is the out lap.

**Sessions run themselves.** Park (under 5 km/h) for a minute and you're in the pits: the [debrief](#the-pit-debrief) comes up (or, before there are corner sectors, the session's summary with its laps and their tags). Drive off again within 10 minutes of stopping (a red flag, a quick check) and the same session carries on, back on the timing screen. Stay parked longer and the session ends; the next time you drive off at the track, Session 2 starts by itself, then 3, and so on (leaving the track doesn't start one). Sessions are numbered per track and per local day, and the count is kept in flash, so the first run after a power cut is the next session too. Holding the screen still ends a session at any time.

**Away from a track:** you get the home screen. Drive off from it (20 km/h for 2 seconds) and Free Roam starts by itself.

**The home screen** leads with what's next:

- **NEXT UP:** the big card at the top. One tap does the most likely thing, and it picks itself:
  1. **Today's autocross course** within 5 km: "Autocross · Oct 25 course", "Run 4 next · best 52.31". A tap starts timing runs on it, armed for the next one.
  2. **A circuit** within 5 km: its name and layout and your PB, such as "Calabogie · Full" and "PB 2:43.61". A tap opens its Ready screen.
  3. **Drive:** "Free Roam, it learns tracks". A tap starts Free Roam.
  With no GPS fix yet it says "Waiting for GPS…", and a tap shows the GPS status.
- **TRACK DAY:** the circuits list. The nearest circuit comes first, then the rest in order of distance. Layouts of one venue stay together. Without a GPS fix the list is in `tracks.json` order, and it sorts itself as soon as there is a fix. **+ New circuit** comes after the last track, just before the **Bench Test**, which is always last. On a unit used with BOOT only, a **Back** entry follows the Bench Test.
  - **+ New circuit** creates a track where you are. Start, drive to the start/finish line and **tap the screen as you cross it**. That sets the line and starts lap 1. After your first full lap, the lap becomes the track's map, saved when you finish the session. The track is saved to `tracks.json`, so next time it's picked automatically. It needs a GPS fix.
  - Each track shows how far away it is and, once you have one, its PB.
- **AUTOCROSS:** the Autocross hub: today's course, its runs, and **New course** (see [Point to point](#point-to-point-autocross-hill-climbs-sprints)).
- **DRAG:** times acceleration and braking runs (0-100, quarter mile and so on); see [Acceleration and braking](#acceleration-and-braking). It works without a GPS fix, for the bench demo.
- **DRIVE:** Free Roam, which records your drive. A map of your route draws as you go, with elapsed time, live speed, distance and top speed. Hold the screen to finish; the summary (and the route) is saved to the SD card, unless **Record road drives** is off in [Settings](#settings). Then the drive is shown but not saved. It needs a GPS fix: without one its tile is dimmed and a tap shows the GPS status. Free Roam also watches for a track: see [Tracks that learn themselves](#tracks-that-learn-themselves) below.
- **WI-FI** and **SETTINGS:** the small row at the bottom. Wi-Fi turns on the Wi-Fi page (see [Wi-Fi page](#wi-fi-page)); Settings has the units, brightness and the rest (see [Settings](#settings)).
- **The top bar** opens the GPS status.
- **BOOT only:** press goes through Next up, Track day, Autocross, Drag, Drive, Wi-Fi and Settings, round and round; hold chooses the one outlined.

**Tracks with no start/finish line yet** (from the Canada list, or created on the timer) ask you to tap the screen as you cross the line the first time. **SET LINE** on a track's Ready screen marks a new line for any track, for example if a stored line is in the wrong place. The direction you're driving when you tap is saved with the line, so crossing it the other way later doesn't count. This needs you to be doing at least 15 km/h when you tap; slower than that the GPS direction is too rough, and the line is saved without one.

**Layouts:** a venue with several layouts (Full, Short, the kart track) lists each one separately, and its Ready screen shows which layout is picked. Tap the top bar of the Ready screen for the next layout. You don't have to get it right: after your first full lap, the timer compares your lap with the map of each layout there (and any other track within 3 km that has a map). If another one clearly fits better, it switches to it and shows **Layout: ...** on screen. If you haven't crossed the picked layout's line after 2 km, it checks the same way. It doesn't when you're putting a line down yourself (SET LINE, + New circuit): the line goes on the track you chose.

| Screen | Touch | BOOT button (or big button) |
|---|---|---|
| Waiting for GPS | tap: menu | press: menu |
| Home | tap Next up, one of the four tiles (Track day, Autocross, Drag, Drive), Wi-Fi or Settings; top bar: GPS status | press: next (Next up, the four tiles, Wi-Fi, Settings), hold 1 s: choose |
| Autocross hub | Start runs: left two thirds of the top button row, New course: right third; Back: bottom right, top bar or hold; Adjust start and finish: bottom left and middle | press: next button that works, hold 1 s: choose |
| New course | left: back, centre or right: Set start here (only while stopped with a fix), top bar or hold: back to the hub | press: the other, hold 1 s: choose |
| Track day (the circuits list) | left / right third: previous / next, centre: select (or start, on + New circuit), top bar or hold: home | press: next, hold 1 s: select (or start, on + New circuit; go home, on Back) |
| GPS Status (top bar of the home screen) | tap: back | press: back |
| Ready | centre: start, left: menu (back, if you came from Track day), right: set line, top bar: next layout | hold 1 s: start, press: menu (back, from Track day) |
| Timing | tap: timing / map view (the 5 seconds after a new course's first finish tap: undo the finish), hold 2 s: end | press: switch view, hold 1 s: end |
| In the pits (parked a minute while timing): the debrief | tap: second page, then the timing screen, hold 2 s: end; driving off goes back by itself | press: the same as a tap, hold 1 s: end |
| Setting the line | tap as you cross the line, hold 2 s: end | hold 1 s: end |
| Free Roam | hold 2 s: end | hold 1 s: end |
| Performance | left: rollout / demo, centre: results, right: menu, hold 2 s: menu | press: next screen, hold 1 s: menu |
| Lap session summary | left button: menu, centre button or any other tap: Ready screen again, right button: Wi-Fi page. After a session that ended by itself, driving off starts the next | press: Ready screen again, hold 1 s: Wi-Fi page |
| Free Roam summary | tap: menu; WI-FI button: Wi-Fi page | press: menu, hold 1 s: Wi-Fi page |
| Wi-Fi | tap: Wi-Fi off (not while firmware installs) | press: Wi-Fi off |
| Settings | tap a row, drag to scroll, top bar: back (to the home screen from the list) | press: next row, hold 1 s: open or flip it (on a choice page: choose it; on Brightness and Time zone: press is +, hold goes back; on Coolant: press is + on the highlighted value (past the top it goes round to just above the limit below it), hold goes to the next, and back after the last) |
| First-boot setup | tap the choice on the left or right | press: the other choice, hold 1 s: choose it |
| Any screen | Settings → **Power off**; when it's off, a tap turns it back on | hold 3 s: [power off](#battery-power-off-and-sleep) (BOOT only, not the big button). When it's off, a press turns it back on |

**A BOOT hold acts when you let go:** hold it for a second, then let go (before 2 s) to choose, start or end. Held on past 2 s it's switching off instead, so holding to switch off never starts anything on the way, and letting go before the bar is full does nothing at all. The big button acts as soon as it has been held 1 s.

While timing, the screen is built to be read in one glance, top to bottom:

- **The header:** lap number and the **session clock** (time since the session started, or the time left when a [Session length](#settings) is set; yellow for the last minute, then "Time up"), battery, GPS accuracy and signal bars.
- **The band:** the big number on a full-width band, **green when you're ahead** of the reference lap and **red when you're behind**, with black digits that hold up in sun. The **Big number** setting picks what it shows: the current lap time (the default), the live diff, or the predicted lap time (the reference lap plus the diff). With the lap time or predicted lap on the band, the diff is in its top corner, sign and all. Until there's a reference lap the band is grey and shows the lap time.
- **The trend bar** beside the band: whether you've been gaining or losing over the last 3 s. It grows **up when gaining** and down when losing, full height at 0.3 s.
- **Two smaller numbers** under the band: whichever of the lap time, predicted lap and best lap aren't on it (last and best lap before there's a diff).
- **The sector strip** along the bottom: one block per corner sector, filled in as you finish each one. **Purple:** your best ever there. **Green:** your best of the day. **Yellow:** slower. Before a track has corner sectors, the last lap is shown there instead.
- **Split time:** for 5 s after each split, the split time and its gap to your best lap's split, over the strip.

Colour is never the only signal: the diff's `+` or `−` is drawn large, and the trend bar's direction says the same thing. For red-green colour blindness, the **Delta colours** [setting](#settings) swaps green and red for blue and orange.

The **live diff** is how far ahead (`−`) or behind (`+`) the reference lap you are right now, at the same point on track. It appears once there is a reference lap (see Compare with below).

At the line, **the whole screen flashes** the lap time and its gap to the reference for 4 s: purple for a new personal best (PB) at that track, green (blue) for a new best of the day, grey for any other lap.

A tap switches between the timing and map views, moving or not. The "keep holding to end" bar only appears once a press lasts longer than a tap.

**The screen follows the situation.** Nobody should have to switch screens on track:

- **Fast:** the timing screen (or the map view, if you picked it).
- **Slow:** under 40 km/h for 5 s (`SLOW_SCREEN_KMH`, `SLOW_SCREEN_MS`: below the slowest hairpins), behind a safety car or in the pit lane, the **slow screen** shows your speed in big digits, the last lap and the session clock. When the track's map marks its pit lane (see [Track maps](development.md#track-maps)), being in it counts as slow at any speed, and the speed turns yellow. The timing screen comes back as soon as you're over 40 km/h and out of the pit lane. The lap flash at the line still shows.
- **Parked:** the [debrief](#the-pit-debrief).

The session clock keeps running through a red flag (driving off within 10 minutes is the same session), so a countdown matches the run group.

**Best laps and PBs.** Your best clean lap ever at each track (each layout separately) is kept on the card, path and all, with its date, and so is your best lap of the day. Track day and the Ready screen show the PB and its date. The **Compare with** [setting](#settings) decides what the live diff compares with from your first lap:

- **Best ever:** your PB, until you beat it.
- **Best of the day:** your best lap today at this track, across sessions. Before you've set a lap today, your PB.
- **This session** (the default): only laps from this session, so there's no live diff until lap 2.

Whichever is picked, a quicker lap in this session takes over as the reference. Only clean laps (no [tags](#using-it)) become a PB or the best of the day. Moving the start/finish line more than 5 m (SET LINE) starts the PB again, as times at another line can't be compared.

The live diff compares your position with the path your best lap took. If you're more than 40 m off that path (`LIVE_DELTA_MAX_OFFSET_M`), for example in the pit lane, it shows `--`.

**Corner sectors.** After your first clean lap at a track, Track Rat cuts that lap into sectors, one per corner or complex: each starts at the fastest point before a braking zone, so it holds the braking, the corner and the straight after it, and a good exit counts for the corner that earned it. Braking zones closer than 3 s are joined (a chicane is one sector), and a fast sweeper you take flat out isn't one. They're numbered T1, T2 and so on from the line, saved with the track in `tracks.json` (`"sectors"`) when the session ends, and every lap after is timed through them. Your own splits stay separate, as S1, S2... Moving the start/finish line (SET LINE) makes them again from the next clean lap; to make them again otherwise, delete the track's `"sectors"` from `tracks.json`.

**Lap tags.** Each lap is checked, and a lap that can't be trusted is tagged:

- **PIT:** the car stopped (under 5 km/h) during the lap: a pit stop, a red flag, a spin.
- **SLOW:** more than 107% of the best lap: a cool-down lap, traffic, yellow flags.
- **SUSPECT:** a gap of more than 1 s between GPS fixes, a fix less accurate than ±5 m (u-blox only), or a lap more than 5% longer or shorter than the session's usual lap (a cut, once there are 3 laps to compare).

A tagged lap is never the best lap or the live-diff reference, and isn't counted in the average. The summary lists the tags beside the lap times. The slow and length checks are made again after every lap, so a short first lap that looked like the best loses that place once later laps show it was cut; the next clean lap then stands in as the live-diff reference until there's a new best.

### Point to point: autocross, hill climbs, sprints

A **stage** has a start and a separate finish (plus any splits). Each run counts as a lap: best run, live delta to it, sector colours, PB, debrief.

- **Autocross courses are events.** A course is a stage made for one event day: it's saved with `"kind": "course"` and `"event": "20261025"` (the local date it was made), and named **Oct 25 course** (**Oct 25 course 2** for a second one the same day; rename it on the Wi-Fi page). Courses stay out of the track list, which is for circuits, and out of the nearest-track pick for circuits. Hill climbs and sprints are stages without a kind and are listed and picked as before. A stage named **Course 2026-10-20** (made by the old NEW TRACK > Course) counts as a course for that date, and is marked as one in `tracks.json` at the next boot.
- **The Autocross hub** (the **Autocross** tile on the home screen) shows today's course if there's one within 5 km, otherwise the most recent course within 5 km. It shows the name, **Run 4 next**, the best run and the last run, and the day's runs (the best in green; a run with a pit or suspect tag is never the best). Its buttons:
  - **Start runs:** timing on that course, armed for a standing start.
  - **New course:** park at the start (in the grid or at the lights) and press **Set start here**. It needs a GPS fix and the car stopped (under 3 km/h). It makes the course at that spot and starts timing, armed.
  - **Adjust start** and **Adjust finish** move the course's line in small steps and re-time the day's runs as you go (see "Adjusting a line on the device" below). Each is greyed out, with the reason under it, until it can work: **needs a run** (no runs on that day yet), **set finish first** (the finish is set by tapping on the first run), **needs SD card**, **reading runs...** or **re-timing...**.
  - **Back.** With BOOT only: press moves, hold chooses.
- **Adjusting a line on the device.** A tap made while driving lands a little early or late, and 0.5 m is only about 20 ms at 90 km/h. From the hub, **Adjust start** or **Adjust finish** shows the day's runs and lets you slide the line along the way the cars go, with every run's time changing as it moves.
  - **Reading runs...** comes first: the day's session logs are read from the card a little at a time (the screen keeps working), keeping only the fixes near the line, for the latest 20 runs or fewer. It needs the full logs on an SD card; without them the screen says there's nothing to adjust.
  - **The screen:** a zoomed view about 36 m across with each run's path (the quickest in green, cars going left to right) and the line in yellow once moved (a dotted mark shows where it was). **Moved +1.5 m (later)** says how far it has gone, up to 15 m either way. Below, each run: its number, the old time, the new time and the difference (the run with the best new time marked, and shown in the line above).
  - **Moving it:** touch **0.5 m earlier** or **0.5 m later**; hold one down and it repeats, then goes faster. **Reset** puts it back. With BOOT only: **hold** to pick the button (the highlight goes Later, Earlier, Reset, Cancel, Save), **press** to use it.
  - **Save** writes the moved line to `tracks.json` and times the course's logs again (the same re-timing as the Wi-Fi page's), then goes back to the hub once the summaries and the best laps are right (**Re-timing...** shows until then; **Back to the hub** doesn't stop it). **Cancel**, or the top bar, changes nothing. Save with the line not moved changes nothing either.
  - **Standing starts:** Adjust start shows a note when some runs started standing. Those runs are timed from moving off, so the start line only changes them if it goes more than 1 m ahead of where the car stood (a start made with New course has no direction, so only if it ends up more than 25 m from the car). Rolling starts and every run's finish follow the line exactly. The Wi-Fi page's track editor does the same on a bigger screen.
- **Runs are numbered for the day:** Run 1, 2, 3... of that course on its event date, counted across every session, so a power-off or a session ending doesn't start them again. They're worked out from the day's session summaries on the card (not the logs), and the timing screen and the summary say "Run 4" rather than "Lap". Without an SD card the numbering carries on from the flash summaries; with neither, from the runs since the power came on.
- **At boot,** today's course within 5 km starts timing straight away, armed for the next run, so a restart between runs needs no taps. The timing screen says **Run 4 next** (green once armed).
- **The finish on a new course:** the first run taps as it crosses the finish. A bar then shows for 5 seconds with **Tap to undo** (BOOT does the same): it takes the finish back and the run keeps timing, so tap again at the real finish. After the 5 seconds the finish stands. (Only the first run of a course has this: later runs finish by themselves.)
- **Standing start:** stopped within 25 m of the start, the run is **armed** (it says so on screen) and the clock starts the moment you move. If the start line has a direction and you're stopped more than 1 m behind it (timing lights a little way on), the clock starts as you cross the line instead. A start line with a direction also takes a **rolling start**: driving through it starts a run. A start with no direction (a New course start) only does standing starts, so driving back to the grid never starts one.
- **Finish:** crossing the finish line ends the run (only the way it was set, if it has a direction). With no finish yet, **tap the screen as you cross it** on the first run: that sets the finish (with your direction, above 15 km/h) and times the run. Or don't: a run that stops for 5 s before a finish ends untimed, and you can set the finish later on the Wi-Fi page's [track editor](#track-editor-and-re-timing) (**Set finish**) and re-time the session.
- A run that stops for 5 s before the finish (a spin, a red flag) ends without a time.
- Waiting in line between runs: parked for a minute shows the debrief as usual, but the session doesn't end after 10 minutes; the whole event stays one session. A stage within 5 km at boot (or today's course, above) starts timing straight away, so the first run's standing start isn't missed. The slow screen never takes over during a run.
- In `tracks.json` a stage has `"type": "stage"` and a `"finish"` next to its `"startFinish"` (the start). Its `.vbo` says `Type: stage` and records the finish, and its `[laptiming]` has **Start** and **Finish** lines for Circuit Tools (when they have a direction).
- The thresholds are in `Config.h` (`STAGE_...`).

### Tracks that learn themselves

At a track Track Rat doesn't know, just drive. When you drive the same loop twice in Free Roam, it's a track:

- **A loop** is coming back past a point you passed at 20 km/h or more, heading the same way, within 10 m, at least 20 s and 300 m later. **Twice** means passing it a third time after a second loop about as long as the first (within 25%). One lap round the block, a drive out and back along the same road, or a hill climb and the drive back down never count.
- Track Rat then creates the track (**My Track N** in `tracks.json`), puts a provisional start/finish line where the first loop began, the way you were heading, and switches to a timing session there (**NEW TRACK: My Track N** shows on screen). It times both loops from the fixes it kept, so lap 1 and lap 2 are in the session's laps and log, and times every lap after that. The Free Roam drive up to then is saved as usual.
- If the loop is within 1.5 km of a track in the list that has no line yet, that track gets the line instead of a new one being made. If it's at a track that already has a line (Free Roam picked by hand), the session starts at that track, timed with its own line.
- The line is wherever the loop happened to begin. Move it on the Wi-Fi page's [track editor](#track-editor-and-re-timing) and re-time the session. Rename it in `tracks.json`.
- **Record road drives** off: Free Roam still watches for a loop but saves nothing; once it's a track, the session is saved like any other.
- Both loops have to fit in 12 minutes, as the fixes are kept in memory (PSRAM) to time them.
- Track day's **+ New circuit** and its tap-at-the-line stay, for the exact line straight away.

The rules and limits are in `Config.h` (`LEARN_...`). `TRACK_LEARNING_ENABLED 0` turns it off: the home screen then waits for you to pick.

### The pit debrief

When you park, the screen answers "where's the time?":

- **Best lap, theoretical best and the gap** ("0.70 s on the table"). The theoretical best is the sum of your best time in each corner sector this session. Underneath: how many laps were used (clean laps timed through every sector; tagged laps are left out) and the theoretical best ever at this track (layout).
- **The three sectors where your best lap lost the most** against your best in that sector, marked 1, 2, 3 on the map, each with the lap the best came from and one hint from comparing the two laps' GPS speed through it: **braked 8 m early**, **4 km/h slower at the apex** (the slowest point) or **3 km/h slower on exit**, whichever cost the most. The losses in every sector add up exactly to the gap.
- **Tap** for the second page: every lap with its tag, every sector's best time with the lap it came from and what your best lap lost there, and this session against the last one here ("Best 1.04 s quicker than session 1"). Tap again for the timing screen; driving off goes back to it by itself. Tapping the **top bar** shows the SYNC flash for lining up a video ([Overlay videos](phone-and-video.md#overlay-videos)); so does the summary's top bar.

The best sector times ever and of the day (for the sector strip), and the last session's best, are kept in `/tracks/<track id>/stats.json`, next to the [best laps](#session-logs). Sector times set through other gates (sectors made again after SET LINE) start afresh.

## Bench test

The last entry in Track day is **Bench Test**. Pick it and press START to run a simulated car around a small circuit, with real lap timing, splits, live diff and map, all at your desk. No GPS is needed, and if one is connected it's ignored for that session.

While a bench test runs, a yellow **BENCH** tag shows in the header. When the session ends, the real GPS is used again.

## Settings

The choices a driver makes live in **Settings**, not in the code. Change them on the device or on the Wi-Fi page. They are kept in the board's flash (NVS, apart from the program and the SD card), so they survive switching off and re-flashing.

**On the device:** tap **Settings** on the home screen. It is a list like the phone's Settings, in five groups (Driver and car, Display, Timing, GPS & Bluetooth, Recording), then **Power off**. It is meant for the pits, before the session starts, so it is a normal touch list, not the big glove buttons of the timing screens.

- **Touch:** drag the list up and down to scroll it. Tap a switch (Screen upside down, Bluetooth, Record road drives) to flip it. Tap a row with a `>` to open its page: **Driver** and **Car** list their names with a tick on the current one (tap one to choose it; with many names the page scrolls); Speed units, Big number, Compare with, Delta colours, Session length and GPS mode show their options with a tick on the current one (tap one to choose it and go back); Brightness has a bar (tap a spot on it) and − and + buttons; Time zone has − and + for half an hour and shows the local time; **Coolant** has its four temperatures, each with its own − and +. **Engine data** and **Camera** open the Bluetooth OBD-II adapter's and the DJI camera's screens (see [Bluetooth OBD-II adapter](hardware.md#bluetooth-obd-ii-adapter)). **Power off** asks first: **Cancel** or **Power off** (see [Battery, power off and sleep](#battery-power-off-and-sleep)). The top bar goes back: to the list from a page, to the menu from the list. So does **Back to menu** at the end of the list. Under it, the firmware version (the git commit it was built from, `-dirty` if it had uncommitted changes) and its build date, the same as the Wi-Fi page shows.
- **BOOT (or the big button):** press moves a highlight down the list (the list scrolls with it and starts again after the last row), hold for 1 s opens or flips that row. On a page of options (or names), press moves down and hold chooses. On Brightness and Time zone, press is + (going round at the end) and hold goes back. On Power off, hold switches off and press goes back. A line at the bottom of the screen says so when the board has no touch screen.
- **Saved as you go:** there is no save button. A change takes effect at once, except **GPS mode**, which is used from the next restart.

| Item | Choices | What it does |
|---|---|---|
| Driver | The drivers you have set up (up to 8) | Who is driving. Their best laps and sessions are kept apart: see [Drivers and cars](#drivers-and-cars). Used from the next session. Names are added and renamed on the Wi-Fi page |
| Car | The cars you have set up (up to 8) | Which car. Chosen by itself when the OBD adapter reports a car's VIN that you have saved. Used from the next session |
| Speed units | km/h (default), mph | Every speed on the screens, including Performance, and on the Wi-Fi page. Saved files stay in km/h |
| Screen upside down | Off (default), on | For a unit mounted upside down. Draws the screen and reads touch turned round |
| Brightness | 10% to 100% in steps of 10 (default 100%) | Dims the backlight. Auto-sleep still turns it fully off |
| Compare with | Best of the day, best ever, this session (default) | What the live diff compares with from your first lap: see [Best laps and PBs](#using-it). Used from the next session |
| Big number | Lap time (default), delta, predicted lap | The largest number on the timing screen. Delta and predicted lap show the lap time until there is a best lap |
| Session length | Off (default), 20, 25 or 30 min | With a length, the timing screens' header counts down the time left in the session instead of counting up |
| Min lap time | Auto (default), 10 s, 15 s, 30 s | Line crossings sooner than this after the last are ignored (GPS jitter at the line). Auto is 20 s, or 10 s at a kart track. Used from the next session |
| Kart everywhere | Off (default), on | On: every track is timed as a kart track (8 m lines where `tracks.json` gives no width, the kart minimum lap). Off: only tracks marked `"kind": "kart"`. **Applied at the next restart** |
| Delta colours | Green / red (default), blue / orange | Ahead and behind on the timing screen, and the best-of-the-day colour. Blue and orange read better with red-green colour blindness |
| GPS mode | 20 Hz, GPS + Galileo (default); 10 Hz, all systems | 10 Hz uses all four satellite systems: more satellites, half the rate. **Applied at the next restart** |
| Bluetooth | On (default), off | The RaceBox output for phone apps. Off stops it at once |
| Bluetooth profile | RaceBox Mini (default), RaceChrono | What the phone sees, one at a time: a RaceBox Mini (most apps), or a RaceChrono DIY device with GPS and engine data. See [RaceChrono with engine data](phone-and-video.md#racechrono-with-engine-data). **Applied at the next restart** |
| Engine data | A screen, not a setting | Find, choose or forget a Bluetooth OBD-II adapter, and see how fast it answers. The row shows Connected only while the adapter is really there and answering, otherwise the remembered adapter's name, or None |
| Coolant | Cold below 70 °C, Warm at 100 °C, Hot at 110 °C, Oil hot at 130 °C (defaults), in 1 °C steps | The colours of the coolant temperature in the top bar, and when the full-screen overheat warning shows: see [Engine temperatures](hardware.md#engine-temperatures-coolant-and-oil). Shown in °F with Speed units in mph. Cold below, Warm at and Hot at always stay in that order: moving one past another moves the other along |
| Camera | A screen, not a setting | Pair or forget a DJI Osmo Action camera: see [DJI Osmo Action](phone-and-video.md#dji-osmo-action-track-rat-as-its-gps-remote-untested-on-hardware). The row shows Connected only while the camera is really there and sending its status, otherwise its name, or None |
| Camera recording | With sessions (default), manual | With sessions: a paired camera starts recording when you drive off in a session and stops when you park in the pits. Manual: only its own button, or a tap on its tag in the top bar |
| Time zone | UTC−12:00 to UTC+14:00 in 30-minute steps | For session file names. Shows the local time now once the GPS has a fix. A phone opening the Wi-Fi page sets it |
| Record road drives | On (default), off | Off: Free Roam drives are shown but not saved |

**First boot:** a new board asks two questions after the logo, before anything else. First, **which way up**: the question is printed at the top and again upside down at the bottom, with an arrow on each side. Tap the arrow that points up, whichever way the unit is mounted; the screen turns round at once if it was the other one. Then **km/h or mph**. With BOOT, press picks the other choice and hold chooses it. Both can be changed later in Settings.

**On the Wi-Fi page:** the **Settings** tab has the same items except the time zone, which it shows but takes from the phone. Changing anything asks for the 4-digit upload code on Track Rat's Wi-Fi screen once. A GPS mode change offers a **Restart now** button. The Bluetooth item is missing from the page on a build with Bluetooth left out.

**What a new board starts with** is set by the `DEFAULT_*` lines in `include/Config.h` (see [Configuration](development.md#configuration)). Pins and everything else stay there.

## Drivers and cars

A club unit or a shared car can keep each driver's and each car's best laps apart. Up to 8 drivers and 8 cars. A new board has **Driver 1** and **Car 1**, and while that is all there is, nothing here shows and nothing changes.

- **Choosing:** Settings > **Driver** and **Car**. A change applies from the next session, never in the middle of one. Add, rename, remove and choose them on the Wi-Fi page too, and give a car its VIN (see below). The page's request is `POST /api/profiles`; `GET /api/profiles` lists them.
- **On the Wi-Fi page:** a **Drivers and cars** card at the top of the Settings tab lists both. Each name has **Use** (make it the current one), **Rename** and **Remove** (asks first; off while it is the only one). Type a name under **Add driver** or **Add car**; the field goes away at 8. A name is 1 to 20 bytes (fewer letters with accents) and not used twice in its list. Each car has a VIN field. When Track Rat has read a VIN that no car has, each car offers **Use the VIN Track Rat read**. Changes ask for the 4-digit upload code once, like the other settings, and a refusal shows Track Rat's reason. In the Sessions tab, **Driver and car** on a session shows who drove it (`Dylan · Civic`), read from its summary when you tap it; it works in the viewer too.
- **On screen:** once there is more than one driver or car, Track day and the Ready screen show `driver · car` under the PB.
- **Best laps are kept for each pair.** Driver 1 with Car 1 keep the files in `/tracks/<track id>/` exactly as before, so PBs from before drivers and cars are still theirs. Every other pair has a folder inside the track's: `/tracks/<track id>/d3-c2/` (driver 3, car 2) with its own `best.bin`, `day.bin` and `stats.json`. The live delta, the PB on the screens and the pit debrief's records all follow the pair of the session. Ids are never used twice, so removing a driver or car never hands its files to a new one; its folder stays on the card (delete it by hand). Re-timing a track counts only the sessions of the current driver and car for the PB (a log from before drivers and cars counts as Driver 1 with Car 1).
- **The car's VIN:** give a car its 17-character VIN on the Wi-Fi page and Track Rat picks that car by itself when the Bluetooth OBD-II adapter reports it. Not during a session: it waits for the next. The VIN is only read when there is more than one car (or a car has a VIN), or there are car profiles on the card. The page offers the last VIN seen to save to a car. These cars are not the OBDb car profiles on the SD card (`/obd`), which are matched to the VIN as before.
- **In the session files:** see [Session logs](#session-logs).

## Acceleration and braking

Pick **Drag** on the home screen. Use a flat, straight road or a drag strip.

- **READY / GO:** stop for 1 s (under 1 km/h) and the screen shows READY in green. Pull away and it shows GO, with the elapsed time and distance. Timing starts when the car starts to move, worked out from how fast speed is rising.
- **What's timed:** 0-60 and 0-100 km/h, 0-60 mph, 100-200 km/h, and the 1/8 and 1/4 mile (elapsed time and the speed at the end of each). Times are worked out between GPS fixes, so they're good to a few milliseconds at 10 or 20 Hz. Distance comes from adding up GPS speed.
- **100-200 km/h** is also timed from a rolling start, when you haven't stopped first.
- **Braking:** the distance and time from the moment you slow through 100 km/h to a stop. It works from a rolling start, so you can brake whenever you like.
- **1 FT:** the left button turns on a 1-foot rollout. The clock then starts once the car has moved a foot, as at a drag strip, so times come out about 0.3 s shorter. Distances are still measured from where the car started to move. A tag at the top of the screen shows when it's on.
- **Result cards:** a run ends when you lift (speed drops 10 km/h below its peak), stop, or hold a steady speed for 3 s. Its card then appears, with the times that were reached and `--` for the rest. PREV steps back through the runs, BEST shows the best of each measurement this session, and LIVE goes back to the speed screen. The next launch switches back to live by itself.
- **INVALID:** the GPS dropped fixes during the run (more than 0.4 s apart), so the times can't be trusted. The card says so and the run doesn't count towards the best.
- **NOT LEVEL:** the height changed by 3 m or more, and by 1.5% or more of the distance, so the road wasn't flat. GPS height is only good to a few metres, so gentle slopes aren't flagged. Times are not corrected for slope, and a not-level run still counts towards the best.
- **Units:** the **Speed units** [setting](#settings) (km/h or mph) drives these screens too. Turn on `PERF_ROLLOUT_DEFAULT` in `include/Config.h` to start with the rollout on.
- **Buttons are ignored while moving** (over 5 km/h), so a bump can't end a run. Hold the screen or BOOT to leave.
- **Demo:** with no GPS fix, the left button reads DEMO. It starts a simulated car that stands for 4 s, launches to 215 km/h, brakes to a stop and repeats, so you can try the screens at a desk. The yellow BENCH tag shows while it runs.

Each run is saved to `/sessions/<date>_<time>_perf.json`, rewritten after every run (see [Session logs](#session-logs)).

## Tracks

Tracks are loaded from `/tracks.json`, searched in this order:

1. The SD card
2. Internal flash (`uploadfs`)
3. The built-in list

The easiest way to add your own track is to edit the file on the SD card.

```json
{
  "tracks": [
    {
      "id": "my-track",
      "name": "My Local Track",
      "location": "Town, State",
      "country": "USA",
      "length": 2400,
      "startFinish": { "lat": 40.123456, "lon": -74.123456, "radius": 15, "heading": 90 },
      "splits": [
        { "lat": 40.124500, "lon": -74.120100, "radius": 15, "heading": 180 }
      ]
    }
  ]
}
```

- **`startFinish` and `splits`:** a point on the racing line. A lap or split counts when your path passes through that point, within `radius` metres either side.
- **`heading`:** the direction of travel across the line, in degrees (0 = north, 90 = east, 180 = south). A crossing counts only if you're heading within 90° of it, so a straight alongside the line or the pit lane, running the other way, doesn't end a lap. This matters most on compact kart tracks. Optional: without it, crossings either way count. If the track has a map, the direction is taken from the map near the line instead, and if that turns out backwards (you cross the line the "wrong" way twice in the same place), it's turned round.
- **`radius`:** about half the track width plus a few metres. 10–20 m is typical. Without one, a line is 15 m either side, or 8 m on a kart track.
- **`"kind": "kart"`** (on the venue or a layout): a kart track. Lines with no `radius` are 8 m either side, and the minimum lap is 10 s (the **Min lap time** setting can change it). `tools/fetch_canada_tracks.py` marks karting tracks from OpenStreetMap; the **Kart everywhere** setting treats every track as one.
- **Placement:** put splits where no other part of the track passes within `radius`, or give them a `heading`.
- **`splits`:** optional.
- **`sectors`:** corner sectors, the same form as `splits`, made by the timer from your first clean lap (see [Using it](#using-it)). Each is the gate at the end of one sector: the first ends T1. You can move or delete them by hand; delete them all to have them made again.
- **No start/finish yet:** a track can have a `"center": {"lat": .., "lon": ..}` instead of `startFinish`. It's still found automatically, and the timer asks you to tap at the line the first time.
- **Saved by the timer:** new tracks, start/finish lines marked on track (with their direction), and maps and corner sectors from your first lap are written back into `tracks.json` on the SD card (maps when the session ends). It's saved compactly, one track per line.

### Layouts

A venue with more than one layout lists them in `layouts`. Each has a `name`, and its own `startFinish`, `splits`, `map` and (optionally) `length`; the venue's own `startFinish`, `splits` and `map` are then ignored. Venues without `layouts` work as before.

```json
{
  "id": "my-kart-park",
  "name": "My Kart Park",
  "center": { "lat": 40.123, "lon": -74.123 },
  "layouts": [
    { "name": "Full", "length": 1200,
      "startFinish": { "lat": 40.123456, "lon": -74.123456, "radius": 12, "heading": 90 } },
    { "name": "Reverse", "length": 1200,
      "startFinish": { "lat": 40.123456, "lon": -74.123456, "radius": 12, "heading": 270 } },
    { "name": "Short", "length": 700,
      "startFinish": { "lat": 40.123456, "lon": -74.123456, "radius": 12, "heading": 90 } }
  ]
}
```

- **Records are kept per layout.** Each layout's id is `<venue id>.<layout id>` (here `my-kart-park.full`), used for session file names. A layout's id is its name in lower case with dashes, unless you give it an `"id"`.
- **Telling layouts apart:** the timer needs a `map` for each layout to recognise it from your lap (see Track maps). A Reverse layout with the same map is told apart by the `heading` of its line.
- **Switching mid-session:** if the new layout shares the start/finish line, your laps so far are kept. Otherwise the session so far is saved under the old layout, and timing starts again at the new layout's line, in a new session file.

### All Canadian tracks

`tools/fetch_canada_tracks.py` adds every motorsport track in Canada from OpenStreetMap, each with its name, location and layout map. Run it on your PC; it needs Python 3, no extra packages.

```bash
python tools/fetch_canada_tracks.py
```

Then copy `data/tracks.json` to the SD card.

- **No start/finish lines:** OpenStreetMap doesn't record where the line is, so the timer asks you to mark it on your first visit.
- **Town and province:** each new track's location is written as "Town, ON". The town is a nearby place the track is named after (Shannonville Motorsports Park), else the town the track is in or right next to (within 3 km), else the nearest town or city, else the nearest village. A town from the track's own OpenStreetMap address is kept, and the province comes from OpenStreetMap's reverse geocoder, Nominatim. Nearby towns come from Photon (Komoot's OpenStreetMap geocoder), or Overpass if Photon is down. `--no-locations` skips this.

`tools/fill_track_locations.py` does the same for tracks already in `tracks.json`: those with no province, or every Canadian track with `--all`. `--dry-run` shows the changes without saving. It asks Nominatim once a second, as its usage policy requires, so 120 tracks take about three minutes.
- **What's skipped:** dirt tracks, motocross and drag strips, unless you add `--include-dirt` or `--include-drag`. Karting tracks are included; `--no-karting` leaves them out.
- **Kart tracks:** karting venues (OSM `sport=karting`), and venues whose every raceway is a kart track, get `"kind": "kart"`. A car circuit with a kart track beside it stays a car circuit.
- **Your existing tracks are never changed,** except that a karting track you already have gets `"kind": "kart"` if it has no `kind`. That includes any track within 1 km of one you already have.

> **The pre-loaded track coordinates are approximate.** Check the start/finish point of any track you drive (satellite view in Google Maps works well) before trusting its lap times.

## Battery, power off and sleep

Plug a 3.7 V LiPo into the board's battery socket (1.25 mm, 2 pins) and the unit keeps running when the car's USB power goes off. The board charges it from USB.

- **Battery percentage:** shown in the top bar next to the GPS bars. It turns yellow at 30% and red at 15%. With no battery fitted, nothing is shown.
- **It's an estimate.** The percentage comes from the battery voltage, which reads high while charging and dips under load.
- **How it's measured:** the board feeds half the battery voltage to IO9 (two 200 k resistors, from the board's schematic). The firmware averages 16 readings a second and smooths them. If the figure is off from a multimeter, set `BATTERY_CAL_OFFSET_MV`.
- **No battery fitted:** a reading under 2.5 V, or over 4.23 V (the charger holds an empty socket at about 4.2 V), for 5 s in a row counts as "no battery". If a missing battery still shows a percentage, or a full one disappears, adjust `BATTERY_PRESENT_MAX_MV`. The serial log prints the readings every 10 s.
- **Low battery:** at 3.30 V or below for 10 s, Track Rat saves the session, shows **BATTERY LOW** for 5 s, then goes to sleep. Charge it, then touch the screen or press BOOT.

**Power off:** hold BOOT for 3 s on any screen, or use **Settings → Power off** (it asks first). From 2 s a **POWER OFF** bar fills; let go before it's full and nothing happens. Nothing else happens on the way either: BOOT's 1 s hold only acts when it's let go before 2 s. **POWER OFF** shows while it saves whatever is running, the same as ending it. A DJI camera recording with the session stops too. Then the screen goes dark: let go of BOOT.

**Turning it back on:** tap the screen, like a phone's tap to wake, or press BOOT. It starts up as it does after auto-sleep: the logo, then the GPS, which has its fix a second or so later.

- **It's a deep sleep, not a cut:** nothing on the board can disconnect the battery, so off is the same deep sleep as auto-sleep, straight away. The touch controller keeps feeling for a finger, which uses a little current (not measured yet), and anything that touches the screen turns it on. Plugging in USB doesn't.
- **The GPS stays in standby** (see [GPS data](hardware.md#gps-data)), so it has a fix a second or so after turning on, or a few seconds after a day or two off. Unplugging the battery loses that.
- **Not while firmware installs:** holding BOOT during an update over Wi-Fi does nothing.
- **Still held when the screen goes dark:** it waits up to 10 s for BOOT, or a finger on the screen, to be let go. Held longer, it looks again every 10 s and stays off.

**Auto-sleep:** parked with no touch and no BOOT press for 20 minutes, the backlight goes off. This is a full off, whatever the Brightness setting; the chosen level comes back when the screen wakes. A minute later the board goes into deep sleep. Moving at 5 km/h or more counts as activity, so it never sleeps during a session.

- **Waking:** touch the screen or press BOOT. From backlight-off, the press only turns the screen on. From deep sleep, the board restarts as if just powered on, so it finds the GPS again (out of standby).
- **Saving:** a session in progress is saved before sleeping, the same as ending it.
- **Current asleep:** the GPS stays powered while the board sleeps (on battery its 5 V pin is live), so it goes into standby. Before, it kept tracking at about 20 mA, enough to empty a 3000 mAh battery in a week or two. What's left is the board's own draw (the regulator, the battery divider, the screen, the SD card): not measured yet. A multimeter in series with the battery, USB unplugged, would say.
- **Turning it off:** set `AUTO_SLEEP_ENABLED` to `0` in `include/Config.h`.

## Wi-Fi page

Get your sessions onto your phone without taking the SD card out.

1. In the pits, tap **Wi-Fi** on the home screen or **WI-FI** on a session summary (or hold BOOT on a summary). The screen shows the network name, the address and an upload code.
2. On your phone, join the Wi-Fi network **TrackRat-XXXX**. There's no password. The last 4 characters are different on each board.
3. The page opens by itself, like a hotel Wi-Fi login. If it doesn't, open **http://192.168.4.1** in your browser.
4. When you're done, tap the Track Rat screen (or press BOOT) to turn Wi-Fi off.

What the page does:

- **Sessions:** every file in `/sessions`, by day and track, newest first. Tap a file to download it. Files from the same session (`.json`, `.vbo` and so on) are shown together. Times are in your phone's time zone. Each session has its logbook note under it: see [Logbook](#logbook) below.
- **Tracks:** download the `tracks.json` Track Rat is using, or upload an edited one. The old one is kept as `tracks.json.bak`, and Track Rat restarts to load the new list. If power is cut mid-save (here or on the device) and `tracks.json` is gone, the next boot puts back the newest complete copy it finds (`.tmp`, `.upload`, then `.bak`).
- **Track editor** (on the Tracks tab): see [Track editor and re-timing](#track-editor-and-re-timing) below.
- **Compare:** any two laps at a track, from any sessions: speed against distance, the time gap against distance, and the map coloured by who was faster where. Plus the season: your best lap at each visit and your PB over time. See [Lap comparison](#lap-comparison) below.
- **Firmware:** install a new `firmware.bin` (from `.pio/build/es3c28p/` after `pio run`). The page and Track Rat both check the file first, as the SD card update does: it must be an ESP32-S3 app image that fits the slot, so a `bootloader.bin` or a file for another chip is refused before anything is written. A progress bar shows while it's sent; then Track Rat restarts with Wi-Fi off. Keep it powered until it does.
- **Device:** firmware version, free space on the card and in flash, GPS status and memory.
- **Settings:** the same items as the Settings screen on Track Rat; see [Settings](#settings). Changing one asks for the upload code and takes effect at once, except the GPS mode, which waits for a restart (the page offers one). The time zone is shown but not changed here.

Each time the page opens it tells Track Rat your phone's time zone, which becomes the **Time zone** setting. From then on, session files are named in your local time instead of UTC. Daylight saving time changes the zone, so it's correct again the next time a phone opens the page. You can also set it on the Settings screen, but the next phone to open the page replaces it.

Good to know:

- **Wi-Fi is off while timing.** It's only on while the Wi-Fi screen shows, and starting a session turns it off. Bluetooth (RaceBox) keeps working while it's on.
- **Downloads in the sign-in window:** the window phones open by themselves often can't save files. If a download doesn't start, open http://192.168.4.1 in Safari or Chrome while still on the TrackRat network.
- **Mobile data:** some phones keep using mobile data for everything when a Wi-Fi network has no internet. If the page won't load, tell the phone to stay connected or turn mobile data off.
- **Upload code:** installing firmware, replacing `tracks.json` (or saving the track editor's lines), re-timing, saving a note or changing a setting asks for the 4-digit code on Track Rat's Wi-Fi screen, so only someone who can see the screen can do it. After 5 wrong codes, uploads are locked until Wi-Fi is turned off and on again (which also gives a new code). Downloads don't need it.
- **Anyone nearby can join** and download sessions while it's on, so turn it off when you're done.

### The viewer on the website

The same page also runs at [trackrat.ca/analyze](https://trackrat.ca/analyze/), with no Track Rat connected. Open a session's files from your computer (the `.vbo` log and its `.json` summary, and its `_note.json` if it has one), by the **Open files** button or by dropping them anywhere on the page. Files with the same name before the extension are one session. Nothing is uploaded: the page reads them in your browser. The Sessions tab (with CSV for overlay tools) and the Compare tab work on them. The tabs that need the board (Tracks, Firmware, Device, Settings) and the note editor are hidden. **Try a sample session** opens a simulated drive at Calabogie, made by `tools/make_sample_session.py`. The page can also be tried from your own copy of the repository: serve `web/` with any web server and add `?viewer` to its address.

### Track editor and re-timing

Record first, decide later: a wrong start/finish, a missing split or a layout Track Rat didn't know never costs a session, because every fix is in the log.

1. On the **Tracks** tab, pick a session (any `.vbo` log on the SD card). Its laps are drawn from the log on your phone, no map download needed: every lap in grey, one in full, with the track's start/finish (green, with an arrow the way the track runs), splits (blue, S1, S2...) and corners (purple, T1, T2... or their names) on top. The track is the one the log names; pick another from the list if it was timed at the wrong one.
2. Edit:
   - **Move line:** tap the path where the start/finish should be. Its direction is taken from the path there. The corner sectors go with the old line, as with SET LINE on the device: Track Rat makes them again from the next clean lap.
   - **Set finish** (stages only): tap the path where the finish is. Its direction is taken from the path there.
   - **Flip direction:** turns the line round.
   - **Add split / Remove split:** tap the path to add one (numbered in the order a lap passes them, from the line), or tap a split to remove it.
   - **Corners:** type a name for any corner sector ("The Wall"); clear it to go back to the number. Names are saved in `tracks.json` (`"name"` on each of `"sectors"`).
   - **Nudge a line:** pick the start/finish, the finish or a split and press ◀ or ▶ to slide it 0.5 m earlier or later along the direction of travel (about 20 ms at 90 km/h), which is the way to fix a line tapped a little late or early while driving. It shows how far you have moved it (**Reset** puts it back) and, for the session's runs or laps, each time with the saved lines and with the moved line, timed again from the log on every press. **Add the day's other runs** brings in the day's other logs at the track. A standing start is timed from moving off, so moving the start leaves it alone unless you move the line more than a metre ahead of where the car stood. A line with no direction moves along the path where it crosses. Then save and re-time as below.
   - **New layout:** makes a layout of the venue (for example a short course), starting from this track's line, splits and map. A venue with no layouts keeps what it had as a layout called **Main**, as `tools/fetch_track_map.py` does.
3. **Save lines:** asks for the upload code and sends the edited `tracks.json`, the same way as uploading one: the old file is kept as `tracks.json.bak` and Track Rat restarts to load it.
4. Turn Wi-Fi on again and open the editor: **Re-time** times **this session** or **every session at this track** again from their logs, with the lines Track Rat has now. It runs in the background on the device (the page shows how far it is, and the Wi-Fi screen stays on until it's done), rewrites each session's summary (laps, splits, sectors, tags), and puts the PB and the best of the day right: the quickest clean lap replaces a stored one that was timed at another line, or that it beats. A session needs its `.vbo` log on the SD card to be re-timed.

### Lap comparison

On the **Compare** tab, pick a track, then lap **A** (blue) and lap **B** (orange): each from any session at that track with a `.vbo` log on the SD card. The lap lists come from the session summaries, with each lap's time and tags; the best lap is picked to start with. Your phone draws everything from the logs (Track Rat only serves the files):

- **Speed against distance:** both laps on one chart, so you see where one brakes later or carries more speed.
- **Time gap against distance:** B's time minus A's at the same point on track. Going up means B is losing time there. B's lap is stretched to A's length, as two laps never measure quite the same distance, so the same corner lines up. The last point is the gap at the line.
- **Who was faster where:** lap A's path, each stretch blue where A was gaining and orange where B was.
- **Channels:** under the gap chart, a chip for each channel both laps have: RPM, throttle, coolant, intake and oil temperatures, lateral and longitudinal g, and any channels from your car profile (brake pressure, steering angle). Tap one to draw it against distance, A and B on one chart, with its unit. A temperature of 0 means the car didn't report it, so it shows as a gap and a chip is left out if the lap never had one.
- **Friction circle:** lateral g across, acceleration up and braking down, a dot for each fix of A and B, with rings at 0.5 g and 1.0 g and each lap's biggest combined g. It only shows when the log has g.
- **Drives without laps:** a Free Roam drive, a performance run or any log with no lap crossings can be compared too. It shows as one item, **Whole drive** (or **Whole log**), from the first fix to the last, measured by distance. Pick Free Roam drives under **Free Roam drives** in the track list. It works with every chart above, on the board and at trackrat.ca/analyze, and can be compared with a lap or another drive (the other one is stretched to the same length).
- **Corner sectors:** a row for each corner with A's time, B's time and the difference (coloured for whoever was quicker), and the quickest of each corner in the two sessions marked **best**. Corners are named as in the [track editor](#track-editor-and-re-timing), else T1, T2... It only shows when both laps have sectors, which Track Rat makes after the first clean lap at a track, so older sessions and the sample session don't have them.
- **Cursor:** move the mouse over, or touch and drag across, any of the distance charts. A line shows that distance on all of them, a dot for each lap shows where it was on the map and on the circle, and the bar at the top gives both laps' speed, the gap and the picked channel there.
- **Season:** the best lap of each visit (day) at the track, newest first, with your PB as it stood then, and a chart of both over time.

Laps are cut where they were cut live (the log's Lap column). After [re-timing](#track-editor-and-re-timing) with a moved line, the lap list follows the new summary but the traces still follow the old cuts.

### Logbook

Keep a note with each session: tyre pressures, the weather, setup changes, what your instructor said.

1. On the **Sessions** tab, tap **Add note** under a session (or **Edit note** to change one).
2. Type it and tap **Save note**. It asks for the upload code. Up to 1000 bytes (about 1000 letters, fewer with accents or emoji).
3. To delete a note, clear it and save.

The note is saved next to the session as `<date>_<time>_<track id>_note.json` (`{"note": "..."}`), on the SD card or in flash, wherever the session is. When you download the session's `.vbo` from the page, the note is in its `[comments]` as `Note: ...` lines, so Circuit Tools shows it too. The file on the card is not changed, so a `.vbo` copied straight off the card has no note.

### Changing the page

The page is a single file, `web/index.html` (plain HTML and JavaScript, no libraries), plus its fonts in `web/fonts/`. `tools/build_web.py` gzips the page into `data/web/index.html.gz` and copies the fonts to `data/web/fonts/` every time PlatformIO runs; it also puts the git version on the Device tab. To put a new page on the board:

```bash
pio run -e es3c28p -t uploadfs
```

Without it, the board shows a plain page with the session list, `tracks.json` and firmware upload. There is no Settings tab then; use the Settings screen on the board.

## Session logs

Sessions are saved in `/sessions` on the SD card, as two files with the same name. For a venue with layouts, the track id includes the layout (`calabogie.full`) and the summary also names the layout.

- **`<date>_<time>_<track id>.vbo`:** every GPS fix, up to 20 a second (10 in the 10 Hz GPS mode): UTC time, position, speed, heading, height, satellites, HDOP (GPS precision), lap number, lateral and longitudinal g worked out from the GPS (+ = turning right, + = speeding up), and a lap tag mark. About 6.8 MB per hour of driving. The **Lap tag** channel marks the fixes that tag a lap where they happened: 1 = stopped, 4 = after a gap of more than 1 s, 8 = accuracy worse than ±5 m (added together when there are several). The header names the session number (`Session: 2`) and, in `[comments]`, `Driver: <name>` and `Car: <name>` (and `Driver id`, `Car id`). With an OBDb car profile in use, a `Car data:` line names that profile, ending `(OBDb car profile)`.
- **`<date>_<time>_<track id>.json`:** the summary: the session number (`"session"`), every lap and split time, each lap's corner sector times (`"sectorsMs"`: the time into the lap at the end of T1, T2... the last sector ends at the line), max and average speed and distance per lap, the hottest coolant and oil temperature per lap and for the session (`"coolantMaxC"`, `"oilMaxC"`, °C, only with engine data), each lap's tags (`"tags": ["pit", "slow", "suspect"]`, and for a suspect lap why: `"suspect": ["gps gap", "accuracy", "distance"]`), the best clean lap, the average of the untagged laps, and the name of its `.vbo`. The summaries of laps, stages, Free Roam and Performance name who drove: `"driver"` and `"car"` (their names then) and `"driverId"` and `"carId"` (see [Drivers and cars](#drivers-and-cars)); a session from before drivers and cars has none of them. With a paired DJI camera, `"cameraStartUtc"` is the GPS time the camera started recording (`"cameraStartsUtc"` lists every start: one video per time out of the pits).

File names use UTC until the **Time zone** setting is set, either on the Settings screen or by a phone opening the Wi-Fi page. Then they use your local time. Times inside the `.vbo` are always UTC. The files' dates on the card (Date modified on a PC) are set from the GPS too, in the same local time, once the GPS has a fix with a date. Files saved before that, or by older firmware, show 1980 (or 1979 on a PC west of UTC). Speeds in the files are always km/h, whatever the Speed units setting.

Free Roam drives are saved the same way, as `<date>_<time>_freeroam.vbo` and `.json`, unless **Record road drives** is off in [Settings](#settings): then nothing is saved. The summary holds the duration, distance, and top and average speed.

**Best laps** are kept in `/tracks/<track id>/` on the card (in `/tracks/<track id>/d3-c2/` for other drivers and cars): `best.bin` (the PB) and `day.bin` (the best of its day), each about 30 KB: the lap's path (position, time and speed every fix), its time, its date and where the start/finish was. They're saved the same safe way as the summary.

**Without an SD card,** only the summaries and best laps are kept, in flash (a full log fills it in about half an hour). Free Roam then saves its route as GPS points about 10 m apart in the summary. The log at boot says when there's no card.

### Opening a session in Circuit Tools

[Circuit Tools](https://vboxmotorsport.co.uk/us/circuit-tools) is Racelogic's free analysis software for Windows, macOS and iOS. It shows lap overlays and speed and time-gap graphs.

1. Copy the `.vbo` from the card's `sessions` folder to your computer.
2. Open it in Circuit Tools.
3. **If the track has a map** in `tracks.json`, the file includes the start/finish line and any splits, so the laps appear straight away. Corner sectors come after the splits, named T1, T2... **Otherwise** (new tracks, a line you marked during the session), set the start/finish line in Circuit Tools. Everything you drove is in the file either way.

### If the power goes off

The ignition can cut the power at any moment, so nothing needs to be ended to be kept:

- **The log** is written about once a second, so a power cut loses at most the last second.
- **The summary** is rewritten after every lap without ever being half written: the new copy goes to a temporary file, which then replaces the old one.
- **At the next boot,** any session that wasn't ended gets its summary rebuilt from its log, by timing the log again with the same lap timer and the same lines (the log records each line's direction too). A session that switched to another layout at the same start/finish is timed with that layout's lines and kept under its name, as its summary was. The log at boot shows `Session: recovered ...`.

Performance runs are saved as `/sessions/<date>_<time>_perf.json`, with every run's times (seconds), trap speeds (km/h), braking distance (metres), peak speed, height change, and whether it was invalid or not level, plus the session's best. It's rewritten after every run the same safe way as the summary.
