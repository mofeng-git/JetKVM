//go:build cgo

package main

import "github.com/erikdubbelboer/gspt"

func setProcTitle(s string) { gspt.SetProcTitle(s) }

