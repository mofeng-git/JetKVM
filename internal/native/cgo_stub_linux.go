//go:build linux && !cgo

package native

// no-op stubs to allow building on Linux without CGO/native libs

func setUpNativeHandlers()                     {}
func uiInit(rotation uint16)                   {}
func uiTick()                                  {}
func uiSetVar(name, value string)              {}
func uiGetVar(name string) string              { return "" }
func uiSwitchToScreen(screen string)           {}
func uiGetCurrentScreen() string               { return "" }
func uiEventCodeToName(code int) string        { return "" }
func uiGetLVGLVersion() string                 { return "" }
func uiObjHide(string) (bool, error)           { return false, nil }
func uiObjShow(string) (bool, error)           { return false, nil }
func uiObjAddState(string, string) (bool, error)   { return false, nil }
func uiObjClearState(string, string) (bool, error) { return false, nil }
func uiObjAddFlag(string, string) (bool, error)    { return false, nil }
func uiObjClearFlag(string, string) (bool, error)  { return false, nil }
func uiObjSetOpacity(string, int) (bool, error)    { return false, nil }
func uiObjFadeIn(string, uint32) (bool, error)     { return false, nil }
func uiObjFadeOut(string, uint32) (bool, error)    { return false, nil }
func uiLabelSetText(string, string) (bool, error)  { return false, nil }
func uiImgSetSrc(string, string) (bool, error)     { return false, nil }
func uiDispSetRotation(uint16) (bool, error)       { return true, nil }

func videoInit() error                    { return nil }
func videoShutdown()                      {}
func videoStart()                         {}
func videoStop()                          {}
func videoLogStatus() string              { return "" }
func videoGetStreamQualityFactor() (float64, error) { return 0, nil }
func videoSetStreamQualityFactor(float64) error     { return nil }
func videoGetEDID() (string, error)      { return "", nil }
func videoSetEDID(string) error          { return nil }

func crash() {}

