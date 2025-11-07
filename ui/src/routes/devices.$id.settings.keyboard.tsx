import { useCallback, useEffect } from "react";

import { useSettingsStore } from "@/hooks/stores";
import { JsonRpcResponse, useJsonRpc } from "@/hooks/useJsonRpc";
import useKeyboardLayout from "@/hooks/useKeyboardLayout";
import { SettingsItem } from "@components/SettingsItem";
import { SettingsPageHeader } from "@components/SettingsPageheader";
import { Checkbox } from "@/components/Checkbox";
import { SelectMenuBasic } from "@/components/SelectMenuBasic";
import notifications from "@/notifications";
import { useI18n } from "@/i18n";

export default function SettingsKeyboardRoute() {
  const { t } = useI18n();
  const { setKeyboardLayout } = useSettingsStore();
  const { showPressedKeys, setShowPressedKeys } = useSettingsStore();
  const { selectedKeyboard, keyboardOptions } = useKeyboardLayout();

  const { send } = useJsonRpc();

  useEffect(() => {
    send("getKeyboardLayout", {}, (resp: JsonRpcResponse) => {
      if ("error" in resp) return;
      const isoCode = resp.result as string;
      console.log("Fetched keyboard layout from backend:", isoCode);
      if (isoCode && isoCode.length > 0) {
        setKeyboardLayout(isoCode);
      }
    });
  }, [send, setKeyboardLayout]);

  const onKeyboardLayoutChange = useCallback(
    (e: React.ChangeEvent<HTMLSelectElement>) => {
      const isoCode = e.target.value;
      send("setKeyboardLayout", { layout: isoCode }, resp => {
        if ("error" in resp) {
          notifications.error(
            t("settings.keyboard.notify.setLayoutFail", { reason: resp.error.data || "Unknown error" }),
          );
        }
        notifications.success(t("settings.keyboard.notify.setLayoutOk", { iso: isoCode }));
        setKeyboardLayout(isoCode);
      });
    },
    [send, setKeyboardLayout],
  );

  return (
    <div className="space-y-4">
      <SettingsPageHeader
        title={t("settings.keyboard.title")}
        description={t("settings.keyboard.desc")}
      />

      <div className="space-y-4">
        <SettingsItem
          title={t("settings.keyboard.layoutTitle")}
          description={t("settings.keyboard.layoutDesc")}
        >
          <SelectMenuBasic
            size="SM"
            label=""
            fullWidth
            value={selectedKeyboard.isoCode}
            onChange={onKeyboardLayoutChange}
            options={keyboardOptions}
          />
        </SettingsItem>
        <p className="text-xs text-slate-600 dark:text-slate-400">
          {t("settings.keyboard.info")}
        </p>
      </div>

      <div className="space-y-4">
        <SettingsItem
          title={t("settings.keyboard.showPressedTitle")}
          description={t("settings.keyboard.showPressedDesc")}
        >
          <Checkbox
            checked={showPressedKeys}
            onChange={e => setShowPressedKeys(e.target.checked)}
          />
        </SettingsItem>
      </div>
    </div>
  );
}
