; RX watchdog is enabled only while reverse is enabled and GPIO init succeeded.
(sleep 0.2)
(test-check (and (= diag-first 0) (= rv-hw-ok 1)) "RX failure case starts healthy")
(setq rm-rev-en 1)
(defun gpio-read (pin)
    (if (eq pin 'pin-rx) (exit-error 'injected-rx-failure) host-tx))
; A trapped worker exit wakes the sleeping parent; use elapsed time instead.
(def host-wait-start (systime))
(loopwhile (< (secs-since host-wait-start) 0.3) (sleep 0.01))
(def host-expected-cause 5)
(test-check (> diag-rx-age 0.1) "RX fault snapshot exceeds unchanged watchdog limit")
