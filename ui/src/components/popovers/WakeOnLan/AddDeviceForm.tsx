import { useState, useRef } from "react";
import { LuPlus, LuArrowLeft } from "react-icons/lu";

import { InputFieldWithLabel } from "@/components/InputField";
import { Button } from "@/components/Button";
import { useI18n } from "@/i18n";

interface AddDeviceFormProps {
  onAddDevice: (name: string, macAddress: string) => void;
  setShowAddForm: (show: boolean) => void;
  errorMessage: string | null;
  setErrorMessage: (errorMessage: string | null) => void;
}

export default function AddDeviceForm({
  setShowAddForm,
  onAddDevice,
  errorMessage,
  setErrorMessage,
}: AddDeviceFormProps) {
  const { t } = useI18n();
  const [isDeviceNameValid, setIsDeviceNameValid] = useState<boolean>(false);
  const [isMacAddressValid, setIsMacAddressValid] = useState<boolean>(false);

  const nameInputRef = useRef<HTMLInputElement>(null);
  const macInputRef = useRef<HTMLInputElement>(null);

  return (
    <div className="space-y-4">
      <div
        className="animate-fadeIn opacity-0 space-y-4"
        style={{
          animationDuration: "0.5s",
          animationFillMode: "forwards",
        }}
      >
        <InputFieldWithLabel
          ref={nameInputRef}
          placeholder={t("popovers.wol.form.placeholderName")}
          label={t("popovers.wol.form.deviceName")}
          required
          onChange={e => {
            setIsDeviceNameValid(e.target.validity.valid);
            setErrorMessage(null);
          }}
          maxLength={30}
        />
        <InputFieldWithLabel
          ref={macInputRef}
          placeholder={t("popovers.wol.form.placeholderMac")}
          label={t("popovers.wol.form.mac")}
          onKeyUp={e => e.stopPropagation()}
          required
          pattern="^([0-9a-fA-F][0-9a-fA-F]:){5}([0-9a-fA-F][0-9a-fA-F])$"
          error={errorMessage}
          onChange={e => {
            setIsMacAddressValid(e.target.validity.valid);
            setErrorMessage(null);
          }}
          minLength={17}
          maxLength={17}
          onKeyDown={e => {
            if (isMacAddressValid || isDeviceNameValid) {
              if (e.key === "Enter" && (e.metaKey || e.ctrlKey)) {
                e.preventDefault();
                const deviceName = nameInputRef.current?.value || "";
                const macAddress = macInputRef.current?.value || "";
                onAddDevice(deviceName, macAddress);
              } else if (e.key === "Escape") {
                e.preventDefault();
                setShowAddForm(false);
              }
            }
          }}
        />
      </div>
      <div
        className="flex animate-fadeIn opacity-0 items-center justify-end space-x-2"
        style={{
          animationDuration: "0.7s",
          animationDelay: "0.2s",
        }}
      >
        <Button
          size="SM"
          theme="light"
          text={t("popovers.wol.form.back")}
          LeadingIcon={LuArrowLeft}
          onClick={() => setShowAddForm(false)}
        />
        <Button
          size="SM"
          theme="primary"
          text={t("popovers.wol.form.save")}
          disabled={!isDeviceNameValid || !isMacAddressValid}
          onClick={() => {
            const deviceName = nameInputRef.current?.value || "";
            const macAddress = macInputRef.current?.value || "";
            onAddDevice(deviceName, macAddress);
          }}
          LeadingIcon={LuPlus}
        />
      </div>
    </div>
  );
}
