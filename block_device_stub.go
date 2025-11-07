//go:build !cgo

package kvm

import "fmt"

// Minimal stub to allow building without importing go-nbd when CGO is disabled.

type NBDDevice struct{}

func NewNBDDevice() *NBDDevice { return &NBDDevice{} }

func (d *NBDDevice) Start() error {
    return fmt.Errorf("nbd is not supported when CGO is disabled")
}

func (d *NBDDevice) Close() {}

