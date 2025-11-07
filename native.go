package kvm

import (
	"fmt"
	"os"
	"sync"
	"time"

	"github.com/Masterminds/semver/v3"
	"github.com/jetkvm/kvm/internal/native"
	"github.com/pion/webrtc/v4/pkg/media"
)

var (
	nativeInstance *native.Native
	nativeCmdLock  = sync.Mutex{}
)

func initNative(systemVersion *semver.Version, appVersion *semver.Version) {
    // Apply video config via environment variables for the native layer
    // This keeps C code simple and avoids JSON plumbing across cgo.
    if config != nil && config.Video != nil {
        // Select backend: rv1106 (default) or uvc
        if config.Video.Backend != "" {
            _ = os.Setenv("JETKVM_VIDEO_BACKEND", config.Video.Backend)
        }
        if config.Video.Backend == "uvc" {
            if config.Video.Device != "" {
                _ = os.Setenv("JETKVM_UVC_DEVICE", config.Video.Device)
            }
            if config.Video.Width > 0 {
                _ = os.Setenv("JETKVM_UVC_WIDTH", fmt.Sprintf("%d", config.Video.Width))
            }
            if config.Video.Height > 0 {
                _ = os.Setenv("JETKVM_UVC_HEIGHT", fmt.Sprintf("%d", config.Video.Height))
            }
            if config.Video.FPS > 0 {
                _ = os.Setenv("JETKVM_UVC_FPS", fmt.Sprintf("%d", config.Video.FPS))
            }
            if config.Video.Format != "" {
                _ = os.Setenv("JETKVM_UVC_FORMAT", config.Video.Format)
            }
            if config.Video.Encoder != "" {
                _ = os.Setenv("JETKVM_ENCODER", config.Video.Encoder)
            }
            if config.Video.BitrateKbps > 0 {
                _ = os.Setenv("JETKVM_BITRATE_KBPS", fmt.Sprintf("%d", config.Video.BitrateKbps))
            }
            if config.Video.Keyint > 0 {
                _ = os.Setenv("JETKVM_KEYINT", fmt.Sprintf("%d", config.Video.Keyint))
            }
            _ = os.Setenv("JETKVM_REPEAT_HEADERS", map[bool]string{true: "1", false: "0"}[config.Video.RepeatHeaders])
            // Optional tuning for x264; safe defaults
            _ = os.Setenv("JETKVM_X264_PRESET", "ultrafast")
            _ = os.Setenv("JETKVM_X264_TUNE", "zerolatency")
            _ = os.Setenv("JETKVM_X264_PROFILE", "baseline")
        }
    }

    nativeInstance = native.NewNative(native.NativeOptions{
        SystemVersion:   systemVersion,
        AppVersion:      appVersion,
        DisplayRotation: config.GetDisplayRotation(),
		OnVideoStateChange: func(state native.VideoState) {
			lastVideoState = state
			triggerVideoStateUpdate()
			requestDisplayUpdate(true, "video_state_changed")
		},
		OnIndevEvent: func(event string) {
			nativeLogger.Trace().Str("event", event).Msg("indev event received")
			wakeDisplay(false, "indev_event")
		},
		OnRpcEvent: func(event string) {
			nativeCmdLock.Lock()
			defer nativeCmdLock.Unlock()

			nativeLogger.Trace().Str("event", event).Msg("rpc event received")
			switch event {
			case "resetConfig":
				err := rpcResetConfig()
				if err != nil {
					nativeLogger.Warn().Err(err).Msg("error resetting config")
				}
				_ = rpcReboot(true)
			case "reboot":
				_ = rpcReboot(true)
			default:
				nativeLogger.Warn().Str("event", event).Msg("unknown rpc event received")
			}
		},
		OnVideoFrameReceived: func(frame []byte, duration time.Duration) {
			if currentSession != nil {
				err := currentSession.VideoTrack.WriteSample(media.Sample{Data: frame, Duration: duration})
				if err != nil {
					nativeLogger.Warn().Err(err).Msg("error writing sample")
				}
			}
		},
	})
	nativeInstance.Start()

	if os.Getenv("JETKVM_CRASH_TESTING") == "1" {
		nativeInstance.DoNotUseThisIsForCrashTestingOnly()
	}
}
