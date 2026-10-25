/**
 * Track Rat - reading the day's logs for the Adjust start / Adjust finish screen (9.3b)
 *
 * start() lists /sessions for the course's day, then poll() reads the logs a little at a time
 * (a few KB per call, never more than about 10 ms of one loop pass), feeding AdjustLine.h's
 * DayScan, so the screen keeps drawing and answering touches while the card is read. Only the
 * fixes near the line are kept (at most adjust::MAX_RUNS runs, each a few KB: PSRAM takes the
 * larger blocks). Nothing is read in the background any more once the model is ready; release()
 * hands the memory back when the screen is left.
 *
 * Not used while a run is being timed: the Adjust screen is reached from the Autocross hub only.
 */

#ifndef ADJUST_LOAD_H
#define ADJUST_LOAD_H

#include <Arduino.h>
#include "AdjustLine.h"
#include "TrackDatabase.h"

namespace AdjustLoad {

// Begin reading the day's runs of `course` for moving its start or finish. Without a card or logs
// the load ends at once with failed() set.
void start(const Track& course, adjust::Which which);
void poll();                // a little more of the work; cheap when there is none
bool reading();             // not finished yet
bool failed();              // nothing to adjust: no card, no logs, or no timed runs in them
const String& failure();    // why, for the screen
int logsDone();
int logsTotal();            // 0 while the card is still being listed
adjust::Model& model();     // when !reading() && !failed()
void release();             // free everything

}  // namespace AdjustLoad

#endif  // ADJUST_LOAD_H
