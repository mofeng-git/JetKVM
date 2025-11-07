import { PlusCircleIcon } from "@heroicons/react/16/solid";
import { LuPlus } from "react-icons/lu";

import Card from "@/components/Card";
import { Button } from "@/components/Button";
import { useI18n } from "@/i18n";

export default function EmptyStateCard({
  onCancelWakeOnLanModal,
  setShowAddForm,
}: {
  onCancelWakeOnLanModal: () => void;
  setShowAddForm: (show: boolean) => void;
}) {
  const { t } = useI18n();
  return (
    <div className="select-none space-y-4">
      <Card className="animate-fadeIn opacity-0">
        <div className="flex items-center justify-center py-8 text-center">
          <div className="space-y-3">
            <div className="space-y-1">
              <div className="inline-block">
                <Card>
                  <div className="p-1">
                    <PlusCircleIcon className="h-4 w-4 shrink-0 text-blue-700 dark:text-white" />
                  </div>
                </Card>
              </div>
              <h3 className="text-sm font-semibold leading-none text-black dark:text-white">
                {t("popovers.wol.empty.noDevices")}
              </h3>
              <p className="text-xs leading-none text-slate-700 dark:text-slate-300">
                {t("popovers.wol.empty.addHint")}
              </p>
            </div>
          </div>
        </div>
      </Card>
      <div
        className="flex animate-fadeIn opacity-0 items-center justify-end space-x-2"
        style={{
          animationDuration: "0.7s",
          animationDelay: "0.2s",
        }}
      >
        <Button size="SM" theme="blank" text={t("popovers.wol.actions.close")} onClick={onCancelWakeOnLanModal} />
        <Button
          size="SM"
          theme="primary"
          text={t("popovers.wol.actions.add")}
          onClick={() => setShowAddForm(true)}
          LeadingIcon={LuPlus}
        />
      </div>
    </div>
  );
}
