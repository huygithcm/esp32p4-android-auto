; Explicit scheduler/API model for the startup interleaving seen on hardware.
; Real host monotonic uptime is nonzero when Lisp starts, just as ESC uptime
; continues across script reloads. Native clock and scheduler are unchanged.
(test-check (> (secs-since 0) 0.1) "fixture starts at nonzero uptime beyond watchdog threshold")
(def host-disable-count 0)
(def host-rx-configured nil)
(def host-rx-release nil)
(def host-rx-fail nil)
(def host-rx-read-count 0)
(def host-reverse-config 1)
; Valid saved ride config, with reverse enabled before workers are spawned.
(defun eeprom-read-i (address)
    (cond ((= address 16) 0x524D02) ((= address 17) 1)
          ((= address 18) (+ 2300 host-reverse-config)) ((= address 20) 500)
          ((= address 21) 700) ((= address 22) 1000)
          ((= address 23) host-reverse-config) ((= address 24) 30)
          ((= address 25) 70) (t nil)))
(defun gpio-configure (pin mode) {
    (if (eq pin 'pin-rx) (setq host-rx-configured t))
    t
})
(defun app-disable-output (timeout) {
    (setq host-disable-count (+ host-disable-count 1))
    ; First invocation is the top-level native-output lock: never block it.
    ; At the motor's first invocation allow RX to reach its first read first.
    (if (= host-disable-count 2)
        (loopwhile (not host-rx-configured) (sleep 0.001)))
    t
})
(defun gpio-read (pin)
    (if (eq pin 'pin-tx) host-tx {
        (loopwhile (not host-rx-release) (sleep 0.001))
        (if host-rx-fail (exit-error 'injected-rx-read-failure))
        (setq host-rx-read-count (+ host-rx-read-count 1))
        host-rx
    }))
