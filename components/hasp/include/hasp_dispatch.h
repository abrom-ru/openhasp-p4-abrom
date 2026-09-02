/* MIT License - Copyright (c) 2019-2024 Francis Van Roie
   For full license information read the LICENSE file in the project folder */
/* Ported to p4-abrom (step 3d MVP): mirrors src/hasp/hasp_dispatch.h subset.
 * MVP scope (see memory/project_hasp_step3d_plan.md):
 *   - page N | page next | page prev | page back
 *   - clearpage N | clearpage | clearpage all
 *   - p<page>b<id>.<attr>=<value>
 *   - {...} jsonl / [...] json array
 * Deferred (marked in the plan doc): MQTT topic prefixes, config, moodlight,
 * screenshot, statusupdate, sensors, reboot, factory_reset, calibrate, sleep,
 * antiburn, wakeup, run_script, fs, shell_execute, theme, service, discovery.
 */

#ifndef HASP_DISPATCH_H
#define HASP_DISPATCH_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Single entry point. `line` is one text command:
 *   - "page 2", "page next"
 *   - "clearpage 1", "clearpage all"
 *   - "p1b2.text=Hello", "p1b5.val=42"
 *   - "{...}" jsonl object
 *   - "[...]" json array of commands
 *   - "// comment" or empty → ignored
 * Caller must hold the LVGL lock.
 */
void hasp_dispatch_command(const char* line);

/* Page dispatch subroutines — direct callers can use these to skip parsing. */
void hasp_dispatch_page(const char* payload);       /* "2" | "next" | "prev" | "back" | "" */
void hasp_dispatch_clear_page(const char* payload); /* "N" | "all" | "" */

/* Publish "<subtopic>" state payload to MQTT. Step 4b — mirrors S3
 * src/hasp/hasp_dispatch.cpp:64 dispatch_state_subtopic(). Currently forwards
 * to hasp_mqtt_publish_state; when 4c adds group/broadcast this fans out
 * without touching event handlers or object code. Log-traces on success. */
void dispatch_state_subtopic(const char* subtopic, const char* payload);

#ifdef __cplusplus
}
#endif

#endif // HASP_DISPATCH_H
