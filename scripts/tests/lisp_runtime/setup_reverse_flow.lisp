; Valid persisted reverse-enabled ride configuration; no real ESC writes.
(defun eeprom-read-i (address)
    (cond ((= address 16) 0x524D02) ((= address 17) 1)
          ((= address 18) 2301) ((= address 20) 500)
          ((= address 21) 700) ((= address 22) 1000)
          ((= address 23) 1) ((= address 24) 30)
          ((= address 25) 70) (t nil)))
