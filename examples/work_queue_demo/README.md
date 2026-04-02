# Work Queue Demo

Demonstrates Boreas kernel primitives on any ESP32 dev kit (no special hardware needed).

## What it shows

1. **Periodic timer** -- `k_timer` firing every 1 second with expiry and stop callbacks
2. **Work with context** -- `k_work` using `CONTAINER_OF` to access a parent struct
3. **Delayed work** -- `k_work_delayable` scheduled to fire after 2 seconds
4. **Reschedulable work** -- `k_work_reschedule` to change the deadline while pending

## Build and run

```bash
cd examples/work_queue_demo
source ../../.env  # or your IDF setup
idf.py set-target esp32s3
idf.py -p /dev/cu.usbmodem2101 flash monitor
```

## Expected output

```
[Timer] Heartbeat #1
[Work] Processing sensor 7, reading=2350
[Delayed] This ran 2 seconds after scheduling
[Timer] Heartbeat #2
[Timer] Heartbeat #3
[Reschedule] Finally fired after 1 reschedules
[Timer] Heartbeat #4
[Timer] Heartbeat #5
[Timer] Heartbeat stopped after 5 beats
=== Demo complete ===
```
