package usbgadget

import "time"

// hidWriteTimeout controls the write timeout for HID events.
const hidWriteTimeout = 10 * time.Millisecond
