; Explicit host fixtures. No motor, pin, CAN or temperature hardware is accessed.
; Unknown config keys throw, so a newly used firmware API cannot silently pass.
(def host-adc0 0.0)
(def host-adc1 0.0)
(def host-range-ok t)
(def host-adc-type 0)
(def host-tx 1)
(def host-rx 1)
(def host-current 0.0)
(def host-brake 0.0)
(def host-scale 1.0)
(def host-last-send nil)
(def host-min-speed-ok t)
(def host-min-speed-value nil)
(def host-speed 0.0)
(def host-current-kind nil)
(def host-current-delay nil)
(def host-ramp-pos 0.5)
(def host-ramp-neg 0.5)
(defun conf-get (key)
    (cond
        ((eq key 'adc-ctrl-type) host-adc-type)
        ((eq key 'l-current-max) 70.0)
        ((eq key 'l-current-min) -30.0)
        ((eq key 'adc-ramp-time-pos) host-ramp-pos)
        ((eq key 'adc-ramp-time-neg) host-ramp-neg)
        (t (exit-error 'unknown-config-key))))
(defun conf-set (key value)
    (cond
        ((eq key 'l-current-max-scale) (setq host-scale value))
        ((eq key 'min-speed) (if host-min-speed-ok
            (setq host-min-speed-value value) (exit-error 'unsupported)))
        (t (exit-error 'unknown-config-key))))
(defun get-adc-decoded (channel) (if (= channel 0) host-adc0 host-adc1))
(defun app-adc-range-ok () host-range-ok)
(defun app-disable-output (timeout) t)
(defun get-speed () host-speed)
(defun get-rpm () 0.0)
(defun eeprom-read-i (address) nil)
(defun eeprom-store-i (address value) t)
(defun gpio-configure (pin mode) t)
(defun gpio-read (pin) (if (eq pin 'pin-tx) host-tx host-rx))
; Official 6.05 ext_set_current/ext_set_current_rel accept one or two args.
; LispBM closures collect optional arguments via rest-args. This records the
; API command only: delay is not a watchdog and no motor is simulated here.
(defun set-current (value) {
    (if (> (length (rest-args)) 1) (exit-error 'invalid-current-arity))
    (if (rest-args) (setq host-current-delay (rest-args 0)))
    (setq host-current-kind 'absolute)
    (setq host-current value)
})
(defun set-current-rel (value) {
    (if (> (length (rest-args)) 1) (exit-error 'invalid-current-arity))
    (if (rest-args) (setq host-current-delay (rest-args 0)))
    (setq host-current-kind 'relative)
    (setq host-current value)
})
(defun set-brake-rel (value) {
    ; Prior host-current remains historical: this marks the actual last API.
    (setq host-current-kind 'brake)
    (setq host-brake value)
})
(defun throttle-curve (value accel brake mode) value)
(defun event-enable (event) t)
(defun shutdown-hold (held) t)
(defun send-data (data mode destination) {
    (setq host-last-send (bufcreate (buflen data)))
    (bufcpy host-last-send 0 data 0 (buflen data))
    t
})
