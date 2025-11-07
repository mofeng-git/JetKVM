import { LuPower } from "react-icons/lu";
import { useCallback, useEffect, useState } from "react";

import { Button } from "@components/Button";
import Card from "@components/Card";
import { SettingsPageHeader } from "@components/SettingsPageheader";
import { JsonRpcResponse, useJsonRpc } from "@/hooks/useJsonRpc";
import notifications from "@/notifications";
import FieldLabel from "@components/FieldLabel";
import LoadingSpinner from "@components/LoadingSpinner";
import {SelectMenuBasic} from "@components/SelectMenuBasic";
import { useI18n } from "@/i18n";

interface DCPowerState {
  isOn: boolean;
  voltage: number;
  current: number;
  power: number;
  restoreState: number;
}

export function DCPowerControl() {
  const { t } = useI18n();
  const { send } = useJsonRpc();
  const [powerState, setPowerState] = useState<DCPowerState | null>(null);

  const getDCPowerState = useCallback(() => {
    send("getDCPowerState", {}, (resp: JsonRpcResponse) => {
      if ("error" in resp) {
        notifications.error(
          t("extensions.dc.notify.getStateFail", { reason: resp.error.data || t("extensions.common.unknown") }),
        );
        return;
      }
      setPowerState(resp.result as DCPowerState);
    });
  }, [send]);

  const handlePowerToggle = (enabled: boolean) => {
    send("setDCPowerState", { enabled }, (resp: JsonRpcResponse) => {
      if ("error" in resp) {
        notifications.error(
          t("extensions.dc.notify.setStateFail", { reason: resp.error.data || t("extensions.common.unknown") }),
        );
        return;
      }
      getDCPowerState(); // Refresh state after change
    });
  };
  const handleRestoreChange = (state: number) => {
    // const state = powerState?.restoreState === 0 ? 1 : powerState?.restoreState === 1 ? 2 : 0;
    send("setDCRestoreState", { state }, (resp: JsonRpcResponse) => {
      if ("error" in resp) {
        notifications.error(
          t("extensions.dc.notify.setRestoreFail", { reason: resp.error.data || t("extensions.common.unknown") }),
        );
        return;
      }
      getDCPowerState(); // Refresh state after change
    });
  };



  useEffect(() => {
    getDCPowerState();
    // Set up polling interval to update status
    const interval = setInterval(getDCPowerState, 1000);
    return () => clearInterval(interval);
  }, [getDCPowerState]);

  return (
    <div className="space-y-4">
      <SettingsPageHeader
        title={t("extensions.dc.title")}
        description={t("extensions.dc.desc")}
      />

      {powerState === null ? (
        <Card className="flex h-[160px] justify-center p-3">
          <LoadingSpinner className="h-6 w-6 text-blue-500 dark:text-blue-400" />
        </Card>
      ) : (
        <Card className="animate-fadeIn opacity-0">
          <div className="space-y-4 p-3">
            {/* Power Controls */}
            <div className="flex items-center space-x-2">
              <Button
                size="SM"
                theme="light"
                LeadingIcon={LuPower}
                text={t("extensions.dc.powerOn")}
                onClick={() => handlePowerToggle(true)}
                disabled={powerState.isOn}
              />
              <Button
                size="SM"
                theme="light"
                LeadingIcon={LuPower}
                text={t("extensions.dc.powerOff")}
                disabled={!powerState.isOn}
                onClick={() => handlePowerToggle(false)}
              />
            </div>
            {powerState.restoreState > -1 ? (
              <div className="flex items-center">
                <SelectMenuBasic
                    size="SM"
                    label={t("extensions.dc.restoreLabel")}
                    value={powerState.restoreState}
                    onChange={e => handleRestoreChange(parseInt(e.target.value))}
                    options={[
                      { value: '0', label: t("extensions.dc.restore.off") },
                      { value: '1', label: t("extensions.dc.restore.on") },
                      { value: '2', label: t("extensions.dc.restore.last") },
                    ]}
                />
              </div>
            ) : null}
            <hr className="border-slate-700/30 dark:border-slate-600/30" />

            {/* Status Display */}
            <div className="grid grid-cols-3 gap-4">
              <div className="space-y-1">
                <FieldLabel label={t("extensions.dc.voltage")} />
                <p className="text-sm font-medium text-slate-900 dark:text-slate-100">
                  {powerState.voltage.toFixed(1)}V
                </p>
              </div>
              <div className="space-y-1">
                <FieldLabel label={t("extensions.dc.current")} />
                <p className="text-sm font-medium text-slate-900 dark:text-slate-100">
                  {powerState.current.toFixed(1)}A
                </p>
              </div>
              <div className="space-y-1">
                <FieldLabel label={t("extensions.dc.power")} />
                <p className="text-sm font-medium text-slate-900 dark:text-slate-100">
                  {powerState.power.toFixed(1)}W
                </p>
              </div>
            </div>
          </div>
        </Card>
      )}
    </div>
  );
}
