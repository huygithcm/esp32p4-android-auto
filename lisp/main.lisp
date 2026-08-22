(def cruise-active 0)
(def cruise-rpm 0)
(def rx-button-state 1)
(def tx-button-state 1)
; Motor arbiter state (motor-control-loop). setq'd at runtime → above @const.
(def out-rel 0.0)     ; throttle slew-limiter state (relative current 0..1)
(def brk-rel 0.0)     ; brake slew-limiter state (relative brake current 0..1)
(def cruise-i 0.0)    ; cruise PI integrator (A), seeded on activation (bumpless)
(def armed 0)         ; safe-start: throttle must be seen released once after boot
; Cruise PI gains are HARDCODED here (edit + re-upload to tune): the firmware
; speed-PID gains (s-pid-kp/ki in VESC Tool) are NOT exposed to LISP conf-get,
; so there is nothing to read them from. Ramping times, by contrast, ARE read
; live from the VESC Tool ADC app page — see throttle-out/brake-out.
(def cruise-kp 0.02)  ; cruise PI: A per ERPM of error
(def cruise-ki 0.05)  ; cruise PI: A/s per ERPM of error
; Throttle feel knobs. ctl-dt is the arbiter tick — 100 Hz like Vedder's
; vl_bike pkg: at 20 Hz the ramp advanced in 12.5%-of-max current steps, which
; the FOC loop executes instantly → felt like jerks, not a ramp.
(def ctl-dt 0.01)          ; arbiter period (s)
(def thr-curve-accel 0.0)  ; throttle-curve accel const, -1..1 (0 = linear)
(def thr-curve-mode 0)     ; 0 exponential, 1 natural, 2 polynomial
(def current-profile 0)
(def num-profiles 3)
(def first-profile-init 1)
(def rpm-per-ms 0.0)
(def throttle-on 1)
(def tc-on 0)
(def tc-sens 50.0)
(def pbuf (bufcreate 128))
(def pi 0)
(def beep-vol-addr 0)
(def beep-vol (let ((v (eeprom-read-i beep-vol-addr)))
                (if (and v (>= v 0) (<= v 50)) v 30)))
(def beep-vol-dirty 0)
; Pedal-assist setpoint from the head unit / BLE helper (over
; COMM_CUSTOM_APP_DATA, msg 0x05). pas-amps is the requested motor current
; (A); pas-seen is the (systime) of the last accepted frame, for a staleness
; check; pas-src is the CAN id of the sender we are locked onto (-1 = none) —
; see the 0x05 handler. setq'd at runtime → MUST stay above @const.
(def pas-amps 0.0)
(def pas-seen 0)
(def pas-src -1)
; ---- ride-mode config (see docs/RIDE_MODE_REVERSE_BE_CONTRACT.md) ----------
; Speeds are km/h x 10, current scale is per-mille of Motor Current Max, so the
; wire, the EEPROM and the screen all hold the same integers and a number the
; rider typed cannot come back a rounding step away.
; Defaults reproduce the previous hard-coded behaviour exactly.
(def rm-s0 50)  (def rm-c0 300)
(def rm-s1 100) (def rm-c1 600)
(def rm-s2 200) (def rm-c2 1000)
(def rm-rev-en 0)
(def rm-rev-speed 30)
(def rm-rev-cur 70)
(def rm-revision 0)
(def rm-persist 0)     ; EEPROM still owes a write
(def rm-fault 0)       ; result code of the last refusal, 0 = none
; Vehicle calibration. THE speed ceiling: the P4 range check is deliberately
; permissive so this stays the single limit to re-derive when the wheel or
; motor changes.
(def rm-ceiling 200)   ; 20.0 km/h
; ---- reverse runtime ------------------------------------------------------
(def rv-dir 1)         ; 1 forward, 0 interlock/coast, -1 reverse
(def rv-btn 0)         ; debounced, 1 = pressed
(def rv-btn-raw 0)
(def rv-btn-count 0)
(def rv-armed 0)
(def rv-rel 0.0)       ; reverse slew state, mirrors out-rel
(def rv-brake-ticks 0)
; Safe start, the reverse twin of `armed`: a button shorted to GND since boot
; must not arm reverse. Nothing happens until it has been seen released once.
(def rv-seen-release 0)
; Set by the monitor thread only after gpio-configure has returned. If the
; target does not expose pin-ppm the thread dies there and this stays 0, which
; is what makes reverse report UNSUPPORTED_HARDWARE instead of guessing a pin.
(def rv-hw-ok 0)
; @const-start flashes every definition below, freeing the cons heap. Without it
; all the defun bodies live in RAM and exhaust the heap — panel-event-loop then
; OOMs at runtime and the display goes blank while motor control keeps running.
; Everything mutable MUST stay above this line: setq'd scalars, and especially
; the pbuf buffer (a flashed buffer is read-only, bufset would fail/crash).
@const-start
(defun play-stop () {
    (sleep 0.1)
    (foc-play-stop)
})
; Profiles scale the current limit instead of overwriting it: Motor Current Max
; in VESC Tool stays the master value (applied live, no LISP restart) and each
; profile is a fraction of it. Braking is never scaled — always full.
(defun rm-speed-of (i) (if (= i 0) rm-s0 (if (= i 1) rm-s1 rm-s2)))
(defun rm-cur-of   (i) (if (= i 0) rm-c0 (if (= i 1) rm-c1 rm-c2)))
; max-speed and min-speed take m/s; the firmware stores min-speed as a negative
; erpm limit itself. Speed limiting stays a soft current taper, not a speed PID:
; it will not brake the bike downhill, and that is deliberate.
(defun apply-profile (profile-index) {
    (let ((sp (rm-speed-of profile-index))
          (cu (rm-cur-of profile-index))) {
        (conf-set 'max-speed (/ (/ sp 10.0) 3.6))
        (conf-set 'l-current-max-scale (/ cu 1000.0))
        (print (str-merge "Profile " (to-str profile-index) ": "
                          (to-str (/ sp 10.0)) " km/h, "
                          (to-str (/ cu 10.0)) "% current"))
    })
    (if (= first-profile-init 0) {
        (let ((beep-freq (if (= profile-index 0) {
            500
        } {
            (if (= profile-index 1) {
                750
            } {
                1000
            })
        }))) {
            (foc-play-tone 0 beep-freq 10)
            (spawn 150 play-stop)
        })
    } {
        (setq first-profile-init 0)
    })
})
; ---- ride config persistence ----------------------------------------------
; Address 0 belongs to the beep volume; this block starts at 16. Magic is
; written LAST so a write cut short by a power loss reads back as absent rather
; than as a half-updated config.
(def rm-ee-magic 16)
(def rm-ee-rev   17)
(def rm-ee-sum   18)
(def rm-ee-base  20)          ; 20..28, in the order below
(def rm-ee-tag   0x524D01)    ; 'RM' + format version 1

(defun rm-checksum ()
    (mod (+ rm-s0 rm-c0 rm-s1 rm-c1 rm-s2 rm-c2
            rm-rev-en rm-rev-speed rm-rev-cur) 65536))

; Bounds are re-checked on the way IN as well as on the way out: EEPROM can be
; stale from an older format or simply corrupt, and a config that passed
; validation when it was written is not automatically valid now.
(defun rm-values-ok (s0 c0 s1 c1 s2 c2 re rs rc)
    (and (>= s0 10) (<= s0 rm-ceiling) (>= c0 100) (<= c0 1000)
         (>= s1 10) (<= s1 rm-ceiling) (>= c1 100) (<= c1 1000)
         (>= s2 10) (<= s2 rm-ceiling) (>= c2 100) (<= c2 1000)
         (<= s0 s1) (<= s1 s2)
         (or (= re 0) (= re 1))
         (>= rs 10) (<= rs 50)
         (>= rc 10) (<= rc 140)))

; All-or-nothing: a single bad field means the whole stored block is ignored
; and the defaults stand. Mixing a trusted default with an untrusted stored
; value is how a config nobody ever chose ends up driving the motor.
; min-speed is the motor's negative speed limit. Only written when reverse is
; actually enabled: with it off the value is unreachable, and mutating motor
; config nobody asked about is how a setting drifts away from what VESC Tool
; shows. Magnitude in m/s; the firmware stores the negative erpm itself.
(defun rm-apply-reverse-speed ()
    (if (= rm-rev-en 1)
        (conf-set 'min-speed (/ (/ rm-rev-speed 10.0) 3.6))))
(defun rm-load () {
    (if (= (eeprom-read-i rm-ee-magic) rm-ee-tag) {
        (let ((s0 (eeprom-read-i (+ rm-ee-base 0)))
              (c0 (eeprom-read-i (+ rm-ee-base 1)))
              (s1 (eeprom-read-i (+ rm-ee-base 2)))
              (c1 (eeprom-read-i (+ rm-ee-base 3)))
              (s2 (eeprom-read-i (+ rm-ee-base 4)))
              (c2 (eeprom-read-i (+ rm-ee-base 5)))
              (re (eeprom-read-i (+ rm-ee-base 6)))
              (rs (eeprom-read-i (+ rm-ee-base 7)))
              (rc (eeprom-read-i (+ rm-ee-base 8)))) {
            (if (and (= (eeprom-read-i rm-ee-sum)
                        (mod (+ s0 c0 s1 c1 s2 c2 re rs rc) 65536))
                     (rm-values-ok s0 c0 s1 c1 s2 c2 re rs rc)) {
                (setq rm-s0 s0) (setq rm-c0 c0)
                (setq rm-s1 s1) (setq rm-c1 c1)
                (setq rm-s2 s2) (setq rm-c2 c2)
                (setq rm-rev-en re)
                (setq rm-rev-speed rs)
                (setq rm-rev-cur rc)
                (setq rm-revision (eeprom-read-i rm-ee-rev))
                (print "ride config: loaded from eeprom")
            } (print "ride config: eeprom rejected, using defaults"))
        })
    } (print "ride config: no eeprom block, using defaults"))
})

; Magic last, for the reason above. Runs off the slow persist thread, never
; from the packet handler: a flash write inside the motor loop's period is how
; a control tick gets missed.
(defun rm-store () {
    (eeprom-store-i rm-ee-magic 0)
    (eeprom-store-i (+ rm-ee-base 0) rm-s0)
    (eeprom-store-i (+ rm-ee-base 1) rm-c0)
    (eeprom-store-i (+ rm-ee-base 2) rm-s1)
    (eeprom-store-i (+ rm-ee-base 3) rm-c1)
    (eeprom-store-i (+ rm-ee-base 4) rm-s2)
    (eeprom-store-i (+ rm-ee-base 5) rm-c2)
    (eeprom-store-i (+ rm-ee-base 6) rm-rev-en)
    (eeprom-store-i (+ rm-ee-base 7) rm-rev-speed)
    (eeprom-store-i (+ rm-ee-base 8) rm-rev-cur)
    (eeprom-store-i rm-ee-sum (rm-checksum))
    (eeprom-store-i rm-ee-rev rm-revision)
    (eeprom-store-i rm-ee-magic rm-ee-tag)
    (setq rm-persist 0)
})
(gpio-configure 'pin-rx 'pin-mode-in-pu)
(gpio-configure 'pin-tx 'pin-mode-in-pu)
(defun update-rpm-per-ms () {
    (loopwhile t {
        (if (= cruise-active 0) {
            (let ((current-rpm (get-rpm))) {
                (let ((current-speed-ms (get-speed))) {
                    (if (and (> (abs current-rpm) 10) (> (abs current-speed-ms) 0.1)) {
                        (setq rpm-per-ms (/ (abs current-rpm) (abs current-speed-ms)))
                    })
                })
            })
        })
        (sleep 0.2)
    })
})
; Cruise is a PI speed controller with a CURRENT output inside the motor
; arbiter — not the firmware speed PID. No set-rpm mode switch, so engaging
; can't jerk: the integrator is seeded with the actual motor current and the
; loop keeps commanding current smoothly. (De)activation just flips state; the
; arbiter (motor-control-loop) does everything else.
(defun activate-cruise-control () {
    (if (and (= cruise-active 0) (= throttle-on 1)) {
        (setq cruise-rpm (get-rpm))
        (if (> (abs cruise-rpm) 0) {
            (setq cruise-i (get-current))   ; bumpless transfer
            (setq cruise-active 1)
            (print (str-merge "Cruise control activated at RPM: " (to-str cruise-rpm)))
        } {
            (print "Cannot activate cruise control: speed is zero")
        })
    })
})
(defun deactivate-cruise-control () {
    (if (= cruise-active 1) {
        (setq cruise-active 0)
        (setq cruise-rpm 0)
        (setq rpm-per-ms 0.0)
        (print "Cruise control deactivated")
    })
})
(defun increase-cruise-speed () {
    (if (= cruise-active 1) {
        (if (> rpm-per-ms 0.0) {
            (let ((current-speed-ms (/ (abs cruise-rpm) rpm-per-ms))) {
                (let ((new-speed-ms (+ current-speed-ms (/ 1.0 3.6)))) {
                    (let ((new-rpm (* new-speed-ms rpm-per-ms))) {
                        (if (< cruise-rpm 0) {
                            (setq cruise-rpm (- new-rpm))
                        } {
                            (setq cruise-rpm new-rpm)
                        })
                        (print (str-merge "Cruise speed increased to RPM: " (to-str cruise-rpm)))
                    })
                })
            })
        } {
            (let ((rpm-increment 50)) {
                (if (< cruise-rpm 0) {
                    (setq cruise-rpm (- cruise-rpm rpm-increment))
                } {
                    (setq cruise-rpm (+ cruise-rpm rpm-increment))
                })
                (print (str-merge "Cruise speed increased to RPM: " (to-str cruise-rpm)))
            })
        })
    })
})
(defun monitor-rx-button () {
    (loopwhile t {
        (let ((current-button-state (gpio-read 'pin-rx))) {
            (if (and (= rx-button-state 1) (= current-button-state 0)) {
                (if (= cruise-active 1) {
                    (increase-cruise-speed)
                } {
                    (activate-cruise-control)
                })
            })
            (setq rx-button-state current-button-state)
        })
        (sleep 0.05)
    })
})
(defun switch-profile () {
    (setq current-profile (+ current-profile 1))
    (if (>= current-profile num-profiles) {
        (setq current-profile 0)
    })
    (apply-profile current-profile)
})
; Direct profile selection — what the on-screen panel buttons call (the TX pin
; button still cycles through them with switch-profile). Selecting the profile
; that is already active is a no-op: the panel's toggles are a radio group, so
; tapping the lit one just gets re-lit by the STATE echo instead of toggling off.
(defun panel-set-profile (idx) {
    (if (and (>= idx 0) (< idx num-profiles) (not (= idx current-profile))) {
        (setq current-profile idx)
        (apply-profile current-profile)
    })
})
(defun decrease-cruise-speed () {
    (if (= cruise-active 1) {
        (if (> rpm-per-ms 0.0) {
            (let ((current-speed-ms (/ (abs cruise-rpm) rpm-per-ms))) {
                (let ((new-speed-ms (- current-speed-ms (/ 1.0 3.6)))) {
                    (if (> new-speed-ms 0.1) {
                        (let ((new-rpm (* new-speed-ms rpm-per-ms))) {
                            (if (< cruise-rpm 0) {
                                (setq cruise-rpm (- new-rpm))
                            } {
                                (setq cruise-rpm new-rpm)
                            })
                            (print (str-merge "Cruise speed decreased to RPM: " (to-str cruise-rpm)))
                        })
                    } {
                        (deactivate-cruise-control)
                        (print "Cruise control deactivated: speed too low")
                    })
                })
            })
        } {
            (deactivate-cruise-control)
            (print "Cruise control deactivated: no speed ratio available")
        })
    })
})
(defun monitor-tx-button () {
    (loopwhile t {
        (let ((current-button-state (gpio-read 'pin-tx))) {
            (if (and (= tx-button-state 1) (= current-button-state 0)) {
                (if (= cruise-active 1) {
                    (decrease-cruise-speed)
                } {
                    (switch-profile)
                })
            })
            (setq tx-button-state current-button-state)
        })
        (sleep 0.05)
    })
})
; Config first, then the profile that uses it. Boot always lands on mode 0
; whatever was in force before: waking up in the fastest mode is not a
; behaviour anyone asked for.
(rm-load)
(rm-apply-reverse-speed)
(apply-profile 0)
(spawn 150 update-rpm-per-ms)
(spawn 150 monitor-rx-button)
(spawn 150 monitor-tx-button)
(defun pu8  (v) { (bufset-u8  pbuf pi v) (setq pi (+ pi 1)) })
(defun pi32 (v) { (bufset-i32 pbuf pi (to-i32 v)) (setq pi (+ pi 4)) })
(defun pstr (s) { (bufcpy pbuf pi s 0 (buflen s)) (setq pi (+ pi (buflen s))) })
; Big-endian u16, built from two u8 writes so it uses nothing this script has
; not already proven on this firmware.
(defun pu16 (v) { (pu8 (mod (/ v 256) 256)) (pu8 (mod v 256)) })
(defun rm-send-config (reply-id seq result) {
    (setq pi 0)
    (pu8 0x56) (pu8 0x50) (pu8 0x87)
    (pu16 seq) (pu8 result) (pu8 1)
    (pu16 rm-revision)
    (pu16 rm-s0) (pu16 rm-c0)
    (pu16 rm-s1) (pu16 rm-c1)
    (pu16 rm-s2) (pu16 rm-c2)
    (pu8 rm-rev-en)
    (pu16 rm-rev-speed) (pu16 rm-rev-cur)
    (pu8 rm-persist)
    (send-data pbuf 2 reply-id)
})
; Sent after SELECT and on the dashboard's own cadence. active-speed is what is
; actually applied, not what the profile says it should be, so a mismatch is
; visible rather than assumed away.
(defun rm-send-status (reply-id) {
    (setq pi 0)
    (pu8 0x56) (pu8 0x50) (pu8 0x89)
    (pu16 rm-revision)
    (pu8 current-profile)
    (pu16 (rm-speed-of current-profile))
    (pu8 (if (= rv-dir -1) 255 rv-dir))   ; i8 on the wire
    (pu8 rv-btn) (pu8 rv-armed)
    (pu8 rm-persist) (pu8 rm-fault)
    (send-data pbuf 2 reply-id)
})
(defun panel-send-ui (reply-id) {
    (setq pi 0)
    (pu8 0x56) (pu8 0x50) (pu8 0x81) (pu8 1) (pu8 6)
    (pu8 1) (pu8 1) (pstr "Throttle") (pu8 (if (= throttle-on 1) 1 0))
    (pu8 4) (pu8 2) (pstr "Beep")
    (pu8 5) (pu8 3) (pstr "Beep Vol")
    (pi32 0) (pi32 50000) (pi32 5000) (pi32 (* beep-vol 1000)) (pstr "")
    ; Profile radio group (ids 10..12) — exactly one is lit, tapping a row
    ; selects that profile. Keep the labels in step with apply-profile.
    (pu8 10) (pu8 1) (pstr "Slow 5 km/h")    (pu8 (if (= current-profile 0) 1 0))
    (pu8 11) (pu8 1) (pstr "Medium 10 km/h") (pu8 (if (= current-profile 1) 1 0))
    (pu8 12) (pu8 1) (pstr "Fast 20 km/h")   (pu8 (if (= current-profile 2) 1 0))
    (send-data pbuf 2 reply-id)
})
(defun panel-send-state (reply-id) {
    (setq pi 0)
    (pu8 0x56) (pu8 0x50) (pu8 0x82) (pu8 5)
    (pu8 1) (pi32 (* (if (= throttle-on 1) 1 0) 1000))
    (pu8 5) (pi32 (* beep-vol 1000))
    (pu8 10) (pi32 (* (if (= current-profile 0) 1 0) 1000))
    (pu8 11) (pi32 (* (if (= current-profile 1) 1 0) 1000))
    (pu8 12) (pi32 (* (if (= current-profile 2) 1 0) 1000))
    (send-data pbuf 2 reply-id)
})
(defun panel-send-dash (reply-id) {
    (setq pi 0)
    (pu8 0x56) (pu8 0x50) (pu8 0x84)
    (pi32 (* cruise-active 1000))
    (pi32 (* cruise-rpm 1000))
    (pi32 (* current-profile 1000))
    (pi32 (* rpm-per-ms 1000.0))
    (send-data pbuf 2 reply-id)
})
; Master enable is just a flag now — the motor arbiter owns all output and
; coasts (set-current 0) while throttle-on = 0. No app juggling needed.
(defun panel-set-throttle (on) {
    (if (= on 0) {
        (if (= cruise-active 1) (deactivate-cruise-control))
        (setq throttle-on 0)
    } {
        (setq throttle-on 1)
    })
})
(defun two-beeps () {
    (foc-play-tone 0 800 beep-vol)
    (sleep 0.1)
    (foc-play-stop)
    (sleep 0.06)
    (foc-play-tone 0 900 beep-vol)
    (sleep 0.1)
    (foc-play-stop)
    (sleep 0.3)
    (foc-play-tone 0 800 beep-vol)
    (sleep 0.1)
    (foc-play-stop)
    (sleep 0.06)
    (foc-play-tone 0 900 beep-vol)
    (sleep 0.1)
    (foc-play-stop)
})
; Run the sequence in its own thread so the sleeps don't block panel-event-loop.
(defun panel-beep () (spawn 150 two-beeps))
(defun panel-action (cid val) {
    (cond
        ((= cid 1) (panel-set-throttle (if (> val 0.5) 1 0)))
        ((= cid 4) (panel-beep))
        ((= cid 5) {
            (setq beep-vol (to-i32 val))
            (setq beep-vol-dirty 1)
        })
        ; Profile radio group: the row identifies the profile, so val is ignored
        ; (tapping the already-lit row sends 0 and panel-set-profile no-ops).
        ((= cid 10) (panel-set-profile 0))
        ((= cid 11) (panel-set-profile 1))
        ((= cid 12) (panel-set-profile 2)))
})
; SET is a whole transaction. Refusing tells the rider which rule they broke;
; silently accepting a different config than the one they submitted is the one
; outcome that must never happen, because the screen would then show numbers
; the vehicle is not using.
;
; Order matters: cheap structural checks first, then the ones that depend on
; how the vehicle is moving, so a malformed packet can never reach the code
; that touches conf-set.
(defun rm-apply-set (data reply-id seq) {
    (let ((res
        (cond
            ; 6 header + 1 fmt + 12 modes + 1 + 2 + 2 = 24
            ((< (buflen data) 24) 1)
            ((not (= (bufget-u8 data 6) 1)) 2)
            (t (let ((s0 (bufget-u16 data 7))  (c0 (bufget-u16 data 9))
                     (s1 (bufget-u16 data 11)) (c1 (bufget-u16 data 13))
                     (s2 (bufget-u16 data 15)) (c2 (bufget-u16 data 17))
                     (re (bufget-u8  data 19))
                     (rs (bufget-u16 data 20)) (rc (bufget-u16 data 22)))
                (cond
                    ((not (or (= re 0) (= re 1))) 3)
                    ((or (< s0 10) (> s0 rm-ceiling) (< c0 100) (> c0 1000)
                         (< s1 10) (> s1 rm-ceiling) (< c1 100) (> c1 1000)
                         (< s2 10) (> s2 rm-ceiling) (< c2 100) (> c2 1000)
                         (< rs 10) (> rs 50) (< rc 10) (> rc 140)) 3)
                    ((or (> s0 s1) (> s1 s2)) 4)
                    ; A config change re-scales the current limit and the speed
                    ; ceiling under whatever the motor is doing right now, so it
                    ; is only taken at a standstill with the throttle released.
                    ((> (abs (get-speed)) 0.083) 5)
                    ((> (get-adc-decoded 0) 0.05) 6)
                    ((not (= rv-dir 1)) 7)
                    ; Asking for reverse on a target whose pin never came up is
                    ; refused rather than stored and quietly ignored later.
                    ((and (= re 1) (= rv-hw-ok 0)) 9)
                    (t {
                        (setq rm-s0 s0) (setq rm-c0 c0)
                        (setq rm-s1 s1) (setq rm-c1 c1)
                        (setq rm-s2 s2) (setq rm-c2 c2)
                        (setq rm-rev-en re)
                        (setq rm-rev-speed rs)
                        (setq rm-rev-cur rc)
                        (setq rm-revision (mod (+ rm-revision 1) 65536))
                        ; Apply before persisting: the rider feels the change
                        ; on the next twist whether or not flash cooperates.
                        (apply-profile current-profile)
                        (rm-apply-reverse-speed)
                        (setq rm-persist 1)
                        0
                    })))))))
        {
            (setq rm-fault res)
            (rm-send-config reply-id seq res)
        })
})
(defun panel-handle (data) {
    (if (and (>= (buflen data) 4)
             (= (bufget-u8 data 0) 0x56)
             (= (bufget-u8 data 1) 0x50))
        (let ((msg (bufget-u8 data 2))
              (reply-id (bufget-u8 data 3))) {
            (cond
                ((= msg 0x01) (panel-send-ui reply-id))
                ((= msg 0x03) (panel-send-state reply-id))
                ((= msg 0x04) (panel-send-dash reply-id))
                ((= msg 0x05) {
                    ; Pedal-assist setpoint (fire-and-forget, no reply). i32 mA at
                    ; byte 4 (after magic[0,1], msg[2], reply-id[3]).
                    ;
                    ; SOURCE LOCK: more than one node may stream setpoints (the
                    ; P4 display's on-device PAS idles at 0 A, 20 Hz, forever on
                    ; firmware older than 1.3.1). Interleaved with a real assist
                    ; current those zeros chop pas-amps into 3→0→3… and the
                    ; motor jerks. So: lock onto whoever sent the last NON-ZERO
                    ; setpoint (reply-id = the sender's CAN id) and ignore other
                    ; senders until that source goes silent/zero; a zero from the
                    ; locked source releases the lock, staleness (0.4 s) too.
                    (let ((amps (/ (bufget-i32 data 4) 1000.0)))
                        (if (or (= pas-src reply-id)
                                (= pas-src -1)
                                (> (secs-since pas-seen) 0.4))
                            {
                                (setq pas-amps amps)
                                (setq pas-seen (systime))
                                (setq pas-src (if (> amps 0.0) reply-id -1))
                            }
                            nil))
                })
                ((= msg 0x06) {
                    ; Atomic throttle toggle from the BLE helper (its GUI /
                    ; throttle_ctl): flip our own state — the sender never
                    ; needs to know it — and answer with fresh STATE.
                    (panel-set-throttle (if (= throttle-on 1) 0 1))
                    (panel-send-state reply-id)
                })
                ; The sequence number lives at 4..5, so the length has to be
                ; established BEFORE it is read -- panel-handle only guarantees
                ; four bytes. A truncated request otherwise reads past the
                ; buffer to find the seq it would answer with.
                ((= msg 0x07)
                    (if (>= (buflen data) 6)
                        (rm-send-config reply-id (bufget-u16 data 4) 0)))
                ((= msg 0x08)
                    (if (>= (buflen data) 6)
                        (rm-apply-set data reply-id (bufget-u16 data 4))))
                ; Status poll. Deliberately NOT a re-SELECT of the current
                ; profile: the P4's cached profile goes stale the moment the
                ; rider presses the TX button, and re-asserting it would drag
                ; the mode straight back and make the physical button look
                ; broken.
                ((= msg 0x0A) (rm-send-status reply-id))
                ((= msg 0x09) {
                    ; Changing the forward profile while rolling is allowed --
                    ; it only re-scales limits -- but changing DIRECTION is not,
                    ; and reverse owns the direction while it is engaged.
                    (if (and (>= (buflen data) 7) (= rv-dir 1))
                        (panel-set-profile (bufget-u8 data 6)))
                    (rm-send-status reply-id)
                })
                ((= msg 0x02)
                    (let ((cid (bufget-u8 data 4))
                          (val (/ (bufget-i32 data 5) 1000.0))) {
                        (panel-action cid val)
                        (panel-send-state reply-id)
                    })))
        }))
})
(defun monitor-traction () {
    (let ((last-erpm 0.0)) {
        (loopwhile t {
            (if (= tc-on 1) {
                (let ((erpm (get-rpm))) {
                    (let ((accel (- (abs erpm) (abs last-erpm)))
                          (limit (- 5000.0 (* tc-sens 40.0)))) {
                        (if (> accel limit) (set-current 0))
                    })
                    (setq last-erpm erpm)
                })
            } {
                (setq last-erpm (get-rpm))
            })
            (sleep 0.02)
        })
    })
})
(defun clampf (v lo hi) (if (< v lo) lo (if (> v hi) hi v)))
; Slew v toward target: up at 1/pos-s per second, down at 1/neg-s per second.
(defun slew (v target pos-s neg-s)
    (if (> target v)
        (clampf (+ v (/ ctl-dt pos-s)) 0.0 target)
        (clampf (- v (/ ctl-dt neg-s)) target 1.0)))
; Ramping times come LIVE from the VESC Tool ADC app page (App Settings → ADC
; → Ramping Time Positive/Negative) — same knobs that shaped the stock throttle,
; so tuning stays in VESC Tool and applies instantly. Clamped away from 0 so a
; zero in the config can't divide-by-zero and kill the arbiter thread.
(defun ramp-pos () (clampf (conf-get 'adc-ramp-time-pos) 0.05 5.0))
(defun ramp-neg () (clampf (conf-get 'adc-ramp-time-neg) 0.05 5.0))
; Throttle output: VESC-Tool-style curve, then a slew limit toward the target
; (replaces the ADC app's pos/neg ramping), then RELATIVE current — scales live
; with l-current-max × profile scale and the thermal derating, so changing
; Motor Current Max in VESC Tool takes effect immediately. A released throttle
; (thr <= 0.05) ramps DOWN through the same slew — the arbiter keeps calling
; this until out-rel reaches 0 instead of cutting the current instantly.
(defun throttle-out (thr) {
    (let ((target (if (> thr 0.05)
                      (throttle-curve thr thr-curve-accel 0.0 thr-curve-mode)
                      0.0)))
        (setq out-rel (slew out-rel target (ramp-pos) (ramp-neg))))
    (set-current-rel out-rel 0.2)
})
; Brake output, same shape and same ADC ramp times (the stock ADC app ramped
; the brake with them too): grabbing the lever is a fast ramp, not an instant
; regen hit, and releasing it tails off instead of stepping to 0.
(defun brake-out (brk) {
    (let ((target (if (> brk 0.05) brk 0.0)))
        (setq brk-rel (slew brk-rel target (ramp-pos) (ramp-neg))))
    (set-brake-rel brk-rel)
})
; Reverse current ceiling. Three limits, whichever is smallest: what the rider
; configured, what the motor config allows in the negative direction, and the
; forward maximum. l-current-min-scale is deliberately NOT touched per mode --
; it also scales regenerative braking, and quietly halving the brake to slow
; the reverse would be a genuinely dangerous trade.
(defun reverse-limit ()
    (let ((imin (abs (conf-get 'l-current-min)))
          (imax (conf-get 'l-current-max)))
        (clampf (/ rm-rev-cur 10.0) 0.0 (if (< imin imax) imin imax))))

; Reverse output. A separate ramp from out-rel: sharing it would let a throttle
; release that is still tailing off forward come back out as reverse current.
; set-current with an explicit negative value, never set-current-rel, so the
; magnitude is bounded by reverse-limit and not by the profile scale.
(defun reverse-out (thr) {
    (setq out-rel 0.0)
    (let ((target (if (and (= rv-dir -1) (> thr 0.05)) thr 0.0)))
        (setq rv-rel (slew rv-rel target (ramp-pos) (ramp-neg))))
    (set-current (* (- 0.0 rv-rel) (reverse-limit)) 0.2)
})

; The state machine of contract section 9, evaluated once per monitor tick.
; Everything here is a permission to produce torque; nothing here brakes the
; bike. Releasing the button ramps current to zero and leaves the rider on the
; brake lever, which is where stopping belongs.
(defun reverse-step () {
    (let ((sp  (get-speed))            ; m/s, signed
          (thr (get-adc-decoded 0))
          (brk (get-adc-decoded 1))) {
        (if (or (= rm-rev-en 0) (= rv-hw-ok 0) (= rv-seen-release 0)) {
            ; Feature off, no pin, or the button has been held since boot.
            (setq rv-dir 1) (setq rv-armed 0) (setq rv-brake-ticks 0)
        } {
            (cond
                ((= rv-btn 0) {
                    ; Released. Interlock until the bike has actually stopped
                    ; and the throttle is back at rest, so a rider still rolling
                    ; backwards cannot be handed forward torque.
                    (setq rv-armed 0) (setq rv-brake-ticks 0)
                    (if (not (= rv-dir 1))
                        (if (and (< (abs sp) 0.083) (< thr 0.05))
                            (setq rv-dir 1)
                            (setq rv-dir 0)))
                })
                ((> sp 0.083) {
                    ; Pressed while still moving forward: interlock only. This
                    ; is the case that must never produce negative current.
                    (setq rv-armed 0) (setq rv-brake-ticks 0)
                    (setq rv-dir 0)
                })
                (t {
                    ; Stopped and held. Arm on a deliberate brake hold, then run
                    ; once the brake is released and the throttle is twisted.
                    (if (> brk 0.05)
                        (setq rv-brake-ticks (+ rv-brake-ticks 1))
                        (setq rv-brake-ticks 0))
                    (if (>= rv-brake-ticks 20) (setq rv-armed 1))
                    (if (and (= rv-armed 1) (< brk 0.05))
                        (setq rv-dir -1)
                        (if (not (= rv-dir -1)) (setq rv-dir 0)))
                }))
        })
        ; Anything other than plain forward takes the bike off cruise and PAS
        ; for good, not just for this tick: both would otherwise keep asking for
        ; forward torque underneath the interlock.
        (if (not (= rv-dir 1)) {
            (if (= cruise-active 1) (deactivate-cruise-control))
            (setq pas-amps 0.0)
            (setq pas-src -1)
        })
    })
})

; 100 Hz, matching the motor loop. Debounce is 4 ticks -- 40 ms, inside the
; 30..50 ms the contract asks for.
;
; The pin is configured HERE rather than at load time, and this thread is
; spawned with spawn-trap: on a target that does not expose pin-ppm the
; configure throws, this thread alone dies, and rv-hw-ok stays 0 so reverse
; reports UNSUPPORTED_HARDWARE. The motor arbiter and the panel are unaffected.
; Guessing a different pin is not an option -- it would be an output somewhere
; on a live ESC.
(defun monitor-reverse () {
    (gpio-configure 'pin-ppm 'pin-mode-in-pu)
    (setq rv-hw-ok 1)
    (loopwhile t {
        ; Active-low: the button shorts the pin to ESC ground.
        (let ((raw (if (= (gpio-read 'pin-ppm) 0) 1 0))) {
            (if (= raw rv-btn-raw)
                (if (< rv-btn-count 4) (setq rv-btn-count (+ rv-btn-count 1)))
                { (setq rv-btn-raw raw) (setq rv-btn-count 0) })
            (if (>= rv-btn-count 4) {
                (setq rv-btn rv-btn-raw)
                ; Only a SETTLED release counts. rv-btn starts at 0, so testing
                ; it before the debounce has produced a reading would mark the
                ; button released on the very first tick and hand reverse to a
                ; rider whose button is shorted to ground.
                (if (= rv-btn-raw 0) (setq rv-seen-release 1))
            })
        })
        (reverse-step)
        (sleep 0.01)
    })
})
; Cruise output: PI on ERPM error → current. Integrator anti-windup-clamped to
; the live limit (l-current-max × profile scale, both read fresh each tick so a
; VESC Tool write or profile switch applies immediately; the firmware control
; loop additionally clamps for thermal derating). Sign-aware for reverse cruise.
(defun cruise-out () {
    (let ((err (- cruise-rpm (get-rpm)))
          (imax (* (conf-get 'l-current-max) (conf-get 'l-current-max-scale)))) {
        (if (>= cruise-rpm 0) {
            (setq cruise-i (clampf (+ cruise-i (* cruise-ki err ctl-dt)) 0.0 imax))
            (set-current (clampf (+ (* cruise-kp err) cruise-i) 0.0 imax) 0.2)
        } {
            (setq cruise-i (clampf (+ cruise-i (* cruise-ki err ctl-dt)) (- imax) 0.0))
            (set-current (clampf (+ (* cruise-kp err) cruise-i) (- imax) 0.0) 0.2)
        })
    })
})
; THE motor arbiter — the only place that commands the motor. The native ADC
; app stays configured (its thread keeps decoding the throttle/brake pots for
; get-adc-decoded, and VESC Tool keeps its calibration UI) but its OUTPUT is
; suppressed with a rolling 1.5 s disable that this loop keeps extending. If
; this script ever dies: motor stops via the motor-command timeout (every
; set-* here feeds it), and ~1.5 s later the stock ADC throttle comes back —
; the bike stays rideable (without cruise/PAS) instead of bricking.
; Priority: master-off > brake > direction interlock/reverse > throttle >
; cruise > PAS > coast. Direction outranks the throttle because the whole point
; of the interlock is that a twisted throttle must NOT produce forward torque
; while the bike is in or leaving reverse; it sits under the brake because the
; lever always wins.
(defun motor-control-loop () {
    (loopwhile t {
        (app-disable-output 1500)
        (let ((thr   (get-adc-decoded 0))
              (brake (get-adc-decoded 1))) {
            ; Safe start: no output until the throttle has been seen released
            ; once (protects against a stuck/held throttle at script start).
            (if (< thr 0.05) (setq armed 1))
            (if (= armed 0) (setq thr 0.0))
            ; A branch stays selected while its slew tails off (out-rel /
            ; brk-rel > 0), so releasing throttle or brake ramps down smoothly
            ; instead of stepping the current to 0.
            (cond
                ((= throttle-on 0) {          ; panel master switch — coast
                    (setq out-rel 0.0)
                    (setq brk-rel 0.0)
                    (set-current 0) })
                ((or (> brake 0.05) (> brk-rel 0.001)) {  ; 1. brake
                    (if (> brake 0.05) (deactivate-cruise-control))
                    (setq out-rel 0.0)        ; throttle cut is fine under brake
                    (brake-out brake) })      ; full range, never profile-scaled
                ((or (not (= rv-dir 1)) (> rv-rel 0.001))  ; 2. direction
                    ; Reverse, or the interlock ramping reverse current back to
                    ; zero. Selected while rv-rel is still tailing off so the
                    ; current returns smoothly instead of stepping.
                    (reverse-out thr))
                ((or (> thr 0.05) (> out-rel 0.001)) {    ; 3. throttle
                    (if (> thr 0.05) (deactivate-cruise-control))
                    (throttle-out thr) })
                ((= cruise-active 1)          ; 3. cruise (PI → current)
                    (cruise-out))
                ((and (> pas-amps 0.0)        ; 4. pedal assist from head unit
                      (< (secs-since pas-seen) 0.4))
                    ; Stale setpoint (sensor/link dropped) falls through to
                    ; coast — the P4 watchdog also sends an explicit 0.
                    (set-current pas-amps 0.2))  ; fw clamps to lo_current_max
                (t                            ; 5. coast
                    (set-current 0)))
        })
        (sleep ctl-dt)
    })
})
(defun panel-on-shutdown () {
    (if (or (= beep-vol-dirty 1) (= rm-persist 1)) {
        (shutdown-hold t)
        (if (= beep-vol-dirty 1) {
            (eeprom-store-i beep-vol-addr beep-vol)
            (setq beep-vol-dirty 0)
        })
        (if (= rm-persist 1) (rm-store))
        (shutdown-hold nil)
    })
})
; Beep volume changes via the panel slider (many intermediate values per drag)
; and must survive a reboot. Persisting only in panel-on-shutdown was unreliable:
; event-shutdown fires only on a real power-off, not on a bench / USB / re-flash
; reboot, so eeprom-store-i never ran. Flush the dirty volume on a slow timer
; instead — coalesces a drag into ~one flash write and does not depend on a
; clean shutdown. panel-on-shutdown stays as a final flush.
(defun persist-volumes-loop () {
    (loopwhile t {
        (if (= beep-vol-dirty 1) {
            (eeprom-store-i beep-vol-addr beep-vol) (setq beep-vol-dirty 0) })
        ; Ride config rides the same slow thread, and for the same reason: the
        ; flash write must not happen on the packet handler or anywhere near
        ; the 100 Hz motor loop.
        (if (= rm-persist 1) (rm-store))
        (sleep 2)
    })
})
; Custom button frames from the BLE helper: plain standard-id CAN frames,
; id/data configured per button in the helper GUI. Command = the data bytes
; read as a big-endian u16 (single-byte frames work too):
;   CAN ID 0x123, data 00 01  (button A) -> toggle the throttle master switch
;   CAN ID 0x123, data 00 02  (button B) -> switch speed profile (mode);
;                                           apply-profile beeps per profile
;   anything else             -> just printed; add your commands below
(def helper-btn-id 0x123)
(defun proc-helper-btn (data) {
    (let ((cmd (if (>= (buflen data) 2)
                   (bufget-u16 data 0)
                   (bufget-u8 data 0)))) {
        (cond
            ((= cmd 1) {
                (panel-set-throttle (if (= throttle-on 1) 0 1))
                (print (str-merge "helper: throttle "
                                  (if (= throttle-on 1) "ON" "off")))
            })
            ((= cmd 2) {
                (switch-profile)
                (print (str-merge "helper: profile "
                                  (to-str current-profile)))
            })
            (t (print (str-merge "helper cmd " (to-str cmd)))))
    })
})
(defun panel-event-loop () {
    (loopwhile t {
        (recv ((event-data-rx . (? data)) (panel-handle data))
              ((event-can-sid (? id) . (? data))
                  (if (= id helper-btn-id) (proc-helper-btn data)))
              (event-shutdown               (panel-on-shutdown))
              (_ nil))
    })
})

; Spawn threads and enable events LAST — after every function they reach is
; bound. panel-event-loop → panel-handle → panel-action → panel-set-profile;
; spawned earlier, an incoming panel command during load would hit a still
; unbound binding → the handler thread dies → panel dead.
; These are plain expressions (not definitions), so @const-start does not flash
; them — they just execute here, which is exactly what we want.
(event-register-handler (spawn panel-event-loop))
(event-enable 'event-data-rx)
(event-enable 'event-shutdown)
(spawn 150 persist-volumes-loop)
(spawn 150 motor-control-loop)
; spawn-trap, not spawn: this is the only thread that touches pin-ppm, and on a
; target without it the gpio-configure throws. Trapped, that kills this thread
; alone and leaves rv-hw-ok at 0 -- reverse then reports UNSUPPORTED_HARDWARE
; and the bike rides exactly as it did before.
(spawn-trap 150 monitor-reverse)
@const-end
