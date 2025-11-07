import React, { createContext, useCallback, useContext, useEffect, useMemo, useState } from "react";
import dayjs from "dayjs";
import en from "./locales/en.json";
import zhCN from "./locales/zh-CN.json";
import "dayjs/locale/zh-cn";

type Lang = "en" | "zh-CN";

interface Dict {
  [key: string]: string | Dict;
}

const catalogs: Record<Lang, Dict> = {
  en: en as Dict,
  "zh-CN": zhCN as Dict,
};

function normalizeLang(input?: string | null): Lang {
  const v = (input || "en").toLowerCase();
  if (v.startsWith("zh")) return "zh-CN";
  return "en";
}

function get(obj: Dict, path: string): string | undefined {
  return path.split(".").reduce<any>((acc, key) => (acc && (acc as Dict)[key]) as any, obj) as
    | string
    | undefined;
}

function interpolate(str: string, vars?: Record<string, any>): string {
  if (!vars) return str;
  return str.replace(/\{(\w+)\}/g, (_, k) => (k in vars ? String(vars[k]) : `{${k}}`));
}

interface I18nCtx {
  lang: Lang;
  setLang: (l: Lang) => void;
  t: (key: string, vars?: Record<string, any>) => string;
}

const Ctx = createContext<I18nCtx | null>(null);

export function I18nProvider({ children }: { children: React.ReactNode }) {
  const [lang, setLangState] = useState<Lang>(() => {
    const saved = localStorage.getItem("lang");
    return normalizeLang(saved || navigator.language);
  });

  const setLang = useCallback((l: Lang) => {
    setLangState(l);
    localStorage.setItem("lang", l);
  }, []);

  useEffect(() => {
    document.documentElement.lang = lang;
    dayjs.locale(lang === "zh-CN" ? "zh-cn" : "en");
  }, [lang]);

  const t = useCallback(
    (key: string, vars?: Record<string, any>) => {
      const primary = get(catalogs[lang], key);
      const fallback = get(catalogs.en, key);
      const text = (primary ?? fallback ?? key) as string;
      return interpolate(text, vars);
    },
    [lang],
  );

  const value = useMemo(() => ({ lang, setLang, t }), [lang, setLang, t]);
  return <Ctx.Provider value={value}>{children}</Ctx.Provider>;
}

export function useI18n() {
  const ctx = useContext(Ctx);
  if (!ctx) throw new Error("useI18n must be used within I18nProvider");
  return ctx;
}

export function LanguageSwitcher() {
  const { lang, setLang } = useI18n();
  return (
    <button
      className="rounded-md border px-2 py-1 text-xs dark:text-white border-slate-800/20 dark:border-slate-300/20 hover:bg-slate-100 dark:hover:bg-slate-700"
      onClick={() => setLang(lang === "en" ? "zh-CN" : "en")}
      title={lang === "en" ? "切换到中文" : "Switch to English"}
    >
      {lang === "en" ? "中文" : "EN"}
    </button>
  );
}
