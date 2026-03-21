# ComponentCtrl

ARM64 Win32 control app for the AW22XXX LED driver.

## Features
- Enumerates the `GUID_DEVINTERFACE_AW22XXX_LED` device interface
- Reads the driver config table and lists available configs by name in a dropdown
- Applies the selected config through `IOCTL_AW22XXX_APPLY_CONFIG`
- Toggles RGB override for configs that support it
- Refreshes device information and can send `IOCTL_AW22XXX_LED_OFF`

## Build
- Run `build_arm64.cmd` in this directory.
- Output: `x64` is not used for this workflow; the intended build is `ARM64`.

## Runtime
- The AW22XXX driver persists the last selected config.
- If no config has been selected before, the driver restores `fan_led_on` by default.
