(def tx-button-state 1)
; Motor arbiter state (motor-control-loop). setq'd at runtime → above @const.
(def out-rel 0.0)     ; throttle slew-limiter state (relative current 0..1)
(def brk-rel 0.0)     ; brake slew-limiter state (relative brake current 0..1)
(def armed 0)         ; safe-start: throttle must be seen released once after boot
; No cruise/throttle-hold controller. Ramping times are read live from the
; VESC Tool ADC app page — see throttle-out/brake-out.
; Throttle feel knobs. ctl-dt is the arbiter tick — 100 Hz like Vedder's
; vl_bike pkg: at 20 Hz the ramp advanced in 12.5%-of-max current steps, which
; the FOC loop executes instantly → felt like jerks, not a ramp.
(def ctl-dt 0.01)          ; arbiter period (s)
(def thr-curve-accel 0.0)  ; throttle-curve accel const, -1..1 (0 = linear)
(def thr-curve-mode 0)     ; 0 exponential, 1 natural, 2 polynomial
(def current-profile 0)
(def num-profiles 3)
(def throttle-on 0)
(def park-on 1)
(def safety-fault 0)
(def motor-seen 0)
(def motor-live 0)
(def rv-seen 0)
(def tx-raw 1)
(def tx-count 0)
(def tx-seen-release 0)
(def tx-pressed-at 0)
(def tx-long 0)
(def tx-exit-ok 0)
(def tx-park-ok 0)
(def tx-live 0)
(def tx-seen 0)
(def pas-ready-src -1)
(def safety-owner -1)
(def safety-token -1)
(def safety-consumed 0)
(def safety-result 0)
(def safety-token-at 0)
(def tc-on 0)
(def tc-sens 50.0)
(def pbuf (bufcreate 128))
; send-data sends the full array: safety v1 is exactly 9 payload bytes.
; Keep mutable arrays above @const-start.
(def safety-buf (bufcreate 9))
(def status-seq-buf (bufcreate 19))
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
; FORMAT 2. A mode is one ABSOLUTE motor-current limit in deci-amps and
; nothing else:
;
;     effective = min(requested, l-current-max)
;
; The requested figure is stored as typed even when it exceeds what this ESC
; allows, so raising Motor Current Max later starts using it with nothing
; re-entered. The ESC is never raised to meet a mode -- it stays the master
; limit and the firmware's thermal and hardware protection sit underneath.
;
; Speed is Motor Settings' business now; ride modes no longer touch max-speed.
; The modes are independent, so 90/40/70 A is a legitimate choice.
(def rm-a0 500)   ; 50.0 A
(def rm-a1 700)   ; 70.0 A
(def rm-a2 1000)  ; 100.0 A -- above a 70 A ESC on purpose; it clamps
; Last l-current-max read, in dA. Cached so the status packet and the scale
; sync agree within a tick and neither has to call conf-get twice.
(def rm-esc-max 0)
; The scale actually written, so sync-current-scale only calls conf-set when
; the answer changes rather than every 10 ms.
(def rm-scale-applied -1.0)
(def rm-rev-en 0)
(def rm-rev-speed 30)
(def rm-rev-cur 70)
(def rm-revision 0)
(def rm-persist 0)     ; EEPROM still owes a write
(def rm-fault 0)       ; result code of the last refusal, 0 = none
; Entry range, in dA. Three digits of whole amperes, as agreed with the user.
; Deliberately NOT tied to the ESC: a figure above what this ESC can deliver is
; legal to store and simply clamps when applied.
(def rm-a-min 10)      ;   1.0 A
(def rm-a-max 9990)    ; 999.0 A
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
; Set by the monitor thread only after gpio-configure has returned. If RX
; cannot be configured or read, the thread dies there and this stays 0, which
; is what makes reverse report UNSUPPORTED_HARDWARE rather than pretending.
(def rv-hw-ok 0)
; @const-start flashes every definition below, freeing the cons heap. Without it
; all the defun bodies live in RAM and exhaust the heap — panel-event-loop then
; OOMs at runtime and the display goes blank while motor control keeps running.
; Everything mutable MUST stay above this line: setq'd scalars, and especially
; the pbuf buffer (a flashed buffer is read-only, bufset would fail/crash).
@const-start
; Native application output must not automatically return when Lisp stops.
; This lock does not replace the ESC motor-command timeout or hardware inhibit.
(app-disable-output -1)
(set-current 0)
; Persist ADC Control Type NONE in VESC Tool before enabling this script.
; Do not silently write the user's flash config: NONE also prevents native
; throttle output during reboot/before Lisp has started. ADC decoding remains.
(defun native-input-only ()
    (match (trap (conf-get 'adc-ctrl-type))
        ((exit-ok (? value)) (= value 0))
        (_ nil)))
(if (not (native-input-only)) (setq safety-fault 9))

(defun drive-clear () {
    (setq out-rel 0.0) (setq rv-rel 0.0)
    (setq pas-amps 0.0) (setq pas-src -1) (setq pas-ready-src -1)
    (setq rv-armed 0) (setq rv-brake-ticks 0)
})
(defun ride-stopped () (<= (abs (get-speed)) 0.083))
(defun ride-throttle-idle () (<= (get-adc-decoded 0) 0.05))
; Explicit target command, never a toggle. Returns a protocol result code.
; P locks propulsion, while a deliberate brake request still reaches brake-out.
(defun ride-set-park (on) {
    (cond
        ((not (or (= on 0) (= on 1))) 3)
        ((= on park-on) 0)
        ((not (ride-stopped)) 5)
        ((not (ride-throttle-idle)) 6)
        ((and (= on 1) (<= (get-adc-decoded 1) 0.05)) 11)
        ((and (= on 0) (or (not (= safety-fault 0)) (= motor-live 0)
                           (> (secs-since motor-seen) 0.1))) 12)
        ((and (= on 0) (= rv-btn 1)) 7)
        (t {
            (drive-clear)
            (setq rv-dir 1)
            (setq park-on on)
            (setq throttle-on (if (= on 1) 0 1))
            0
        }))
})
(defun ride-input-fault () {
    (setq safety-fault 12)
    (setq park-on 1) (setq throttle-on 0)
    (drive-clear)
    (setq rv-dir 0)
})
; Profiles scale the current limit instead of overwriting it: Motor Current Max
; in VESC Tool stays the master value (applied live, no LISP restart) and each
; profile is a fraction of it. Braking is never scaled — always full.
(defun rm-amps-of (i) (if (= i 0) rm-a0 (if (= i 1) rm-a1 rm-a2)))

; l-current-max in dA, floored at zero. Cached so the status packet and the
; scale sync see the same number within a tick.
(defun rm-read-esc-max () {
    (let ((m (* (conf-get 'l-current-max) 10.0)))
        (setq rm-esc-max (if (> m 0.0) (to-i m) 0)))
    rm-esc-max
})

; What the active mode actually delivers: the requested figure clamped to
; whatever the ESC currently allows.
(defun rm-effective-dA () {
    (let ((req (rm-amps-of current-profile))
          (esc (rm-read-esc-max)))
        (if (< esc req) esc req))
})

; Turn the absolute limit into the scale the firmware understands. The ESC's
; own Motor Current Max is never written: it is the master limit, and raising
; it to satisfy a mode would quietly hand the rider more than the ESC was
; configured for. Scaling instead means throttle, PAS and the native ADC
; fallback all sit under the same cap, and the firmware's thermal and hardware
; protection stay underneath all of it.
(defun rm-desired-scale () {
    (let ((esc (rm-read-esc-max)))
        (if (<= esc 0) 0.0 (/ (to-float (rm-effective-dA)) (to-float esc))))
})

; Speed is Motor Settings' business now. apply-profile does not touch
; max-speed at all, so a mode change no longer moves the speed limit.
; Mode selection only changes the current ceiling: no motor tone or drive
; command. Motor tones energize the windings even with the throttle released.
(defun apply-profile (profile-index) {
    (let ((sc (rm-desired-scale))) {
        (conf-set 'l-current-max-scale sc)
        (setq rm-scale-applied sc)
        (print (str-merge "Mode " (to-str (+ profile-index 1)) ": "
                          (to-str (/ (rm-amps-of profile-index) 10.0)) " A req, "
                          (to-str (/ (rm-effective-dA) 10.0)) " A effective"))
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
(def rm-ee-tag   0x524D02)    ; 'RM' + format version 2
; The magic changed with the format on purpose. Format 1 stored a speed and a
; per-mille scale in these slots; reading those numbers as amperes would give a
; 30 that means "30% of max" the meaning "3.0 A". A stale block must read as
; absent, not as a config.

(defun rm-checksum ()
    (mod (+ rm-a0 rm-a1 rm-a2
            rm-rev-en rm-rev-speed rm-rev-cur) 65536))

; Bounds are re-checked on the way IN as well as on the way out: EEPROM can be
; stale from an older format or simply corrupt, and a config that passed
; validation when it was written is not automatically valid now.
; No ordering rule in format 2: the modes are independent limits.
(defun rm-values-ok (a0 a1 a2 re rs rc)
    (and (>= a0 rm-a-min) (<= a0 rm-a-max)
         (>= a1 rm-a-min) (<= a1 rm-a-max)
         (>= a2 rm-a-min) (<= a2 rm-a-max)
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

; Probe the requested reverse speed before committing a SET transaction.
; `min-speed` is hardware/firmware dependent. Previously an error here killed
; panel-event-loop before rm-send-config ran, leaving the P4 stuck on
; "Saving to VESC..." with no ACK. Keep the error inside this call and let the
; transaction return UNSUPPORTED_HARDWARE instead.
(defun rm-try-reverse-speed (enabled speed-dkmh)
    (match (trap
        (if (= enabled 1)
            (conf-set 'min-speed (/ (/ speed-dkmh 10.0) 3.6))
            t))
        ((exit-ok (? value)) t)
        ((exit-error (? error)) {
            (print (str-merge "ride SET: min-speed failed: " (to-str error)))
            nil
        })
        (_ {
            (print "ride SET: min-speed failed")
            nil
        })))
(defun rm-load () {
    ; eeprom-read-i answers nil for a slot that was never written, and `=` on
    ; nil is a type error, not false. On a board that has never stored a ride
    ; config -- every new board -- comparing it directly would throw here, and
    ; rm-load runs at load time, so the whole script would fail to start.
    ; Guard the magic; if it matches, rm-store wrote every field before it, so
    ; the reads below are known good.
    (let ((magic (eeprom-read-i rm-ee-magic)))
    (if (and magic (= magic rm-ee-tag)) {
        (let ((a0 (eeprom-read-i (+ rm-ee-base 0)))
              (a1 (eeprom-read-i (+ rm-ee-base 1)))
              (a2 (eeprom-read-i (+ rm-ee-base 2)))
              (re (eeprom-read-i (+ rm-ee-base 3)))
              (rs (eeprom-read-i (+ rm-ee-base 4)))
              (rc (eeprom-read-i (+ rm-ee-base 5)))) {
            (if (and (= (eeprom-read-i rm-ee-sum)
                        (mod (+ a0 a1 a2 re rs rc) 65536))
                     (rm-values-ok a0 a1 a2 re rs rc)) {
                (setq rm-a0 a0) (setq rm-a1 a1) (setq rm-a2 a2)
                (setq rm-rev-en re)
                (setq rm-rev-speed rs)
                (setq rm-rev-cur rc)
                (setq rm-revision (eeprom-read-i rm-ee-rev))
                (print "ride config: loaded from eeprom")
            } (print "ride config: eeprom rejected, using defaults"))
        })
    } (print "ride config: no eeprom block, using defaults")))
})

; Magic last, for the reason above. Runs off the slow persist thread, never
; from the packet handler: a flash write inside the motor loop's period is how
; a control tick gets missed.
(defun rm-store () {
    (eeprom-store-i rm-ee-magic 0)
    (eeprom-store-i (+ rm-ee-base 0) rm-a0)
    (eeprom-store-i (+ rm-ee-base 1) rm-a1)
    (eeprom-store-i (+ rm-ee-base 2) rm-a2)
    (eeprom-store-i (+ rm-ee-base 3) rm-rev-en)
    (eeprom-store-i (+ rm-ee-base 4) rm-rev-speed)
    (eeprom-store-i (+ rm-ee-base 5) rm-rev-cur)
    (eeprom-store-i rm-ee-sum (rm-checksum))
    (eeprom-store-i rm-ee-rev rm-revision)
    (eeprom-store-i rm-ee-magic rm-ee-tag)
    (setq rm-persist 0)
})
; pin-rx is configured inside monitor-reverse, under spawn-trap. Doing it here
; as well would put an unguarded call back at load time and undo the guard.
; Both of these are kept as the names their callers already use -- the BLE
; helper's cmd=2 and the quick panel's radio group -- but neither decides
; anything any more. Every mode change funnels through ride-select-mode so the
; reverse/interlock guard lives in exactly one place.
(defun switch-profile () (ride-select-next))
(defun panel-set-profile (idx) (ride-select-mode idx))
; TX is the Mode button and nothing else now that cruise is gone.
;
; Debounced on a stable count rather than by sampling slowly: the old version
; read the pin every 50 ms and took any 1->0 it happened to catch, so a bouncing
; contact could land two mode changes from one press. Four stable 10 ms reads is
; 40 ms of agreement before an edge counts.
(defun mode-button-step (raw) {
    (if (= raw tx-raw)
        (if (< tx-count 4) (setq tx-count (+ tx-count 1)))
        { (setq tx-raw raw) (setq tx-count 0) })
    (if (>= tx-count 4) {
        (if (= tx-raw 1) {
            (if (and (= tx-button-state 0) (= tx-seen-release 1) (= tx-long 0)) {
                (if (= park-on 1)
                    (if (= tx-exit-ok 1) (setq rm-fault (ride-set-park 0)))
                    (ride-select-next))
            })
            (setq tx-seen-release 1)
        } {
            (if (= tx-button-state 1) {
                (setq tx-pressed-at (systime))
                (setq tx-long 0)
                (setq tx-exit-ok (if (and (ride-stopped) (ride-throttle-idle)) 1 0))
                (setq tx-park-ok tx-exit-ok)
            })
            (if (and (= tx-seen-release 1) (= tx-long 0)
                     (>= (secs-since tx-pressed-at) 1.0)) {
                ; Consume the long press even if refused; release never selects.
                (setq tx-long 1)
                (if (= tx-park-ok 1) (setq rm-fault (ride-set-park 1)))
            })
        })
        (setq tx-button-state tx-raw)
    })
})
(defun monitor-tx-button () {
    (gpio-configure 'pin-tx 'pin-mode-in-pu)
    (loopwhile t {
        (mode-button-step (gpio-read 'pin-tx))
        (setq tx-seen (systime)) (setq tx-live 1)
        (sleep 0.01)
    })
})
; Config first, then the profile that uses it. Boot always lands on mode 0
; whatever was in force before: waking up in the fastest mode is not a
; behaviour anyone asked for.
(rm-load)
; A persisted reverse setting must not be able to kill the whole script during
; boot on a build that does not expose `min-speed`. Fail closed and keep the
; panel/current modes alive so the rider can disable or correct the setting.
(if (not (rm-try-reverse-speed rm-rev-en rm-rev-speed)) {
    (setq rm-fault 9)
    (setq rm-rev-en 0)
    (print "ride config: reverse disabled; min-speed unsupported")
})
(apply-profile 0)
(defun pu8  (v) { (bufset-u8  pbuf pi v) (setq pi (+ pi 1)) })
(defun pi32 (v) { (bufset-i32 pbuf pi (to-i32 v)) (setq pi (+ pi 4)) })
(defun pstr (s) { (bufcpy pbuf pi s 0 (buflen s)) (setq pi (+ pi (buflen s))) })
; Big-endian u16, built from two u8 writes so it uses nothing this script has
; not already proven on this firmware.
(defun pu16 (v) { (pu8 (mod (/ v 256) 256)) (pu8 (mod v 256)) })
(defun rm-send-config (reply-id seq result) {
    (setq pi 0)
    (pu8 0x56) (pu8 0x50) (pu8 0x87)
    (pu16 seq) (pu8 result) (pu8 2)
    (pu16 rm-revision)
    (pu16 rm-a0) (pu16 rm-a1) (pu16 rm-a2)
    (pu8 rm-rev-en)
    (pu16 rm-rev-speed) (pu16 rm-rev-cur)
    (pu16 (rm-read-esc-max))
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
    (pu16 (rm-amps-of current-profile))
    (pu16 (rm-effective-dA))
    (pu16 rm-esc-max)
    (pu8 (if (= rv-dir -1) 255 rv-dir))   ; i8 on the wire
    (pu8 rv-btn) (pu8 rv-armed)
    (pu8 rm-persist) (pu8 rm-fault)
    (send-data pbuf 2 reply-id)
})
(defun panel-send-ui (reply-id) {
    (setq pi 0)
    (pu8 0x56) (pu8 0x50) (pu8 0x81) (pu8 1) (pu8 4)
    (pu8 6) (pu8 2) (pstr "Park")
    ; Mode radio group (ids 10..12) — exactly one is lit, tapping a row selects
    ; it. The labels are deliberately plain: they used to read "Slow 5 km/h"
    ; and friends, which stopped being true the moment the limits became
    ; editable, and a label that lies is worse than one that says little.
    (pu8 10) (pu8 1) (pstr "Mode 1") (pu8 (if (= current-profile 0) 1 0))
    (pu8 11) (pu8 1) (pstr "Mode 2") (pu8 (if (= current-profile 1) 1 0))
    (pu8 12) (pu8 1) (pstr "Mode 3") (pu8 (if (= current-profile 2) 1 0))
    (send-data pbuf 2 reply-id)
})
(defun panel-send-state (reply-id) {
    (setq pi 0)
    (pu8 0x56) (pu8 0x50) (pu8 0x82) (pu8 3)
    (pu8 10) (pi32 (* (if (= current-profile 0) 1 0) 1000))
    (pu8 11) (pi32 (* (if (= current-profile 1) 1 0) 1000))
    (pu8 12) (pi32 (* (if (= current-profile 2) 1 0) 1000))
    (send-data pbuf 2 reply-id)
})
(defun panel-send-dash (reply-id) {
    (setq pi 0)
    (pu8 0x56) (pu8 0x50) (pu8 0x84)
    ; Cruise is gone. The four slots keep their positions and widths so the
    ; packet stays a fixed 4 x i32, but they carry what the dashboard actually
    ; needs now: which way the bike is allowed to go, and whether reverse is
    ; armed. The rider has to be able to see that without opening a settings
    ; screen -- an armed bike that looks idle is how someone twists the
    ; throttle and goes backwards.
    (pi32 (* rv-dir 1000))
    (pi32 (* rv-armed 1000))
    (pi32 (* current-profile 1000))
    (pi32 (* (rm-effective-dA) 1000))
    (send-data pbuf 2 reply-id)
})
; Sequenced status is a separate message so legacy 0x89 cannot masquerade as
; a response from the newly selected ESC. The P4 checks the echoed poll token.
(defun rm-send-status-seq (reply-id seq) {
    (setq pi 0)
    (pu8 0x56) (pu8 0x50) (pu8 0x8D) (pu16 seq)
    (pu16 rm-revision) (pu8 current-profile)
    (pu16 (rm-amps-of current-profile)) (pu16 (rm-effective-dA))
    (pu16 rm-esc-max)
    (pu8 (if (= rv-dir -1) 255 rv-dir))
    (pu8 rv-btn) (pu8 rv-armed) (pu8 rm-persist) (pu8 rm-fault)
    (bufcpy status-seq-buf 0 pbuf 0 19)
    (send-data status-seq-buf 2 reply-id)
})
; Dedicated safety protocol v1. Old DASH layout is not reinterpreted.
; State: 0 P, 1 forward, 2 reverse ready, 3 reverse active, 4 interlock, 5 fault.
(defun ride-safety-state ()
    (cond
        ((or (not (= safety-fault 0)) (= motor-live 0)
             (> (secs-since motor-seen) 0.1)) 5)
        ((= park-on 1) 0)
        ((and (= rv-armed 1) (= rv-dir -1)) 3)
        ((= rv-armed 1) 2)
        ((not (= rv-dir 1)) 4)
        (t 1)))
(defun safety-send (reply-id seq msg result) {
    (bufset-u8 safety-buf 0 0x56) (bufset-u8 safety-buf 1 0x50)
    (bufset-u8 safety-buf 2 msg) (bufset-u8 safety-buf 3 1)
    (bufset-u8 safety-buf 4 (mod (/ seq 256) 256))
    (bufset-u8 safety-buf 5 (mod seq 256))
    (bufset-u8 safety-buf 6 (ride-safety-state))
    (bufset-u8 safety-buf 7 current-profile) (bufset-u8 safety-buf 8 result)
    (send-data safety-buf 2 reply-id)
})
(defun safety-query (reply-id seq) {
    (setq safety-owner reply-id) (setq safety-token seq)
    (setq safety-token-at (systime)) (setq safety-consumed 0)
    (safety-send reply-id seq 0x8B rm-fault)
})
(defun safety-set (reply-id seq on) {
    (if (and (= reply-id safety-owner) (= seq safety-token)
             (< (secs-since safety-token-at) 1.0)) {
        (if (= safety-consumed 0) {
            (setq safety-consumed 1)
            (setq safety-result (ride-set-park on))
            (setq rm-fault safety-result)
        })
        (safety-send reply-id seq 0x8C safety-result)
    } (safety-send reply-id seq 0x8C 14))
})

; A source must supply a fresh zero after leaving P/interlock before it may
; supply assist. Zeros from another CAN source cannot unlock this source.
(defun ride-pas-input (reply-id amps) {
    (if (or (= park-on 1) (not (= rv-dir 1)) (not (= safety-fault 0))) {
        (setq pas-amps 0.0) (setq pas-src -1) (setq pas-ready-src -1)
    } {
        (if (<= amps 0.0) {
            (if (or (= pas-src -1) (= pas-src reply-id)
                    (> (secs-since pas-seen) 0.4)) {
                (setq pas-amps 0.0) (setq pas-src -1)
                (setq pas-ready-src reply-id) (setq pas-seen (systime))
            })
        } {
            (if (and (= pas-ready-src reply-id)
                     (< (secs-since pas-seen) 0.4)
                     (or (= pas-src -1) (= pas-src reply-id))) {
                (setq pas-amps amps) (setq pas-src reply-id)
                (setq pas-seen (systime))
            })
        })
    })
})
; Master enable is just a flag now — the motor arbiter owns all output and
; coasts (set-current 0) while throttle-on = 0. No app juggling needed.
; Legacy unsequenced enable/toggle packets cannot unlock P. Use MODE or the
; new one-shot safety command. Motor tones are removed from the driving app.
(defun panel-set-throttle (on)
    (setq rm-fault (if (= on 0) (ride-set-park 1) 13)))
(defun panel-action (cid val) {
    (cond
        ((= cid 1) (panel-set-throttle (if (> val 0.5) 1 0)))
        ((= cid 6) (setq rm-fault (ride-set-park 1)))
        ; Profile radio group: the row identifies the profile, so val is ignored
        ; (tapping the already-lit row sends 0 and panel-set-profile no-ops).
        ((= cid 10) (panel-set-profile 0))
        ((= cid 11) (panel-set-profile 1))
        ((= cid 12) (panel-set-profile 2)))
})
; SET is a whole transaction. Refusing tells the rider which rule they broke;
; silently accepting a different config than the one submitted is the one
; outcome that must never happen, because the screen would then show numbers
; the vehicle is not using.
;
; Order matters: cheap structural checks first, then the ones that depend on
; how the vehicle is moving, so a malformed packet can never reach the code
; that touches conf-set.
;
; Format 2 payload, as Lisp sees it (no COMM byte):
;   [0..1] magic [2] 0x08 [3] reply [4..5] seq [6] fmt
;   [7..8] a0 [9..10] a1 [11..12] a2
;   [13] reverse_enabled [14..15] rev_speed [16..17] rev_current   = 18 bytes
(defun rm-apply-set (data reply-id seq) {
    (print (str-merge "ride SET rx seq=" (to-str seq)))
    (let ((res
        (cond
            ((< (buflen data) 18) 1)
            ((not (= (bufget-u8 data 6) 2)) 2)
            (t (let ((a0 (bufget-u16 data 7))
                     (a1 (bufget-u16 data 9))
                     (a2 (bufget-u16 data 11))
                     (re (bufget-u8  data 13))
                     (rs (bufget-u16 data 14))
                     (rc (bufget-u16 data 16)))
                (cond
                    ((not (rm-values-ok a0 a1 a2 re rs rc)) 3)
                    ; Re-scaling the current limit under a moving motor is not
                    ; something to do behind the rider's back, so a config
                    ; change is only taken at a standstill with the throttle
                    ; released.
                    ((> (abs (get-speed)) 0.083) 5)
                    ((> (get-adc-decoded 0) 0.05) 6)
                    ((not (= rv-dir 1)) 7)
                    ; Asking for reverse on a board whose button never came up
                    ; is refused rather than stored and quietly ignored later.
                    ((and (= re 1) (= rv-hw-ok 0)) 9)
                    ; A missing/unsupported min-speed binding used to throw
                    ; here and prevent every ACK after Save. Probe it under a
                    ; trap before changing the stored ride configuration.
                    ((not (rm-try-reverse-speed re rs)) 9)
                    (t {
                        (setq rm-a0 a0) (setq rm-a1 a1) (setq rm-a2 a2)
                        (setq rm-rev-en re)
                        (setq rm-rev-speed rs)
                        (setq rm-rev-cur rc)
                        (setq rm-revision (mod (+ rm-revision 1) 65536))
                        ; Apply before persisting: the rider feels the change on
                        ; the next twist whether or not flash cooperates.
                        (apply-profile current-profile)
                        (setq rm-persist 1)
                        0
                    })))))))
        {
            ; A success clears the old refusal. Leaving it set showed a stale
            ; error beside a config that had just saved cleanly.
            (setq rm-fault res)
            (print (str-merge "ride SET ack seq=" (to-str seq)
                              " result=" (to-str res)))
            (rm-send-config reply-id seq res)
        })
})

; Every path that changes mode goes through here: the TX button, the quick
; panel's radio group, ride SELECT 0x09 and the BLE helper. Putting the
; direction guard in one place is the point -- it used to live only in the TX
; monitor, so the other three could still change mode mid-reverse.
(defun ride-select-mode (idx) {
    (if (and (= safety-fault 0) (= rv-dir 1) (>= idx 0) (< idx num-profiles)
             (not (= idx current-profile))) {
        (setq current-profile idx)
        (apply-profile current-profile)
    })
})
(defun ride-select-next ()
    (ride-select-mode (mod (+ current-profile 1) num-profiles)))

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
                ((= msg 0x05)
                    (if (= (buflen data) 8)
                        (ride-pas-input reply-id (/ (bufget-i32 data 4) 1000.0))))
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
                ((= msg 0x0D)
                    (if (= (buflen data) 6)
                        (rm-send-status-seq reply-id (bufget-u16 data 4))))
                ((= msg 0x0B)
                    (if (= (buflen data) 6)
                        (safety-query reply-id (bufget-u16 data 4))))
                ((= msg 0x0C)
                    (if (= (buflen data) 7)
                        (safety-set reply-id (bufget-u16 data 4) (bufget-u8 data 6))))
                ((= msg 0x09) {
                    ; Changing the forward profile while rolling is allowed --
                    ; it only re-scales limits -- but changing DIRECTION is not,
                    ; and reverse owns the direction while it is engaged.
                    (if (and (>= (buflen data) 7) (= rv-dir 1))
                        (panel-set-profile (bufget-u8 data 6)))
                    (rm-send-status reply-id)
                })
                ((and (= msg 0x02) (= (buflen data) 9))
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
        (if (or (= park-on 1) (not (= safety-fault 0))) {
            (setq rv-armed 0) (setq rv-brake-ticks 0)
        } {
            (cond
                ((or (= rm-rev-en 0) (= rv-hw-ok 0)
                     (= rv-seen-release 0) (= rv-btn 0)) {
                    ; Released. Interlock until the bike has actually stopped
                    ; and the throttle is back at rest, so a rider still rolling
                    ; backwards cannot be handed forward torque.
                    (setq rv-armed 0) (setq rv-brake-ticks 0)
                    (if (not (= rv-dir 1))
                        (if (and (< (abs sp) 0.083) (< thr 0.05))
                            (setq rv-dir 1)
                            (setq rv-dir 0)))
                })
                ; Already authorized reverse is allowed to move backward.
                ; Never use the standstill arming test to revoke valid motion.
                ((and (= rv-armed 1) (<= sp 0.083)) {
                    (if (<= brk 0.05) (setq rv-dir -1))
                })
                ((> (abs sp) 0.083) {
                    ; New arming is prohibited while rolling in EITHER direction.
                    (setq rv-armed 0) (setq rv-brake-ticks 0)
                    (setq rv-dir 0)
                })
                (t {
                    ; Stopped and held. Arm on a deliberate brake hold, then run
                    ; once the brake is released and the throttle is twisted.
                    ;
                    ; The brake hold only counts while the throttle is at rest.
                    ; Counting it regardless meant a rider holding both could
                    ; arm, and then the moment the brake came off the very next
                    ; tick saw an open throttle and went straight to full
                    ; reverse current. Arming has to be a deliberate act with
                    ; nothing else asking for torque.
                    (if (and (> brk 0.05) (< thr 0.05))
                        (setq rv-brake-ticks (+ rv-brake-ticks 1))
                        (setq rv-brake-ticks 0))
                    (if (> thr 0.05) (setq rv-armed 0))
                    (if (>= rv-brake-ticks 20) (setq rv-armed 1))
                    (if (and (= rv-armed 1) (< brk 0.05))
                        (setq rv-dir -1)
                        (if (not (= rv-dir -1)) (setq rv-dir 0)))
                }))
        })
        ; Anything other than plain forward takes the bike off PAS for good,
        ; not just for this tick: it would otherwise keep asking for forward
        ; torque underneath the interlock.
        (if (not (= rv-dir 1)) {
            (setq pas-amps 0.0)
            (setq pas-src -1)
            (setq pas-ready-src -1)
        })
    })
})

; 100 Hz, matching the motor loop. Debounce is 4 ticks -- 40 ms, inside the
; 30..50 ms the contract asks for.
;
; RX is the reverse button. It was the cruise button, and cruise is gone, so
; the wiring already exists, is already a dry contact to ESC ground, and is
; already proven on this vehicle. That is what replaced the earlier PPM plan:
; no unknown connector pin, and no dependence on a pin name this firmware may
; not expose.
;
; The pin is still configured HERE rather than at load time, and this thread is
; still spawned with spawn-trap, so a GPIO failure kills this thread alone,
; leaves rv-hw-ok at 0, and reverse reports UNSUPPORTED_HARDWARE while forward
; riding carries on untouched.
(defun monitor-reverse () {
    (gpio-configure 'pin-rx 'pin-mode-in-pu)
    (setq rv-hw-ok 1)
    (loopwhile t {
        ; Active-low: the button shorts the pin to ESC ground.
        (let ((raw (if (= (gpio-read 'pin-rx) 0) 1 0))) {
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
        (setq rv-seen (systime))
        (sleep 0.01)
    })
})
; THE motor arbiter — the only place that commands the motor. The native ADC
; app stays configured (its thread keeps decoding the throttle/brake pots for
; get-adc-decoded, and VESC Tool keeps its calibration UI) but its OUTPUT is
; suppressed indefinitely. ADC control type NONE must also be persisted before
; deployment, so reboot/script failure cannot hand propulsion to native ADC.
; The configured ESC timeout must be verified on the target with motor power
; inhibited; optOffDelay on set-current is NOT a command watchdog timeout.
; Priority: fault stop > brake > P > direction interlock/reverse > throttle >
; PAS > coast. Direction outranks the throttle because the whole point
; of the interlock is that a twisted throttle must NOT produce forward torque
; while the bike is in or leaving reverse; it sits under the brake because the
; lever always wins.
; Re-derive the scale from the ESC's live Motor Current Max. VESC Tool can
; change that at any time and a mode is an absolute ampere figure, so the scale
; expressing it has to move with it: drop Motor Current Max from 70 to 60 and a
; 100 A mode becomes 60 A on the next tick, with nothing reloaded. conf-set only
; when the answer actually changes -- writing it at 100 Hz would be pointless
; traffic through the config layer.
(defun sync-current-scale () {
    (let ((sc (rm-desired-scale)))
        (if (not (= sc rm-scale-applied)) {
            (conf-set 'l-current-max-scale sc)
            (setq rm-scale-applied sc)
        }))
})
(defun motor-control-step () {
        (app-disable-output -1)
        (if (or (not (native-input-only))
                (not (app-adc-range-ok))
                (and (= tx-live 1) (> (secs-since tx-seen) 0.1))
                (and (= rm-rev-en 1) (= rv-hw-ok 1)
                     (> (secs-since rv-seen) 0.1)))
            (ride-input-fault))
        (reverse-step)
        (sync-current-scale)
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
                ((not (= safety-fault 0)) {
                    (drive-clear)
                    (set-current 0) })
                ((or (> brake 0.05) (> brk-rel 0.001)) {  ; 1. brake
                    (setq out-rel 0.0)        ; throttle cut is fine under brake
                    ; Reverse current has to RAMP to zero under the brake, not
                    ; sit frozen. This branch outranks the direction branch, so
                    ; reverse-out never runs while braking -- rv-rel kept its
                    ; last value and the bike resumed at exactly that current
                    ; the moment the lever came off. Decay it here instead.
                    ; rv-armed is deliberately left set: releasing the brake
                    ; resumes reverse, which is what reversing into a parking
                    ; space actually needs.
                    (setq rv-rel (slew rv-rel 0.0 (ramp-pos) (ramp-neg)))
                    (brake-out brake) })      ; full range, never profile-scaled
                ((or (= park-on 1) (= throttle-on 0)) {
                    (drive-clear)
                    (set-current 0) })
                ((or (not (= rv-dir 1)) (> rv-rel 0.001))  ; 2. direction
                    ; Reverse, or the interlock ramping reverse current back to
                    ; zero. Selected while rv-rel is still tailing off so the
                    ; current returns smoothly instead of stepping.
                    (reverse-out thr))
                ((or (> thr 0.05) (> out-rel 0.001)) {    ; 3. throttle
                    (throttle-out thr) })
                ((and (> pas-amps 0.0)        ; 4. pedal assist from head unit
                      (< (secs-since pas-seen) 0.4))
                    ; Stale setpoint (sensor/link dropped) falls through to
                    ; coast — the P4 watchdog also sends an explicit 0.
                    (set-current pas-amps 0.2))  ; fw clamps to lo_current_max
                (t                            ; 5. coast
                    (set-current 0)))
        })
        (setq motor-seen (systime)) (setq motor-live 1)
})
(defun motor-control-loop () {
    (loopwhile t {
        (motor-control-step)
        (sleep ctl-dt)
    })
})
; Independent supervisor catches an arbiter exception/hang while the VM still
; runs. Total VM failure relies on native NONE plus the ESC command timeout.
(defun motor-supervisor () {
    (loopwhile t {
        (if (and (= motor-live 1) (> (secs-since motor-seen) 0.1)) {
            (ride-input-fault)
            (set-current 0)
        })
        (sleep 0.02)
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
;   CAN ID 0x123, data 00 02  (button B) -> switch current mode, silently
;   anything else             -> just printed; add your commands below
(def helper-btn-id 0x123)
(defun proc-helper-btn (data) {
    (if (> (buflen data) 0)
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
    }))
})
(defun panel-event-loop () {
    (loopwhile t {
        (recv ((event-data-rx . (? data)) (panel-handle data))
              ((event-can-sid . ((? id) . (? data)))
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
(spawn-trap 200 motor-control-loop)
(spawn 150 motor-supervisor)
; Mode button. Spawned down here with the rest, not next to its definition:
; it reaches ride-select-next, which is bound further down the file, and a
; thread that starts before its callee is bound dies on the first press.
(spawn-trap 150 monitor-tx-button)
; spawn-trap, not spawn: this thread owns the RX pin, and a gpio-configure that
; throws would otherwise take the whole script with it. Trapped, it kills this
; thread alone and leaves rv-hw-ok at 0 -- reverse then reports
; UNSUPPORTED_HARDWARE and the bike rides exactly as it did before.
(spawn-trap 150 monitor-reverse)
@const-end
