; Kill the actual motor worker at current-scale refresh, leaving its independent
; supervisor to latch the fault. Other workers retain the original config API.
(sleep 0.2)
(test-check (and (= diag-first 0) (= motor-live 1)) "motor failure case starts healthy")
(def host-original-conf-get conf-get)
(defun conf-get (key)
    (if (eq key 'l-current-max) (exit-error 'injected-motor-failure)
        (host-original-conf-get key)))
; A trapped worker exit wakes the sleeping parent; use elapsed time instead.
(def host-wait-start (systime))
(loopwhile (< (secs-since host-wait-start) 0.3) (sleep 0.01))
; The worker remains dead; restore this shared API before querying the panel.
(setq conf-get host-original-conf-get)
(def host-expected-cause 6)
(test-check (> diag-motor-age 0.1) "motor snapshot exceeds unchanged supervisor limit")
