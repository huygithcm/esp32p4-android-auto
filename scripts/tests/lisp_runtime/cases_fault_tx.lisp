; Fail only the TX GPIO fixture after its worker has produced a heartbeat.
; Actual motor-loop watchdog must identify the stale worker (>100 ms).
(sleep 0.2)
(test-check (and (= diag-first 0) (= tx-live 1)) "TX failure case starts healthy")
(defun gpio-read (pin)
    (if (eq pin 'pin-tx) (exit-error 'injected-tx-failure) host-rx))
; spawn-trap's exit message wakes the parent out of sleep in upstream 6.05.
; Wait against elapsed time so the watchdog really gets its 100 ms window.
(def host-wait-start (systime))
(loopwhile (< (secs-since host-wait-start) 0.3) (sleep 0.01))
(def host-expected-cause 4)
(test-check (> diag-tx-age 0.1) "TX fault snapshot exceeds unchanged watchdog limit")
