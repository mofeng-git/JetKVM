package main

import (
    "encoding/json"
    "fmt"
    "os"
    "strconv"
    "strings"
    "golang.org/x/crypto/bcrypt"
)

type VideoConfig struct {
    Backend       string `json:"backend"`
    Device        string `json:"device"`
    Width         int    `json:"width"`
    Height        int    `json:"height"`
    FPS           int    `json:"fps"`
    Format        string `json:"format"`
    Encoder       string `json:"encoder"`
    BitrateKbps   int    `json:"bitrate_kbps"`
    Keyint        int    `json:"keyint"`
    RepeatHeaders bool   `json:"repeat_headers"`
}

type Config struct {
    LocalAuthMode      string       `json:"localAuthMode"`
    HashedPassword     string       `json:"hashed_password"`
    DefaultLogLevel    string       `json:"default_log_level"`
    VideoSleepAfterSec int          `json:"video_sleep_after_sec"`
    Video              *VideoConfig `json:"video"`
}

func getenvDefault(name, def string) string {
    v := os.Getenv(name)
    if v == "" {
        return def
    }
    return v
}

func atoiOr(s string, def int) int {
    if s == "" {
        return def
    }
    if v, err := strconv.Atoi(s); err == nil {
        return v
    }
    return def
}

func main() {
    if len(os.Args) < 2 {
        fmt.Fprintf(os.Stderr, "usage: %s <config-path>\n", os.Args[0])
        os.Exit(2)
    }
    path := os.Args[1]
    bs, err := os.ReadFile(path)
    if err != nil {
        fmt.Fprintf(os.Stderr, "read config: %v\n", err)
        os.Exit(1)
    }
    var cfg Config
    if err := json.Unmarshal(bs, &cfg); err != nil {
        fmt.Fprintf(os.Stderr, "parse config: %v\n", err)
        os.Exit(1)
    }
    if cfg.Video == nil {
        cfg.Video = &VideoConfig{}
    }

    // PASSWORD: if set, enable password auth and store bcrypt hash
    if pw := os.Getenv("PASSWORD"); pw != "" {
        // allow special value "admin" default but still hash whatever provided
        hash, err := bcrypt.GenerateFromPassword([]byte(pw), bcrypt.DefaultCost)
        if err != nil {
            fmt.Fprintf(os.Stderr, "bcrypt error: %v\n", err)
            os.Exit(1)
        }
        cfg.LocalAuthMode = "password"
        cfg.HashedPassword = string(hash)
    }

    // VIDEONUM -> /dev/videoN
    if vn := os.Getenv("VIDEONUM"); vn != "" {
        if _, err := strconv.Atoi(vn); err == nil {
            cfg.Video.Device = "/dev/video" + vn
        }
    }

    // VIDEOFORMAT: mjpeg|yuyv -> MJPG|YUYV
    if vf := os.Getenv("VIDEOFORMAT"); vf != "" {
        m := strings.ToLower(vf)
        switch m {
        case "mjpeg", "mjpg":
            cfg.Video.Format = "MJPG"
        case "yuyv", "yuy2":
            cfg.Video.Format = "YUYV"
        }
    }

    // Optional overrides: resolution/fps/bitrate
    if w := os.Getenv("VIDEOWIDTH"); w != "" { cfg.Video.Width = atoiOr(w, cfg.Video.Width) }
    if h := os.Getenv("VIDEOHEIGHT"); h != "" { cfg.Video.Height = atoiOr(h, cfg.Video.Height) }
    if f := os.Getenv("VIDEOFPS"); f != "" { cfg.Video.FPS = atoiOr(f, cfg.Video.FPS) }
    if b := os.Getenv("VIDEOBITRATE"); b != "" { cfg.Video.BitrateKbps = atoiOr(b, cfg.Video.BitrateKbps) }

    // Enforce backend/encoder defaults when missing
    if cfg.Video.Backend == "" { cfg.Video.Backend = "uvc" }
    if cfg.Video.Encoder == "" { cfg.Video.Encoder = "x264" }
    if cfg.Video.Format == "" { cfg.Video.Format = "MJPG" }

    out, err := json.MarshalIndent(&cfg, "", "  ")
    if err != nil {
        fmt.Fprintf(os.Stderr, "marshal config: %v\n", err)
        os.Exit(1)
    }
    if err := os.WriteFile(path, out, 0644); err != nil {
        fmt.Fprintf(os.Stderr, "write config: %v\n", err)
        os.Exit(1)
    }
}

