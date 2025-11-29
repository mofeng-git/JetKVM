package kvm

import (
	"context"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"time"
)

const (
	gpioSysfsBasePath = "/sys/class/gpio"
	invalidGPIOPin    = -1
)

var (
	gpioATXControl *gpioATXDriver
	atxGPIOLogger  = serialLogger.With().Str("service", "atx_gpio").Logger()
)

type gpioATXDriver struct {
	cfg         *ATXGPIOConfig
	powerOutPin int
	resetOutPin int
	powerLedPin int
	hddLedPin   int
	activeHigh  bool
	cancel      context.CancelFunc
	wg          sync.WaitGroup
	mu          sync.Mutex
}

func mountGPIOATXControl() error {
	if config == nil || config.ATX == nil || config.ATX.GPIO == nil {
		return fmt.Errorf("gpio control not configured")
	}
	if gpioATXControl != nil {
		return nil
	}

	driver, err := newGPIOATXDriver(config.ATX.GPIO)
	if err != nil {
		return err
	}
	gpioATXControl = driver
	driver.start()
	atxGPIOLogger.Info().Msg("gpio ATX control mounted")
	return nil
}

func unmountGPIOATXControl() error {
	if gpioATXControl == nil {
		return nil
	}
	gpioATXControl.stop()
	gpioATXControl = nil
	atxGPIOLogger.Info().Msg("gpio ATX control unmounted")
	return nil
}

func gpioPressATXPowerButton(duration time.Duration) error {
	if gpioATXControl == nil {
		return ErrATXUnavailable
	}
	return gpioATXControl.press(gpioATXControl.powerOutPin, duration)
}

func gpioPressATXResetButton(duration time.Duration) error {
	if gpioATXControl == nil {
		return ErrATXUnavailable
	}
	return gpioATXControl.press(gpioATXControl.resetOutPin, duration)
}

func newGPIOATXDriver(cfg *ATXGPIOConfig) (*gpioATXDriver, error) {
	if cfg == nil {
		return nil, fmt.Errorf("gpio config missing")
	}
	driver := &gpioATXDriver{cfg: cfg}
	driver.activeHigh = cfg.OutputActiveHigh

	var err error
	driver.powerOutPin, err = parseGPIOPin(cfg.PowerButtonPin)
	if err != nil {
		return nil, err
	}
	driver.resetOutPin, err = parseGPIOPin(cfg.ResetButtonPin)
	if err != nil {
		return nil, err
	}
	driver.powerLedPin, err = parseGPIOPin(cfg.PowerLedPin)
	if err != nil {
		return nil, err
	}
	driver.hddLedPin, err = parseGPIOPin(cfg.HddLedPin)
	if err != nil {
		return nil, err
	}

	if err := driver.initPins(); err != nil {
		return nil, err
	}

	return driver, nil
}

func (g *gpioATXDriver) initPins() error {
	if err := g.prepareOutput(g.powerOutPin); err != nil {
		return err
	}
	if err := g.prepareOutput(g.resetOutPin); err != nil {
		return err
	}
	if err := g.prepareInput(g.powerLedPin); err != nil {
		return err
	}
	if err := g.prepareInput(g.hddLedPin); err != nil {
		return err
	}
	return nil
}

func (g *gpioATXDriver) prepareOutput(pin int) error {
	if pin == invalidGPIOPin {
		return nil
	}
	if err := exportGPIOPin(pin); err != nil {
		return fmt.Errorf("export gpio%d: %w", pin, err)
	}
	if err := setGPIODirection(pin, "out"); err != nil {
		return fmt.Errorf("set gpio%d direction: %w", pin, err)
	}
	if err := g.writeOutput(pin, false); err != nil {
		return err
	}
	return nil
}

func (g *gpioATXDriver) prepareInput(pin int) error {
	if pin == invalidGPIOPin {
		return nil
	}
	if err := exportGPIOPin(pin); err != nil {
		return fmt.Errorf("export gpio%d: %w", pin, err)
	}
	if err := setGPIODirection(pin, "in"); err != nil {
		return fmt.Errorf("set gpio%d direction: %w", pin, err)
	}
	return nil
}

func (g *gpioATXDriver) start() {
	ctx, cancel := context.WithCancel(context.Background())
	g.cancel = cancel

	if g.powerLedPin != invalidGPIOPin || g.hddLedPin != invalidGPIOPin {
		g.refreshInputs()
		g.wg.Add(1)
		go g.monitorInputs(ctx)
	}
}

func (g *gpioATXDriver) stop() {
	if g.cancel != nil {
		g.cancel()
	}
	g.wg.Wait()
	g.cleanup()
}

func (g *gpioATXDriver) cleanup() {
	pins := []int{g.powerOutPin, g.resetOutPin, g.powerLedPin, g.hddLedPin}
	seen := map[int]struct{}{}
	for _, pin := range pins {
		if pin == invalidGPIOPin {
			continue
		}
		if _, ok := seen[pin]; ok {
			continue
		}
		_ = unexportGPIOPin(pin)
		seen[pin] = struct{}{}
	}
}

func (g *gpioATXDriver) monitorInputs(ctx context.Context) {
	ticker := time.NewTicker(200 * time.Millisecond)
	defer func() {
		ticker.Stop()
		g.wg.Done()
	}()

	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			g.refreshInputs()
		}
	}
}

func (g *gpioATXDriver) refreshInputs() {
	changed := false
	if g.powerLedPin != invalidGPIOPin {
		if val, err := readGPIOValue(g.powerLedPin); err == nil {
			if val != ledPWRState {
				ledPWRState = val
				changed = true
			}
		} else {
			atxGPIOLogger.Warn().Err(err).Int("pin", g.powerLedPin).Msg("failed to read power LED gpio")
		}
	}

	if g.hddLedPin != invalidGPIOPin {
		if val, err := readGPIOValue(g.hddLedPin); err == nil {
			if val != ledHDDState {
				ledHDDState = val
				changed = true
			}
		} else {
			atxGPIOLogger.Warn().Err(err).Int("pin", g.hddLedPin).Msg("failed to read HDD LED gpio")
		}
	}

	if changed && currentSession != nil {
		writeJSONRPCEvent("atxState", ATXState{
			Power: ledPWRState,
			HDD:   ledHDDState,
		}, currentSession)
	}
}

func (g *gpioATXDriver) press(pin int, duration time.Duration) error {
	if pin == invalidGPIOPin {
		return ErrATXUnavailable
	}
	g.mu.Lock()
	defer g.mu.Unlock()

	if err := g.writeOutput(pin, true); err != nil {
		return err
	}
	time.Sleep(duration)
	return g.writeOutput(pin, false)
}

func (g *gpioATXDriver) writeOutput(pin int, pressed bool) error {
	level := !g.activeHigh
	if pressed {
		level = g.activeHigh
	}
	return writeGPIOValue(pin, level)
}

func parseGPIOPin(value string) (int, error) {
	value = strings.TrimSpace(value)
	if value == "" {
		return invalidGPIOPin, nil
	}
	number, err := strconv.Atoi(value)
	if err != nil {
		return invalidGPIOPin, fmt.Errorf("invalid gpio pin value %q: %w", value, err)
	}
	if number < 0 {
		return invalidGPIOPin, fmt.Errorf("gpio pin must be >= 0: %d", number)
	}
	return number, nil
}

func exportGPIOPin(pin int) error {
	path := gpioPinPath(pin, "")
	if _, err := os.Stat(path); err == nil {
		return nil
	}
	exportPath := filepath.Join(gpioSysfsBasePath, "export")
	if err := os.WriteFile(exportPath, []byte(strconv.Itoa(pin)), 0o644); err != nil {
		return err
	}
	deadline := time.Now().Add(500 * time.Millisecond)
	for {
		if _, err := os.Stat(path); err == nil {
			return nil
		}
		if time.Now().After(deadline) {
			return fmt.Errorf("gpio%d did not appear after export", pin)
		}
		time.Sleep(10 * time.Millisecond)
	}
}

func unexportGPIOPin(pin int) error {
	unexportPath := filepath.Join(gpioSysfsBasePath, "unexport")
	return os.WriteFile(unexportPath, []byte(strconv.Itoa(pin)), 0o644)
}

func setGPIODirection(pin int, direction string) error {
	path := gpioPinPath(pin, "direction")
	return os.WriteFile(path, []byte(direction), 0o644)
}

func writeGPIOValue(pin int, high bool) error {
	path := gpioPinPath(pin, "value")
	value := "0"
	if high {
		value = "1"
	}
	return os.WriteFile(path, []byte(value), 0o644)
}

func readGPIOValue(pin int) (bool, error) {
	path := gpioPinPath(pin, "value")
	data, err := os.ReadFile(path)
	if err != nil {
		return false, err
	}
	trimmed := strings.TrimSpace(string(data))
	return trimmed == "1", nil
}

func gpioPinPath(pin int, file string) string {
	base := filepath.Join(gpioSysfsBasePath, fmt.Sprintf("gpio%d", pin))
	if file == "" {
		return base
	}
	return filepath.Join(base, file)
}
