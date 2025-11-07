import { useEffect } from "react";

import { SettingsItem } from "@components/SettingsItem";
import { SettingsPageHeader } from "@components/SettingsPageheader";
import { BacklightSettings, useSettingsStore } from "@/hooks/stores";
import { JsonRpcResponse, useJsonRpc } from "@/hooks/useJsonRpc";
import { SelectMenuBasic } from "@components/SelectMenuBasic";
import { UsbDeviceSetting } from "@components/UsbDeviceSetting";

import notifications from "../notifications";
import { UsbInfoSetting } from "../components/UsbInfoSetting";
import { FeatureFlag } from "../components/FeatureFlag";
import { useI18n } from "@/i18n";

export default function SettingsHardwareRoute() {
  const { t } = useI18n();
  const { send } = useJsonRpc();
  const settings = useSettingsStore();
  const { setDisplayRotation } = useSettingsStore();

  const handleDisplayRotationChange = (rotation: string) => {
    setDisplayRotation(rotation);
    handleDisplayRotationSave();
  };

  const handleDisplayRotationSave = () => {
    send("setDisplayRotation", { params: { rotation: settings.displayRotation } }, (resp: JsonRpcResponse) => {
      if ("error" in resp) {
        notifications.error(
          t("settings.hardware.notify.setRotationFail", { reason: resp.error.data || "Unknown error" }),
        );
        return;
      }
      notifications.success(t("settings.hardware.notify.setRotationOk"));
    });
  };

  const { setBacklightSettings } = useSettingsStore();

  const handleBacklightSettingsChange = (settings: BacklightSettings) => {
    // If the user has set the display to dim after it turns off, set the dim_after
    // value to never.
    if (settings.dim_after > settings.off_after && settings.off_after != 0) {
      settings.dim_after = 0;
    }

    setBacklightSettings(settings);
    handleBacklightSettingsSave();
  };

  const handleBacklightSettingsSave = () => {
    send("setBacklightSettings", { params: settings.backlightSettings }, (resp: JsonRpcResponse) => {
      if ("error" in resp) {
        notifications.error(
          t("settings.hardware.notify.setBacklightFail", { reason: resp.error.data || "Unknown error" }),
        );
        return;
      }
      notifications.success(t("settings.hardware.notify.setBacklightOk"));
    });
  };

  useEffect(() => {
    send("getBacklightSettings", {}, (resp: JsonRpcResponse) => {
      if ("error" in resp) {
        return notifications.error(
          t("settings.hardware.notify.getBacklightFail", { reason: resp.error.data || "Unknown error" }),
        );
      }
      const result = resp.result as BacklightSettings;
      setBacklightSettings(result);
    });
  }, [send, setBacklightSettings]);

  return (
    <div className="space-y-4">
      <SettingsPageHeader
        title={t("settings.hardware.title")}
        description={t("settings.hardware.desc")}
      />
      <div className="space-y-4">
        <SettingsItem
          title={t("settings.hardware.rotationTitle")}
          description={t("settings.hardware.rotationDesc")}
        >
          <SelectMenuBasic
            size="SM"
            label=""
            value={settings.displayRotation.toString()}
            options={[
              { value: "270", label: t("settings.hardware.rotation.normal") },
              { value: "90", label: t("settings.hardware.rotation.inverted") },
            ]}
            onChange={e => {
              settings.displayRotation = e.target.value;
              handleDisplayRotationChange(settings.displayRotation);
            }}
          />
        </SettingsItem>
        <SettingsItem
          title={t("settings.hardware.brightnessTitle")}
          description={t("settings.hardware.brightnessDesc")}
        >
          <SelectMenuBasic
            size="SM"
            label=""
            value={settings.backlightSettings.max_brightness.toString()}
            options={[
              { value: "0", label: t("settings.hardware.brightness.off") },
              { value: "10", label: t("settings.hardware.brightness.low") },
              { value: "35", label: t("settings.hardware.brightness.medium") },
              { value: "64", label: t("settings.hardware.brightness.high") },
            ]}
            onChange={e => {
              settings.backlightSettings.max_brightness = parseInt(e.target.value);
              handleBacklightSettingsChange(settings.backlightSettings);
            }}
          />
        </SettingsItem>
        {settings.backlightSettings.max_brightness != 0 && (
          <>
            <SettingsItem
              title={t("settings.hardware.dimTitle")}
              description={t("settings.hardware.dimDesc")}
            >
              <SelectMenuBasic
                size="SM"
                label=""
                value={settings.backlightSettings.dim_after.toString()}
                options={[
                  { value: "0", label: t("settings.hardware.time.never") },
                  { value: "60", label: t("settings.hardware.time.1m") },
                  { value: "300", label: t("settings.hardware.time.5m") },
                  { value: "600", label: t("settings.hardware.time.10m") },
                  { value: "1800", label: t("settings.hardware.time.30m") },
                  { value: "3600", label: t("settings.hardware.time.1h") },
                ]}
                onChange={e => {
                  settings.backlightSettings.dim_after = parseInt(e.target.value);
                  handleBacklightSettingsChange(settings.backlightSettings);
                }}
              />
            </SettingsItem>
            <SettingsItem
              title={t("settings.hardware.offTitle")}
              description={t("settings.hardware.offDesc")}
            >
              <SelectMenuBasic
                size="SM"
                label=""
                value={settings.backlightSettings.off_after.toString()}
                options={[
                  { value: "0", label: t("settings.hardware.time.never") },
                  { value: "300", label: t("settings.hardware.time.5m") },
                  { value: "600", label: t("settings.hardware.time.10m") },
                  { value: "1800", label: t("settings.hardware.time.30m") },
                  { value: "3600", label: t("settings.hardware.time.1h") },
                ]}
                onChange={e => {
                  settings.backlightSettings.off_after = parseInt(e.target.value);
                  handleBacklightSettingsChange(settings.backlightSettings);
                }}
              />
            </SettingsItem>
          </>
        )}
        <p className="text-xs text-slate-600 dark:text-slate-400">
          {t("settings.hardware.wakeInfo")}
        </p>
      </div>

      <FeatureFlag minAppVersion="0.3.8">
        <UsbDeviceSetting />
      </FeatureFlag>

      <FeatureFlag minAppVersion="0.3.8">
        <UsbInfoSetting />
      </FeatureFlag>
    </div>
  );
}
