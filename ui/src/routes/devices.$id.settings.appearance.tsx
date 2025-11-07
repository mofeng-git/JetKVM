import { useCallback, useState } from "react";

import { SettingsItem } from "@components/SettingsItem";

import { SettingsPageHeader } from "../components/SettingsPageheader";
import { SelectMenuBasic } from "../components/SelectMenuBasic";
import { useI18n } from "@/i18n";

export default function SettingsAppearanceRoute() {
  const { lang, setLang, t } = useI18n();
  const [currentTheme, setCurrentTheme] = useState(() => {
    return localStorage.theme || "system";
  });

  const handleThemeChange = useCallback((value: string) => {
    const root = document.documentElement;

    if (value === "system") {
      localStorage.removeItem("theme");
      // Check system preference
      const systemTheme = window.matchMedia("(prefers-color-scheme: dark)").matches
        ? "dark"
        : "light";
      root.classList.remove("light", "dark");
      root.classList.add(systemTheme);
    } else {
      localStorage.theme = value;
      root.classList.remove("light", "dark");
      root.classList.add(value);
    }
  }, []);

  return (
    <div className="space-y-4">
      <SettingsPageHeader
        title={t("appearance.title")}
        description={t("appearance.desc")}
      />
      <SettingsItem title={t("appearance.themeTitle")} description={t("appearance.themeDesc") }>
        <SelectMenuBasic
          size="SM"
          label=""
          value={currentTheme}
          options={[
            { value: "system", label: t("appearance.theme.system") },
            { value: "light", label: t("appearance.theme.light") },
            { value: "dark", label: t("appearance.theme.dark") },
          ]}
          onChange={e => {
            setCurrentTheme(e.target.value);
            handleThemeChange(e.target.value);
          }}
        />
      </SettingsItem>
      <SettingsItem title={t("appearance.languageTitle")} description={t("appearance.languageDesc") }>
        <SelectMenuBasic
          size="SM"
          label=""
          value={lang}
          options={[
            { value: "en", label: t("appearance.language.en") },
            { value: "zh-CN", label: t("appearance.language.zhCN") },
          ]}
          onChange={e => setLang(e.target.value as "en" | "zh-CN")}
        />
      </SettingsItem>
    </div>
  );
}
