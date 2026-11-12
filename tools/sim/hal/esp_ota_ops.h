// Stand-in (tools/build_sim.py): there is no firmware to roll back.
#pragma once
inline int esp_ota_mark_app_valid_cancel_rollback() { return 0; }
