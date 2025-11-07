import { useNavigate, useParams } from "react-router";
import { useState, useEffect } from "react";
import { LuTrash2 } from "react-icons/lu";

import { KeySequence, useMacrosStore } from "@/hooks/stores";
import { SettingsPageHeader } from "@/components/SettingsPageheader";
import { MacroForm } from "@/components/MacroForm";
import notifications from "@/notifications";
import { Button } from "@/components/Button";
import { ConfirmDialog } from "@/components/ConfirmDialog";
import { useI18n } from "@/i18n";

const normalizeSortOrders = (macros: KeySequence[]): KeySequence[] => {
  return macros.map((macro, index) => ({
    ...macro,
    sortOrder: index + 1,
  }));
};

export default function SettingsMacrosEditRoute() {
  const { t } = useI18n();
  const { macros, saveMacros } = useMacrosStore();
  const [isUpdating, setIsUpdating] = useState(false);
  const [isDeleting, setIsDeleting] = useState(false);
  const navigate = useNavigate();
  const { macroId } = useParams<{ macroId: string }>();
  const [macro, setMacro] = useState<KeySequence | null>(null);
  const [showDeleteConfirm, setShowDeleteConfirm] = useState(false);

  useEffect(() => {
    const foundMacro = macros.find(m => m.id === macroId);
    if (foundMacro) {
      setMacro({
        ...foundMacro,
        steps: foundMacro.steps.map(step => ({
          ...step,
          keys: Array.isArray(step.keys) ? step.keys : [],
          modifiers: Array.isArray(step.modifiers) ? step.modifiers : [],
          delay: typeof step.delay === 'number' ? step.delay : 0
        }))
      });
    } else {
      navigate("../");
    }
  }, [macroId, macros, navigate]);

  const handleUpdateMacro = async (updatedMacro: Partial<KeySequence>) => {
    if (!macro) return;

    setIsUpdating(true);
    try {
      const newMacros = macros.map(m => 
        m.id === macro.id ? {
          ...macro,
          name: updatedMacro.name!.trim(),
          steps: updatedMacro.steps || [],
        } : m
      );

      await saveMacros(normalizeSortOrders(newMacros));
      notifications.success(`Macro "${updatedMacro.name}" updated successfully`);
      navigate("../");
    } catch (error: unknown) {
      if (error instanceof Error) {
        notifications.error(`Failed to update macro: ${error.message}`);
      } else {
        notifications.error("Failed to update macro");
      }
    } finally {
      setIsUpdating(false);
    }
  };

  const handleDeleteMacro = async () => {
    if (!macro) return;

    setIsDeleting(true);
    try {
      const updatedMacros = normalizeSortOrders(macros.filter(m => m.id !== macro.id));
      await saveMacros(updatedMacros);
      notifications.success(`Macro "${macro.name}" deleted successfully`);
      navigate("../macros");
    } catch (error: unknown) {
      if (error instanceof Error) {
        notifications.error(`Failed to delete macro: ${error.message}`);
      } else {
        notifications.error("Failed to delete macro");
      }
    } finally {
      setIsDeleting(false);
    }
  };

  if (!macro) return null;

  return (
    <div className="space-y-4">
      <div className="flex items-center justify-between">
        <SettingsPageHeader
          title={t("settings.macros.editPage.title")}
          description={t("settings.macros.editPage.desc")}
        />
        <Button
          size="SM"
          theme="light"
          text={t("settings.macros.editPage.deleteBtn")}
          className="text-red-500 dark:text-red-400"
          LeadingIcon={LuTrash2}
          onClick={() => setShowDeleteConfirm(true)}
          disabled={isDeleting}
        />
      </div>
      <MacroForm
        initialData={macro}
        onSubmit={handleUpdateMacro}
        onCancel={() => navigate("../")}
        isSubmitting={isUpdating}
        submitText={t("settings.macros.editPage.save")}
      />

      <ConfirmDialog
        open={showDeleteConfirm}
        onClose={() => setShowDeleteConfirm(false)}
        title={useI18n().t("settings.macros.editPage.deleteTitle")}
        description={useI18n().t("settings.macros.editPage.deleteDesc")}
        variant="danger"
        confirmText={isDeleting ? useI18n().t("settings.macros.editPage.deleting") : useI18n().t("settings.macros.delete")}
        onConfirm={() => {
          handleDeleteMacro();
          setShowDeleteConfirm(false);
        }}
        isConfirming={isDeleting}
      />
    </div>
  );
} 
