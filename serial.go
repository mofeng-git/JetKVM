package kvm

import (
	"bufio"
	"errors"
	"io"
	"strconv"
	"strings"
	"time"

	"github.com/pion/webrtc/v4"
	"go.bug.st/serial"
)

const serialPortPath = "/dev/ttyS3"

var port serial.Port
var ErrSerialUnavailable = errors.New("serial port unavailable")
var ErrATXUnavailable = errors.New("atx control unavailable")

const (
	atxDriverSerial = "serial"
	atxDriverGPIO   = "gpio"
)

var currentATXDriver string

func mountATXControl() error {
	if port == nil {
		return ErrSerialUnavailable
	}
	_ = port.SetMode(defaultMode)
	go runATXControl()

	return nil
}

func unmountATXControl() error {
	_ = reopenSerialPort()
	return nil
}

var (
	ledHDDState bool
	ledPWRState bool
	btnRSTState bool
	btnPWRState bool
)

func configuredATXDriver() string {
	if config == nil || config.ATX == nil {
		return atxDriverSerial
	}
	driver := strings.TrimSpace(strings.ToLower(config.ATX.Driver))
	if driver == atxDriverGPIO {
		return atxDriverGPIO
	}
	return atxDriverSerial
}

func runtimeATXDriver() string {
	if currentATXDriver != "" {
		return currentATXDriver
	}
	return configuredATXDriver()
}

func mountConfiguredATXControl() error {
	driver := configuredATXDriver()
	switch driver {
	case atxDriverGPIO:
		if err := mountGPIOATXControl(); err != nil {
			return err
		}
	default:
		driver = atxDriverSerial
		if err := mountATXControl(); err != nil {
			return err
		}
	}
	currentATXDriver = driver
	return nil
}

func unmountConfiguredATXControl() error {
	driver := currentATXDriver
	if driver == "" {
		driver = configuredATXDriver()
	}
	var err error
	if driver == atxDriverGPIO {
		err = unmountGPIOATXControl()
	} else {
		err = unmountATXControl()
	}
	currentATXDriver = ""
	return err
}

func runATXControl() {
	scopedLogger := serialLogger.With().Str("service", "atx_control").Logger()

	reader := bufio.NewReader(port)
	for {
		line, err := reader.ReadString('\n')
		if err != nil {
			scopedLogger.Warn().Err(err).Msg("Error reading from serial port")
			return
		}

		// Each line should be 4 binary digits + newline
		if len(line) != 5 {
			scopedLogger.Warn().Int("length", len(line)).Msg("Invalid line length")
			continue
		}

		// Parse new states
		newLedHDDState := line[0] == '0'
		newLedPWRState := line[1] == '0'
		newBtnRSTState := line[2] == '1'
		newBtnPWRState := line[3] == '1'

		if currentSession != nil {
			writeJSONRPCEvent("atxState", ATXState{
				Power: newLedPWRState,
				HDD:   newLedHDDState,
			}, currentSession)
		}

		if newLedHDDState != ledHDDState ||
			newLedPWRState != ledPWRState ||
			newBtnRSTState != btnRSTState ||
			newBtnPWRState != btnPWRState {
			scopedLogger.Debug().
				Bool("hdd", newLedHDDState).
				Bool("pwr", newLedPWRState).
				Bool("rst", newBtnRSTState).
				Bool("pwr", newBtnPWRState).
				Msg("Status changed")

			// Update states
			ledHDDState = newLedHDDState
			ledPWRState = newLedPWRState
			btnRSTState = newBtnRSTState
			btnPWRState = newBtnPWRState
		}
	}
}

func pressATXPowerButton(duration time.Duration) error {
	if runtimeATXDriver() == atxDriverGPIO {
		return gpioPressATXPowerButton(duration)
	}
	return serialPressATXPowerButton(duration)
}

func pressATXResetButton(duration time.Duration) error {
	if runtimeATXDriver() == atxDriverGPIO {
		return gpioPressATXResetButton(duration)
	}
	return serialPressATXResetButton(duration)
}

func serialPressATXPowerButton(duration time.Duration) error {
	if port == nil {
		return ErrSerialUnavailable
	}
	_, err := port.Write([]byte("\n"))
	if err != nil {
		return err
	}

	_, err = port.Write([]byte("BTN_PWR_ON\n"))
	if err != nil {
		return err
	}

	time.Sleep(duration)

	_, err = port.Write([]byte("BTN_PWR_OFF\n"))
	if err != nil {
		return err
	}

	return nil
}

func serialPressATXResetButton(duration time.Duration) error {
	if port == nil {
		return ErrSerialUnavailable
	}
	_, err := port.Write([]byte("\n"))
	if err != nil {
		return err
	}

	_, err = port.Write([]byte("BTN_RST_ON\n"))
	if err != nil {
		return err
	}

	time.Sleep(duration)

	_, err = port.Write([]byte("BTN_RST_OFF\n"))
	if err != nil {
		return err
	}

	return nil
}

func mountDCControl() error {
	if port == nil {
		return ErrSerialUnavailable
	}
	_ = port.SetMode(defaultMode)
	registerDCMetrics()
	go runDCControl()
	return nil
}

func unmountDCControl() error {
	_ = reopenSerialPort()
	return nil
}

var dcState DCPowerState

func runDCControl() {
	scopedLogger := serialLogger.With().Str("service", "dc_control").Logger()
	reader := bufio.NewReader(port)
	hasRestoreFeature := false
	for {
		line, err := reader.ReadString('\n')
		if err != nil {
			scopedLogger.Warn().Err(err).Msg("Error reading from serial port")
			return
		}

		// Split the line by semicolon
		parts := strings.Split(strings.TrimSpace(line), ";")
		if len(parts) == 5 {
			scopedLogger.Debug().Str("line", line).Msg("Detected DC extension with restore feature")
			hasRestoreFeature = true
		} else if len(parts) == 4 {
			scopedLogger.Debug().Str("line", line).Msg("Detected DC extension without restore feature")
			hasRestoreFeature = false
		} else {
			scopedLogger.Warn().Str("line", line).Msg("Invalid line")
			continue
		}

		// Parse new states
		powerState, err := strconv.Atoi(parts[0])
		if err != nil {
			scopedLogger.Warn().Err(err).Msg("Invalid power state")
			continue
		}
		dcState.IsOn = powerState == 1
		if hasRestoreFeature {
			restoreState, err := strconv.Atoi(parts[4])
			if err != nil {
				scopedLogger.Warn().Err(err).Msg("Invalid restore state")
				continue
			}
			dcState.RestoreState = restoreState
		} else {
			// -1 means not supported
			dcState.RestoreState = -1
		}
		milliVolts, err := strconv.ParseFloat(parts[1], 64)
		if err != nil {
			scopedLogger.Warn().Err(err).Msg("Invalid voltage")
			continue
		}
		volts := milliVolts / 1000 // Convert mV to V

		milliAmps, err := strconv.ParseFloat(parts[2], 64)
		if err != nil {
			scopedLogger.Warn().Err(err).Msg("Invalid current")
			continue
		}
		amps := milliAmps / 1000 // Convert mA to A

		milliWatts, err := strconv.ParseFloat(parts[3], 64)
		if err != nil {
			scopedLogger.Warn().Err(err).Msg("Invalid power")
			continue
		}
		watts := milliWatts / 1000 // Convert mW to W

		dcState.Voltage = volts
		dcState.Current = amps
		dcState.Power = watts

		// Update Prometheus metrics
		updateDCMetrics(dcState)

		if currentSession != nil {
			writeJSONRPCEvent("dcState", dcState, currentSession)
		}
	}
}

func setDCPowerState(on bool) error {
	if port == nil {
		return ErrSerialUnavailable
	}
	_, err := port.Write([]byte("\n"))
	if err != nil {
		return err
	}
	command := "PWR_OFF\n"
	if on {
		command = "PWR_ON\n"
	}
	_, err = port.Write([]byte(command))
	if err != nil {
		return err
	}
	return nil
}

func setDCRestoreState(state int) error {
	if port == nil {
		return ErrSerialUnavailable
	}
	_, err := port.Write([]byte("\n"))
	if err != nil {
		return err
	}
	command := "RESTORE_MODE_OFF\n"
	switch state {
	case 1:
		command = "RESTORE_MODE_ON\n"
	case 2:
		command = "RESTORE_MODE_LAST_STATE\n"
	}
	_, err = port.Write([]byte(command))
	if err != nil {
		return err
	}
	return nil
}

var defaultMode = &serial.Mode{
	BaudRate: 115200,
	DataBits: 8,
	Parity:   serial.NoParity,
	StopBits: serial.OneStopBit,
}

func initSerialPort() {
	_ = reopenSerialPort()
	switch config.ActiveExtension {
	case "atx-power":
		if err := mountConfiguredATXControl(); err != nil {
			serialLogger.Warn().Err(err).Msg("failed to mount ATX control")
		}
	case "dc-power":
		_ = mountDCControl()
	}
}

func reopenSerialPort() error {
	if port != nil {
		port.Close()
	}
	var err error
	port, err = serial.Open(serialPortPath, defaultMode)
	if err != nil {
		// keep port nil on failure and propagate error
		port = nil
		serialLogger.Error().
			Err(err).
			Str("path", serialPortPath).
			Interface("mode", defaultMode).
			Msg("Error opening serial port")
		return err
	}
	return nil
}

func handleSerialChannel(d *webrtc.DataChannel) {
	scopedLogger := serialLogger.With().
		Uint16("data_channel_id", *d.ID()).Logger()

	d.OnOpen(func() {
		go func() {
			defer func() {
				if r := recover(); r != nil {
					scopedLogger.Error().Interface("panic", r).Msg("Recovered in serial reader loop")
				}
			}()

			// Ensure serial port is available before starting the read loop
			if port == nil {
				_ = reopenSerialPort()
			}
			if port == nil {
				scopedLogger.Warn().Msg("Serial port unavailable; closing serial data channel")
				_ = d.Close()
				return
			}

			buf := make([]byte, 1024)
			for {
				n, err := port.Read(buf)
				if err != nil {
					if err != io.EOF {
						scopedLogger.Warn().Err(err).Msg("Failed to read from serial port")
					}
					break
				}
				err = d.Send(buf[:n])
				if err != nil {
					scopedLogger.Warn().Err(err).Msg("Failed to send serial output")
					break
				}
			}
		}()
	})

	d.OnMessage(func(msg webrtc.DataChannelMessage) {
		if port == nil {
			return
		}
		_, err := port.Write(msg.Data)
		if err != nil {
			scopedLogger.Warn().Err(err).Msg("Failed to write to serial")
		}
	})

	d.OnError(func(err error) {
		scopedLogger.Warn().Err(err).Msg("Serial channel error")
	})

	d.OnClose(func() {
		scopedLogger.Info().Msg("Serial channel closed")
	})
}
