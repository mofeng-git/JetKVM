
import { useState , useEffect } from "react";

import { SettingsItem } from "@components/SettingsItem";
import { JsonRpcResponse, useJsonRpc } from "@/hooks/useJsonRpc";

import { SettingsPageHeader } from "../components/SettingsPageheader";
import { Button } from "../components/Button";
import notifications from "../notifications";
import Checkbox from "../components/Checkbox";
import { useDeviceUiNavigation } from "../hooks/useAppNavigation";
import { useDeviceStore } from "../hooks/stores";
import { useI18n } from "@/i18n";


export default function SettingsGeneralRoute() {
  const { send } = useJsonRpc();
  const { navigateTo } = useDeviceUiNavigation();
  const [autoUpdate, setAutoUpdate] = useState(true);
  const { t } = useI18n();

  const currentVersions = useDeviceStore(state => {
    const { appVersion, systemVersion } = state;
    if (!appVersion || !systemVersion) return null;
    return { appVersion, systemVersion };
  });

  useEffect(() => {
    send("getAutoUpdateState", {}, (resp: JsonRpcResponse) => {
      if ("error" in resp) return;
      setAutoUpdate(resp.result as boolean);
    });
  }, [send]);

  const handleAutoUpdateChange = (enabled: boolean) => {
    send("setAutoUpdateState", { enabled }, (resp: JsonRpcResponse) => {
      if ("error" in resp) {
        notifications.error(
          t("settings.general.setAutoUpdateFailed", {
            reason: resp.error.data || "Unknown error",
          }),
        );
        return;
      }
      setAutoUpdate(enabled);
    });
  };

  return (
    <div className="space-y-4">
      <SettingsPageHeader
        title={t("settings.general.title")}
        description={t("settings.general.desc")}
      />

      <div className="space-y-4">
        <div className="space-y-4 pb-2">
          <div className="mt-2 flex items-center justify-between gap-x-2">
            <SettingsItem
              title={t("settings.general.checkUpdates")}
              description={
                currentVersions ? (
                  <>
                    {t("settings.general.app")}: {currentVersions.appVersion}
                    <br />
                    {t("settings.general.system")}: {currentVersions.systemVersion}
                  </>
                ) : (
                  <>
                    {t("settings.general.app")}: {t("settings.general.loading")}
                    <br />
                    {t("settings.general.system")}: {t("settings.general.loading")}
                  </>
                )
              }
            />
            <div>
              <Button
                size="SM"
                theme="light"
                text={t("settings.general.checkUpdates")}
                onClick={() => navigateTo("./update")}
              />
            </div>
          </div>
          <div className="space-y-4">
            <SettingsItem
              title={t("settings.general.autoUpdate")}
              description={t("settings.general.autoUpdateDesc")}
            >
              <Checkbox
                checked={autoUpdate}
                onChange={e => {
                  handleAutoUpdateChange(e.target.checked);
                }}
              />
            </SettingsItem>
          </div>

          <div className="mt-2 flex items-center justify-between gap-x-2">
            <SettingsItem
              title={t("settings.general.reboot")}
              description={t("settings.general.rebootDesc")}
            />
            <div>
              <Button
                size="SM"
                theme="light"
                text={t("settings.general.reboot")}
                onClick={() => navigateTo("./reboot")}
              />
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}
