package usbgadget

import (
	"fmt"
	"os"
	"path"
	"path/filepath"
	"sort"
	"strings"
)

// getUdcs returns a sorted list of UDC names discovered from sysfs.
func getUdcs() []string {
	var udcs []string

	// Prefer the canonical sysfs class for UDCs, which is widely available.
	if entries, err := os.ReadDir("/sys/class/udc"); err == nil {
		for _, e := range entries {
			name := strings.TrimSpace(e.Name())
			if name == "" || name == "." || name == ".." {
				continue
			}
			udcs = append(udcs, name)
		}
	}

	// Fallback for some platforms exposing UDCs under usbdrd path.
	if len(udcs) == 0 {
		if entries, err := os.ReadDir("/sys/devices/platform/usbdrd"); err == nil {
			for _, e := range entries {
				if !e.IsDir() || !strings.HasSuffix(e.Name(), ".usb") {
					continue
				}
				udcs = append(udcs, e.Name())
			}
		}
	}

	if len(udcs) > 1 {
		sort.Strings(udcs)
	}
	return udcs
}

// resolveUDCDriverPath resolves the driver directory for a given UDC name.
// It follows the symlink at /sys/class/udc/<udc>/device/driver and returns
// an absolute path to the driver directory where bind/unbind exist.
func resolveUDCDriverPath(udc string) (string, error) {
	link := filepath.Join("/sys/class/udc", udc, "device", "driver")
	resolved, err := filepath.EvalSymlinks(link)
	if err != nil || resolved == "" {
		return "", fmt.Errorf("unable to resolve driver path for UDC %s: %w", udc, err)
	}
	return resolved, nil
}

func rebindUsb(driverPath, udc string, ignoreUnbindError bool) error {
	if driverPath == "" {
		return fmt.Errorf("driverPath is empty")
	}
	if err := os.WriteFile(path.Join(driverPath, "unbind"), []byte(udc), 0644); err != nil && !ignoreUnbindError {
		return err
	}
	if err := os.WriteFile(path.Join(driverPath, "bind"), []byte(udc), 0644); err != nil {
		return err
	}
	return nil
}

func (u *UsbGadget) rebindUsb(ignoreUnbindError bool) error {
	u.log.Info().Str("udc", u.udc).Str("driver_path", u.udcDriverPath).Msg("rebinding USB gadget to UDC")
	return rebindUsb(u.udcDriverPath, u.udc, ignoreUnbindError)
}

// RebindUsb rebinds the USB gadget to the UDC.
func (u *UsbGadget) RebindUsb(ignoreUnbindError bool) error {
	u.configLock.Lock()
	defer u.configLock.Unlock()

	return u.rebindUsb(ignoreUnbindError)
}

// GetUsbState returns the current state of the USB gadget.
func (u *UsbGadget) GetUsbState() (state string) {
	stateFile := path.Join("/sys/class/udc", u.udc, "state")
	stateBytes, err := os.ReadFile(stateFile)
	if err != nil {
		if os.IsNotExist(err) {
			return "not attached"
		}
		u.log.Trace().Err(err).Msg("failed to read usb state")
		return "unknown"
	}
	return strings.TrimSpace(string(stateBytes))
}

// IsUDCBound checks if the UDC is currently bound (driver sees the UDC name).
func (u *UsbGadget) IsUDCBound() (bool, error) {
	if u.udcDriverPath == "" || u.udc == "" {
		return false, nil
	}
	udcFilePath := path.Join(u.udcDriverPath, u.udc)
	_, err := os.Stat(udcFilePath)
	if err != nil {
		if os.IsNotExist(err) {
			return false, nil
		}
		return false, fmt.Errorf("error checking USB emulation state: %w", err)
	}
	return true, nil
}

// BindUDC binds the gadget to the selected UDC.
func (u *UsbGadget) BindUDC() error {
	if u.udcDriverPath == "" {
		return fmt.Errorf("UDC driver path not set")
	}
	if err := os.WriteFile(path.Join(u.udcDriverPath, "bind"), []byte(u.udc), 0644); err != nil {
		return fmt.Errorf("error binding UDC: %w", err)
	}
	return nil
}

// UnbindUDC unbinds the gadget from the UDC.
func (u *UsbGadget) UnbindUDC() error {
	if u.udcDriverPath == "" {
		return fmt.Errorf("UDC driver path not set")
	}
	if err := os.WriteFile(path.Join(u.udcDriverPath, "unbind"), []byte(u.udc), 0644); err != nil {
		return fmt.Errorf("error unbinding UDC: %w", err)
	}
	return nil
}
